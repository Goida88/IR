#include <algorithm>
#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
#include <unordered_map>
#include <vector>

namespace fs = std::filesystem;

static inline bool is_ascii_upper(uint32_t cp) { return cp >= 'A' && cp <= 'Z'; }
static inline bool is_ascii_digit(uint32_t cp) { return cp >= '0' && cp <= '9'; }

static inline bool is_cyr_upper(uint32_t cp) { return (cp >= 0x0410 && cp <= 0x042F) || cp == 0x0401; }
static inline bool is_cyr_lower(uint32_t cp) { return (cp >= 0x0430 && cp <= 0x044F) || cp == 0x0451; }
static inline bool is_greek(uint32_t cp) { return (cp >= 0x0370 && cp <= 0x03FF); }

static inline uint32_t to_lower_cp(uint32_t cp) {
    if (is_ascii_upper(cp)) return cp + 32;
    if (cp >= 0x0410 && cp <= 0x042F) return cp + 32;
    if (cp == 0x0401) return 0x0451;
    return cp;
}

static inline bool is_letter(uint32_t cp) {
    if ((cp >= 'a' && cp <= 'z') || (cp >= 'A' && cp <= 'Z')) return true;
    if (is_cyr_lower(cp) || is_cyr_upper(cp)) return true;
    if (is_greek(cp)) return true;
    if (cp == 0x00B5) return true; // µ
    return false;
}
static inline bool is_alnum(uint32_t cp) { return is_letter(cp) || is_ascii_digit(cp); }

static inline bool is_hyphen(uint32_t cp) {
    return cp == 0x002D || cp == 0x2010 || cp == 0x2011 || cp == 0x2012 || cp == 0x2212;
}
static inline bool is_apostrophe(uint32_t cp) { return cp == 0x0027 || cp == 0x2019; }

static bool decode_utf8_next(const std::string& s, size_t& i, uint32_t& out_cp) {
    if (i >= s.size()) return false;
    unsigned char c0 = (unsigned char)s[i];
    if (c0 < 0x80) { out_cp = c0; i += 1; return true; }
    if ((c0 & 0xE0) == 0xC0) {
        if (i + 1 >= s.size()) { out_cp = 0xFFFD; i += 1; return true; }
        unsigned char c1 = (unsigned char)s[i+1];
        if ((c1 & 0xC0) != 0x80) { out_cp = 0xFFFD; i += 1; return true; }
        out_cp = ((uint32_t)(c0 & 0x1F) << 6) | (uint32_t)(c1 & 0x3F);
        i += 2; return true;
    }
    if ((c0 & 0xF0) == 0xE0) {
        if (i + 2 >= s.size()) { out_cp = 0xFFFD; i += 1; return true; }
        unsigned char c1 = (unsigned char)s[i+1];
        unsigned char c2 = (unsigned char)s[i+2];
        if (((c1 & 0xC0) != 0x80) || ((c2 & 0xC0) != 0x80)) { out_cp = 0xFFFD; i += 1; return true; }
        out_cp = ((uint32_t)(c0 & 0x0F) << 12) | ((uint32_t)(c1 & 0x3F) << 6) | (uint32_t)(c2 & 0x3F);
        i += 3; return true;
    }
    if ((c0 & 0xF8) == 0xF0) {
        if (i + 3 >= s.size()) { out_cp = 0xFFFD; i += 1; return true; }
        unsigned char c1 = (unsigned char)s[i+1];
        unsigned char c2 = (unsigned char)s[i+2];
        unsigned char c3 = (unsigned char)s[i+3];
        if (((c1 & 0xC0) != 0x80) || ((c2 & 0xC0) != 0x80) || ((c3 & 0xC0) != 0x80)) { out_cp = 0xFFFD; i += 1; return true; }
        out_cp = ((uint32_t)(c0 & 0x07) << 18) | ((uint32_t)(c1 & 0x3F) << 12) | ((uint32_t)(c2 & 0x3F) << 6) | (uint32_t)(c3 & 0x3F);
        i += 4; return true;
    }
    out_cp = 0xFFFD; i += 1; return true;
}

static void append_utf8(std::string& out, uint32_t cp) {
    if (cp <= 0x7F) out.push_back((char)cp);
    else if (cp <= 0x7FF) {
        out.push_back((char)(0xC0 | ((cp >> 6) & 0x1F)));
        out.push_back((char)(0x80 | (cp & 0x3F)));
    } else if (cp <= 0xFFFF) {
        out.push_back((char)(0xE0 | ((cp >> 12) & 0x0F)));
        out.push_back((char)(0x80 | ((cp >> 6) & 0x3F)));
        out.push_back((char)(0x80 | (cp & 0x3F)));
    } else {
        out.push_back((char)(0xF0 | ((cp >> 18) & 0x07)));
        out.push_back((char)(0x80 | ((cp >> 12) & 0x3F)));
        out.push_back((char)(0x80 | ((cp >> 6) & 0x3F)));
        out.push_back((char)(0x80 | (cp & 0x3F)));
    }
}

struct Stats { uint64_t files=0, bytes=0, tokens=0, token_len_sum=0, errors=0; };
struct Options { std::string input; bool print=false; uint64_t limit=0; std::string freq_out; };

static void tokenize_line(const std::string& line, Stats& st, const Options& opt, std::unordered_map<std::string,uint64_t>* freq) {
    size_t i=0; std::string tok; uint64_t tok_len=0;
    auto flush = [&](){
        if(!tok.empty()){
            st.tokens++; st.token_len_sum += tok_len;
            if(opt.print) std::cout << tok << "\n";
            if(freq) (*freq)[tok] += 1;
            tok.clear(); tok_len=0;
        }
    };
    while(i < line.size()){
        size_t j=i; uint32_t cp=0;
        if(!decode_utf8_next(line, j, cp)) break;

        uint32_t next=0; size_t k=j; bool has_next = decode_utf8_next(line, k, next);

        if(is_alnum(cp)){
            cp = to_lower_cp(cp);
            append_utf8(tok, cp); tok_len++; i=j; continue;
        }
        if(is_hyphen(cp) && !tok.empty() && has_next && is_alnum(next)){
            append_utf8(tok, cp); tok_len++; i=j; continue;
        }
        if(cp=='+' && !tok.empty() && has_next && is_alnum(next)){
            append_utf8(tok, cp); tok_len++; i=j; continue;
        }
        if(cp=='.'){
            bool prev_digit = (!tok.empty() && tok.back()>='0' && tok.back()<='9');
            bool next_digit = (has_next && is_ascii_digit(next));
            if(prev_digit && next_digit){
                append_utf8(tok, cp); tok_len++; i=j; continue;
            }
        }
        if(is_apostrophe(cp) && !tok.empty() && has_next && is_letter(next)){
            append_utf8(tok, cp); tok_len++; i=j; continue;
        }
        flush(); i=j;
    }
    if(!tok.empty()){
        st.tokens++; st.token_len_sum += tok_len;
        if(opt.print) std::cout << tok << "\n";
        if(freq) (*freq)[tok] += 1;
    }
}

static void skip_metadata(std::ifstream& in){
    std::string tmp;
    for(int i=0;i<6;i++){
        if(!std::getline(in,tmp)) break;
    }
}

static bool process_file(const fs::path& p, Stats& st, const Options& opt, std::unordered_map<std::string,uint64_t>* freq){
    std::ifstream in(p, std::ios::binary);
    if(!in){ st.errors++; return false; }
    skip_metadata(in);
    std::string line;
    while(std::getline(in,line)){
        st.bytes += (uint64_t)line.size() + 1;
        tokenize_line(line, st, opt, freq);
    }
    st.files++; return true;
}

static void write_freq(const std::string& out_path, const std::unordered_map<std::string,uint64_t>& freq){
    std::vector<std::pair<std::string,uint64_t>> v;
    v.reserve(freq.size());
    for(const auto& kv: freq) v.push_back(kv);
    std::sort(v.begin(), v.end(), [](const auto& a, const auto& b){
        if(a.second!=b.second) return a.second>b.second;
        return a.first<b.first;
    });
    std::ofstream out(out_path, std::ios::binary);
    for(const auto& kv: v) out << kv.first << "\t" << kv.second << "\n";
}

static void usage(){
    std::cerr << "Usage: tokenize --input <file_or_dir> [--print] [--limit N] [--freq-out out.tsv]\n";
}

static bool parse_u64(const std::string& s, uint64_t& out){
    if(s.empty()) return false;
    char* end=nullptr; unsigned long long v = std::strtoull(s.c_str(), &end, 10);
    if(!end || *end!='\0') return false;
    out = (uint64_t)v; return true;
}

static bool parse_args(int argc, char** argv, Options& opt){
    for(int i=1;i<argc;i++){
        std::string a=argv[i];
        if(a=="--input" && i+1<argc) opt.input=argv[++i];
        else if(a=="--print") opt.print=true;
        else if(a=="--limit" && i+1<argc){ uint64_t v=0; if(!parse_u64(argv[++i],v)) return false; opt.limit=v; }
        else if(a=="--freq-out" && i+1<argc) opt.freq_out=argv[++i];
        else if(a=="--help"||a=="-h"){ usage(); std::exit(0); }
        else { std::cerr<<"Unknown arg: "<<a<<"\n"; return false; }
    }
    return !opt.input.empty();
}

int main(int argc, char** argv){
    Options opt;
    if(!parse_args(argc, argv, opt)){ usage(); return 2; }

    fs::path in_path(opt.input);
    if(!fs::exists(in_path)){ std::cerr<<"Input path does not exist: "<<opt.input<<"\n"; return 2; }

    Stats st;
    std::unordered_map<std::string,uint64_t> freq;
    std::unordered_map<std::string,uint64_t>* freq_ptr=nullptr;
    if(!opt.freq_out.empty()){ freq_ptr=&freq; freq.reserve(200000); }

    auto t0 = std::chrono::high_resolution_clock::now();

    if(fs::is_regular_file(in_path)){
        process_file(in_path, st, opt, freq_ptr);
    } else if(fs::is_directory(in_path)){
        std::vector<fs::path> files; files.reserve(35000);
        for(auto it=fs::recursive_directory_iterator(in_path); it!=fs::recursive_directory_iterator(); ++it){
            if(!it->is_regular_file()) continue;
            auto p=it->path();
            if(p.has_extension() && p.extension()==".txt") files.push_back(p);
        }
        std::sort(files.begin(), files.end());
        uint64_t taken=0;
        for(const auto& p: files){
            if(opt.limit!=0 && taken>=opt.limit) break;
            process_file(p, st, opt, freq_ptr);
            taken++;
        }
    } else { std::cerr<<"Input must be file or directory.\n"; return 2; }

    auto t1 = std::chrono::high_resolution_clock::now();
    double sec = std::chrono::duration<double>(t1-t0).count();

    double avg_len = st.tokens ? (double)st.token_len_sum / (double)st.tokens : 0.0;
    double kb = (double)st.bytes / 1024.0;
    double kbps = sec>0.0 ? kb/sec : 0.0;

    std::cerr << "[tokenize] files="<<st.files
              << " bytes="<<st.bytes
              << " tokens="<<st.tokens
              << " avg_token_len="<<avg_len
              << " time_sec="<<sec
              << " speed_kb_per_sec="<<kbps
              << " errors="<<st.errors
              << "\n";

    if(freq_ptr){
        write_freq(opt.freq_out, freq);
        std::cerr << "[tokenize] freq_out="<<opt.freq_out<<" unique_terms="<<freq.size()<<"\n";
    }
    return st.errors?1:0;
}
