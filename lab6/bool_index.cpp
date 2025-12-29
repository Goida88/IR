#include <cstdio>
#include <cstdlib>
#include <cstdint>
#include <cstring>

#include <string>
#include <vector>
#include <filesystem>
#include <fstream>
#include <chrono>
#include <algorithm>

namespace fs = std::filesystem;

static inline uint64_t now_ms() {
    using namespace std::chrono;
    return (uint64_t)duration_cast<milliseconds>(steady_clock::now().time_since_epoch()).count();
}

static uint64_t fnv1a64(const char* s, size_t n) {
    uint64_t h = 1469598103934665603ULL;
    for (size_t i = 0; i < n; i++) {
        h ^= (unsigned char)s[i];
        h *= 1099511628211ULL;
    }
    return h;
}

static void die(const std::string& msg) {
    std::fprintf(stderr, "error: %s\n", msg.c_str());
    std::exit(2);
}

static void ensure_dir(const fs::path& p) {
    std::error_code ec;
    fs::create_directories(p, ec);
    if (ec) die("failed to create dir: " + p.string());
}

static inline bool is_ascii_alpha(unsigned char c) { return (c>='A'&&c<='Z') || (c>='a'&&c<='z'); }
static inline bool is_ascii_digit(unsigned char c) { return (c>='0'&&c<='9'); }

static void lowercase_inplace(std::string& s) {
    for (size_t i = 0; i < s.size(); i++) {
        unsigned char c = (unsigned char)s[i];
        if (c >= 'A' && c <= 'Z') s[i] = (char)(c + 32);
    }

    for (size_t i = 0; i + 1 < s.size(); i++) {
        unsigned char b0 = (unsigned char)s[i];
        unsigned char b1 = (unsigned char)s[i+1];
        if (b0 == 0xD0) {
            if (b1 >= 0x90 && b1 <= 0x9F) {
                s[i+1] = (char)(b1 + 0x20);
            } else if (b1 >= 0xA0 && b1 <= 0xAF) {
                s[i] = (char)0xD1;
                s[i+1] = (char)(b1 - 0x20);
            } else if (b1 == 0x81) { // Ё
                s[i] = (char)0xD1;
                s[i+1] = (char)0x91;
            }
        }
    }
}

static void tokenize_line(const std::string& line, std::vector<std::string>& out_tokens) {
    std::string cur;

    auto is_cyr2_start = [&](size_t i)->bool {
        if (i + 1 >= line.size()) return false;
        unsigned char b0 = (unsigned char)line[i];
        unsigned char b1 = (unsigned char)line[i+1];
        if (!(b0 == 0xD0 || b0 == 0xD1)) return false;
        return (b1 >= 0x80 && b1 <= 0xBF);
    };
    auto is_ascii_alnum = [&](unsigned char c)->bool {
        return is_ascii_alpha(c) || is_ascii_digit(c);
    };
    auto is_tok_start = [&](size_t i)->bool {
        if (i >= line.size()) return false;
        unsigned char c = (unsigned char)line[i];
        return is_ascii_alnum(c) || is_cyr2_start(i);
    };

    auto flush = [&]() {
        if (!cur.empty()) {
            lowercase_inplace(cur);
            out_tokens.push_back(cur);
            cur.clear();
        }
    };

    size_t i = 0;
    while (i < line.size()) {
        unsigned char c = (unsigned char)line[i];

        if (is_ascii_alnum(c)) {
            cur.push_back((char)c);
            i++;
            continue;
        }

        if (is_cyr2_start(i)) {
            cur.push_back(line[i]);
            cur.push_back(line[i+1]);
            i += 2;
            continue;
        }

        if (!cur.empty()) {
            if (c=='-' || c=='+' || c=='\'') {
                if (is_tok_start(i+1)) {
                    cur.push_back((char)c);
                    i++;
                    continue;
                }
            }
            if (c=='.') {
                if (!cur.empty() && is_ascii_digit((unsigned char)cur.back()) && (i+1 < line.size())) {
                    unsigned char n = (unsigned char)line[i+1];
                    if (is_ascii_digit(n)) {
                        cur.push_back('.');
                        i++;
                        continue;
                    }
                }
            }
        }

        flush();
        i++;
    }

    flush();
}

struct PostingList {
    uint32_t* a;
    uint32_t n;
    uint32_t cap;
};

static void pl_init(PostingList* pl) { pl->a=nullptr; pl->n=0; pl->cap=0; }
static void pl_free(PostingList* pl) { std::free(pl->a); pl->a=nullptr; pl->n=pl->cap=0; }
static void pl_push(PostingList* pl, uint32_t docid) {
    if (pl->n == pl->cap) {
        uint32_t new_cap = (pl->cap==0?8u:pl->cap*2u);
        void* p = std::realloc(pl->a, (size_t)new_cap * sizeof(uint32_t));
        if (!p) die("out of memory in postings");
        pl->a = (uint32_t*)p;
        pl->cap = new_cap;
    }
    pl->a[pl->n++] = docid;
}

struct TermEntry {
    char* term;
    uint32_t term_len;
    uint32_t df;
    PostingList postings;
    TermEntry* next;
};

struct TermTable {
    TermEntry** buckets;
    uint32_t nb;
    uint32_t size;
};

static void tt_init(TermTable* tt, uint32_t nbuckets) {
    tt->nb = nbuckets;
    tt->size = 0;
    tt->buckets = (TermEntry**)std::calloc(tt->nb, sizeof(TermEntry*));
    if (!tt->buckets) die("out of memory in tt_init");
}

static void te_free(TermEntry* e) {
    if (!e) return;
    std::free(e->term);
    pl_free(&e->postings);
    std::free(e);
}

static void tt_free(TermTable* tt) {
    for (uint32_t i = 0; i < tt->nb; i++) {
        TermEntry* e = tt->buckets[i];
        while (e) {
            TermEntry* nx = e->next;
            te_free(e);
            e = nx;
        }
    }
    std::free(tt->buckets);
    tt->buckets = nullptr;
    tt->nb = tt->size = 0;
}

static TermEntry* tt_get_or_add(TermTable* tt, const char* term, uint32_t len) {
    uint64_t h = fnv1a64(term, len);
    uint32_t bi = (uint32_t)(h % tt->nb);
    for (TermEntry* e = tt->buckets[bi]; e; e = e->next) {
        if (e->term_len == len && std::memcmp(e->term, term, len) == 0) return e;
    }
    TermEntry* ne = (TermEntry*)std::calloc(1, sizeof(TermEntry));
    if (!ne) die("out of memory adding term");
    ne->term = (char*)std::malloc(len + 1u);
    if (!ne->term) die("out of memory term string");
    std::memcpy(ne->term, term, len);
    ne->term[len] = 0;
    ne->term_len = len;
    ne->df = 0;
    pl_init(&ne->postings);
    ne->next = tt->buckets[bi];
    tt->buckets[bi] = ne;
    tt->size += 1;
    return ne;
}

struct SeenSet {
    TermEntry** slots;
    uint32_t cap;
};

static uint64_t ptr_hash(const void* p) {
    uint64_t x = (uint64_t)(uintptr_t)p;
    x ^= x >> 33; x *= 0xff51afd7ed558ccdULL;
    x ^= x >> 33; x *= 0xc4ceb9fe1a85ec53ULL;
    x ^= x >> 33;
    return x;
}

static void seen_init(SeenSet* ss, uint32_t cap_pow2) {
    ss->cap = cap_pow2;
    ss->slots = (TermEntry**)std::calloc(ss->cap, sizeof(TermEntry*));
    if (!ss->slots) die("out of memory in seen_init");
}
static void seen_reset(SeenSet* ss) {
    std::memset(ss->slots, 0, (size_t)ss->cap * sizeof(TermEntry*));
}
static void seen_free(SeenSet* ss) { std::free(ss->slots); ss->slots=nullptr; ss->cap=0; }
static bool seen_insert(SeenSet* ss, TermEntry* e) {
    uint64_t h = ptr_hash(e);
    uint32_t mask = ss->cap - 1;
    uint32_t i = (uint32_t)h & mask;
    for (uint32_t step = 0; step < ss->cap; step++) {
        TermEntry* cur = ss->slots[i];
        if (!cur) { ss->slots[i] = e; return true; }
        if (cur == e) return false;
        i = (i + 1) & mask;
    }
    return false;
}

static uint32_t parse_docid_from_name(const fs::path& p) {
    std::string stem = p.stem().string();
    uint32_t v = 0;
    for (char c : stem) {
        if (c < '0' || c > '9') continue;
        v = v * 10u + (uint32_t)(c - '0');
    }
    return v;
}

static void parse_header(std::ifstream& in, std::string& title, std::string& url) {
    title.clear(); url.clear();
    std::string line;
    for (int i = 0; i < 6; i++) {
        if (!std::getline(in, line)) return;
        if (line.rfind("Title:", 0) == 0) {
            title = line.substr(6);
            if (!title.empty() && title[0]==' ') title.erase(0,1);
        } else if (line.rfind("URL:", 0) == 0) {
            url = line.substr(4);
            if (!url.empty() && url[0]==' ') url.erase(0,1);
        }
    }
}

struct BuildStats {
    uint32_t docs = 0;
    uint64_t bytes = 0;
    uint64_t tokens = 0;
    uint64_t unique_terms = 0;
    uint64_t postings = 0;
};

static void collect_text_files(const fs::path& corpus, std::vector<fs::path>& out) {
    for (auto it = fs::recursive_directory_iterator(corpus); it != fs::recursive_directory_iterator(); ++it) {
        if (!it->is_regular_file()) continue;
        auto p = it->path();
        if (p.extension() == ".txt") {
            auto s = p.generic_string();
            if (s.find("/text/") != std::string::npos) out.push_back(p);
        }
    }
    std::sort(out.begin(), out.end());
}

static void build_index(const fs::path& corpus, const fs::path& out_dir, uint32_t limit) {
    ensure_dir(out_dir);

    std::vector<fs::path> files;
    files.reserve(32000);
    collect_text_files(corpus, files);
    if (files.empty()) die("no .txt files found under: " + corpus.string());
    if (limit != 0 && limit < files.size()) files.resize(limit);

    TermTable tt;
    tt_init(&tt, 1u << 20);
    SeenSet seen;
    seen_init(&seen, 1u << 15);

    std::ofstream docs_out(out_dir / "docs.tsv", std::ios::binary);
    if (!docs_out) die("failed to open docs.tsv");

    BuildStats st;
    uint64_t t0 = now_ms();

    std::vector<std::string> tokens;
    tokens.reserve(4096);

    for (size_t fi = 0; fi < files.size(); fi++) {
        const fs::path& p = files[fi];
        std::ifstream in(p, std::ios::binary);
        if (!in) { std::fprintf(stderr, "warn: cannot open %s\n", p.string().c_str()); continue; }

        std::string lang = "unk";
        std::string ps = p.generic_string();
        if (ps.find("/enwiki/") != std::string::npos) lang = "en";
        else if (ps.find("/ruwiki/") != std::string::npos) lang = "ru";

        std::string title, url;
        parse_header(in, title, url);
        uint32_t docid_local = parse_docid_from_name(p);
        uint32_t docid = docid_local + ((lang == "ru") ? 30000u : 0u);
        docs_out << docid << "\t" << lang << "\t" << title << "\t" << url << "\t" << p.generic_string() << "\n";

        seen_reset(&seen);

        std::string line;
        while (std::getline(in, line)) {
            st.bytes += (uint64_t)line.size() + 1;
            tokens.clear();
            tokenize_line(line, tokens);

            for (size_t ti = 0; ti < tokens.size(); ti++) {
                const std::string& tok = tokens[ti];
                if (tok.empty()) continue;
                st.tokens += 1;

                TermEntry* e = tt_get_or_add(&tt, tok.data(), (uint32_t)tok.size());
                if (seen_insert(&seen, e)) {
                    pl_push(&e->postings, docid);
                    e->df += 1;
                    st.postings += 1;
                }
            }
        }

        st.docs += 1;
        if (st.docs % 500 == 0) {
            uint64_t dt = now_ms() - t0;
            std::fprintf(stderr, "[build] docs=%u terms=%u postings=%llu tokens=%llu time_ms=%llu\n",
                st.docs, tt.size,
                (unsigned long long)st.postings,
                (unsigned long long)st.tokens,
                (unsigned long long)dt
            );
        }
    }

    st.unique_terms = tt.size;

    TermEntry** arr = (TermEntry**)std::malloc((size_t)tt.size * sizeof(TermEntry*));
    if (!arr) die("out of memory collecting terms");
    uint32_t idx = 0;
    for (uint32_t bi = 0; bi < tt.nb; bi++) {
        for (TermEntry* e = tt.buckets[bi]; e; e = e->next) arr[idx++] = e;
    }
    if (idx != tt.size) die("internal: term count mismatch");

    std::qsort(arr, tt.size, sizeof(TermEntry*), [](const void* a, const void* b)->int{
        const TermEntry* x = *(const TermEntry* const*)a;
        const TermEntry* y = *(const TermEntry* const*)b;
        return std::strcmp(x->term, y->term);
    });

    std::ofstream postings(out_dir / "postings.bin", std::ios::binary);
    if (!postings) die("failed to open postings.bin");
    std::ofstream terms(out_dir / "terms.tsv", std::ios::binary);
    if (!terms) die("failed to open terms.tsv");

    uint64_t offset = 0;
    for (uint32_t i = 0; i < tt.size; i++) {
        TermEntry* e = arr[i];
        std::sort(e->postings.a, e->postings.a + e->postings.n);
        uint32_t* a = e->postings.a;
        uint32_t n = e->postings.n;
        uint32_t w = 0;
        for (uint32_t j = 0; j < n; j++) {
            if (w == 0 || a[j] != a[w-1]) a[w++] = a[j];
        }
        e->postings.n = w;
        e->df = w;

        uint64_t bytes_len = (uint64_t)e->postings.n * sizeof(uint32_t);
        terms << e->term << "\t" << e->df << "\t" << offset << "\t" << bytes_len << "\n";
        postings.write((const char*)e->postings.a, (std::streamsize)bytes_len);
        offset += bytes_len;
    }

    std::free(arr);
    seen_free(&seen);

    uint64_t t1 = now_ms();
    double sec = (double)(t1 - t0) / 1000.0;
    double kb = (double)st.bytes / 1024.0;

    std::fprintf(stderr,
        "[done] docs=%u unique_terms=%llu postings=%llu bytes=%.0fKB tokens=%llu time=%.3fs speed=%.1fKB/s\n",
        st.docs,
        (unsigned long long)st.unique_terms,
        (unsigned long long)st.postings,
        kb,
        (unsigned long long)st.tokens,
        sec,
        sec > 0.0 ? kb / sec : 0.0
    );

    tt_free(&tt);
}

static void lookup_term(const fs::path& index_dir, const std::string& term) {
    std::ifstream terms(index_dir / "terms.tsv", std::ios::binary);
    if (!terms) die("cannot open terms.tsv");

    std::string line;
    uint64_t offset = 0, bytes_len = 0;
    uint32_t df = 0;
    bool found = false;

    while (std::getline(terms, line)) {
        size_t p1 = line.find('\t');
        if (p1 == std::string::npos) continue;
        std::string t = line.substr(0, p1);
        if (t != term) continue;
        size_t p2 = line.find('\t', p1+1);
        size_t p3 = line.find('\t', p2+1);
        if (p2==std::string::npos||p3==std::string::npos) die("bad terms.tsv format");
        df = (uint32_t)std::strtoul(line.substr(p1+1, p2-p1-1).c_str(), nullptr, 10);
        offset = (uint64_t)std::strtoull(line.substr(p2+1, p3-p2-1).c_str(), nullptr, 10);
        bytes_len = (uint64_t)std::strtoull(line.substr(p3+1).c_str(), nullptr, 10);
        found = true;
        break;
    }

    if (!found) { std::printf("NOT FOUND\n"); return; }

    std::ifstream postings(index_dir / "postings.bin", std::ios::binary);
    if (!postings) die("cannot open postings.bin");
    postings.seekg((std::streamoff)offset);
    std::vector<uint32_t> docs(df);
    postings.read((char*)docs.data(), (std::streamsize)bytes_len);

    std::printf("term=%s df=%u\n", term.c_str(), df);
    uint32_t show = std::min<uint32_t>(df, 30);
    for (uint32_t i = 0; i < show; i++) std::printf("%u\n", docs[i]);
    if (df > show) std::printf("... (%u more)\n", df - show);
}

static void usage() {
    std::fprintf(stderr,
        "Usage:\n"
        "  bool_index build  --corpus <dir> --out <dir> [--limit N]\n"
        "  bool_index lookup --index  <dir> --term <term>\n");
}

static bool arg_eq(const char* a, const char* b) { return std::strcmp(a,b)==0; }

int main(int argc, char** argv) {
    if (argc < 2) { usage(); return 2; }
    std::string cmd = argv[1];

    if (cmd == "build") {
        fs::path corpus, out;
        uint32_t limit = 0;
        for (int i = 2; i < argc; i++) {
            if (arg_eq(argv[i], "--corpus") && i+1 < argc) corpus = argv[++i];
            else if (arg_eq(argv[i], "--out") && i+1 < argc) out = argv[++i];
            else if (arg_eq(argv[i], "--limit") && i+1 < argc) limit = (uint32_t)std::strtoul(argv[++i], nullptr, 10);
            else { usage(); return 2; }
        }
        if (corpus.empty() || out.empty()) { usage(); return 2; }
        build_index(corpus, out, limit);
        return 0;
    }

    if (cmd == "lookup") {
        fs::path index;
        std::string term;
        for (int i = 2; i < argc; i++) {
            if (arg_eq(argv[i], "--index") && i+1 < argc) index = argv[++i];
            else if (arg_eq(argv[i], "--term") && i+1 < argc) term = argv[++i];
            else { usage(); return 2; }
        }
        if (index.empty() || term.empty()) { usage(); return 2; }
        std::string t = term;
        lowercase_inplace(t);
        lookup_term(index, t);
        return 0;
    }

    usage();
    return 2;
}
