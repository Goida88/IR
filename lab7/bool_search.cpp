#include <cstdio>
#include <cstdlib>
#include <cstdint>
#include <cstring>
#include <cerrno>

#include <string>
#include <vector>
#include <fstream>
#include <filesystem>
#include <chrono>
#include <algorithm>
#include <iostream>

namespace fs = std::filesystem;

static inline uint64_t now_ms() {
    using namespace std::chrono;
    return (uint64_t)duration_cast<milliseconds>(steady_clock::now().time_since_epoch()).count();
}

static void die(const std::string& msg) {
    std::fprintf(stderr, "error: %s\n", msg.c_str());
    std::exit(2);
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

struct DictEntry {
    uint32_t term_off;  
    uint32_t term_len;  
    uint32_t df;
    uint64_t post_off;  
    uint64_t post_len;   

struct DocRec {
    uint32_t docid;
    std::string lang;
    std::string title;
    std::string url;
};

struct Index {
    std::vector<char> term_pool;
    std::vector<DictEntry> dict;
    std::ifstream postings;
    std::vector<DocRec> docs_sorted;
    std::vector<uint32_t> universe;  
};

static const char* dict_term_ptr(const Index& ix, const DictEntry& e) {
    return ix.term_pool.data() + e.term_off;
}

static int term_cmp(const Index& ix, const DictEntry& e, const std::string& t) {
    const char* p = dict_term_ptr(ix, e);
    size_t n = e.term_len;
    size_t m = t.size();
    size_t k = std::min(n, m);
    int c = std::memcmp(p, t.data(), k);
    if (c != 0) return c;
    if (n < m) return -1;
    if (n > m) return 1;
    return 0;
}

static int binsearch_term(const Index& ix, const std::string& t) {
    int lo = 0, hi = (int)ix.dict.size() - 1;
    while (lo <= hi) {
        int mid = lo + (hi - lo)/2;
        int c = term_cmp(ix, ix.dict[(size_t)mid], t);
        if (c == 0) return mid;
        if (c < 0) lo = mid + 1;
        else hi = mid - 1;
    }
    return -1;
}

static const DocRec* find_doc(const Index& ix, uint32_t docid) {
    size_t lo = 0, hi = ix.docs_sorted.size();
    while (lo < hi) {
        size_t mid = lo + (hi - lo)/2;
        uint32_t v = ix.docs_sorted[mid].docid;
        if (v == docid) return &ix.docs_sorted[mid];
        if (v < docid) lo = mid + 1;
        else hi = mid;
    }
    return nullptr;
}

static void load_docs(Index& ix, const fs::path& docs_tsv) {
    std::ifstream in(docs_tsv, std::ios::binary);
    if (!in) die("cannot open docs.tsv: " + docs_tsv.string());

    std::string line;
    ix.docs_sorted.clear();
    ix.universe.clear();

    while (std::getline(in, line)) {
        if (line.empty()) continue;
        size_t p1 = line.find('\t'); if (p1==std::string::npos) continue;
        size_t p2 = line.find('\t', p1+1); if (p2==std::string::npos) continue;
        size_t p3 = line.find('\t', p2+1); if (p3==std::string::npos) continue;
        size_t p4 = line.find('\t', p3+1); if (p4==std::string::npos) continue;

        uint32_t docid = (uint32_t)std::strtoul(line.substr(0, p1).c_str(), nullptr, 10);
        DocRec d;
        d.docid = docid;
        d.lang = line.substr(p1+1, p2-p1-1);
        d.title = line.substr(p2+1, p3-p2-1);
        d.url = line.substr(p3+1, p4-p3-1);

        ix.docs_sorted.push_back(std::move(d));
        ix.universe.push_back(docid);
    }

    std::sort(ix.docs_sorted.begin(), ix.docs_sorted.end(), [](const DocRec& a, const DocRec& b){
        return a.docid < b.docid;
    });

    std::sort(ix.universe.begin(), ix.universe.end());
    ix.universe.erase(std::unique(ix.universe.begin(), ix.universe.end()), ix.universe.end());
}

static void load_dict(Index& ix, const fs::path& terms_tsv) {
    std::ifstream in(terms_tsv, std::ios::binary);
    if (!in) die("cannot open terms.tsv: " + terms_tsv.string());

    ix.term_pool.clear();
    ix.dict.clear();
    ix.term_pool.reserve(16 * 1024 * 1024);
    ix.dict.reserve(512 * 1024);

    std::string line;
    while (std::getline(in, line)) {
        if (line.empty()) continue;
        size_t p1 = line.find('\t'); if (p1==std::string::npos) continue;
        size_t p2 = line.find('\t', p1+1); if (p2==std::string::npos) continue;
        size_t p3 = line.find('\t', p2+1); if (p3==std::string::npos) continue;

        std::string term = line.substr(0, p1);
        DictEntry e{};
        e.term_off = (uint32_t)ix.term_pool.size();
        e.term_len = (uint32_t)term.size();
        e.df = (uint32_t)std::strtoul(line.substr(p1+1, p2-p1-1).c_str(), nullptr, 10);
        e.post_off = (uint64_t)std::strtoull(line.substr(p2+1, p3-p2-1).c_str(), nullptr, 10);
        e.post_len = (uint64_t)std::strtoull(line.substr(p3+1).c_str(), nullptr, 10);

        ix.term_pool.insert(ix.term_pool.end(), term.begin(), term.end());
        ix.dict.push_back(e);
    }
}

static void open_postings(Index& ix, const fs::path& postings_bin) {
    ix.postings = std::ifstream(postings_bin, std::ios::binary);
    if (!ix.postings) die("cannot open postings.bin: " + postings_bin.string());
}

struct PostList {
    uint32_t* a = nullptr;
    uint32_t n = 0;
};

static void pl_free(PostList& p) { std::free(p.a); p.a=nullptr; p.n=0; }

static PostList pl_clone_from_vec(const std::vector<uint32_t>& v) {
    PostList r;
    r.n = (uint32_t)v.size();
    if (r.n == 0) return r;
    r.a = (uint32_t*)std::malloc((size_t)r.n * sizeof(uint32_t));
    if (!r.a) die("out of memory (pl_clone)");
    std::memcpy(r.a, v.data(), (size_t)r.n * sizeof(uint32_t));
    return r;
}

static PostList load_postings(Index& ix, const std::string& term) {
    PostList r;
    int pos = binsearch_term(ix, term);
    if (pos < 0) return r;
    const DictEntry& e = ix.dict[(size_t)pos];
    if (e.df == 0 || e.post_len == 0) return r;

    r.n = e.df;
    r.a = (uint32_t*)std::malloc((size_t)r.n * sizeof(uint32_t));
    if (!r.a) die("out of memory (load_postings)");

    ix.postings.seekg((std::streamoff)e.post_off);
    ix.postings.read((char*)r.a, (std::streamsize)e.post_len);
    return r;
}

static PostList op_and(const PostList& A, const PostList& B) {
    PostList R;
    if (A.n == 0 || B.n == 0) return R;
    R.a = (uint32_t*)std::malloc((size_t)std::min(A.n, B.n) * sizeof(uint32_t));
    if (!R.a) die("out of memory (AND)");
    uint32_t i=0,j=0,k=0;
    while (i<A.n && j<B.n) {
        uint32_t a = A.a[i], b = B.a[j];
        if (a==b) { R.a[k++]=a; i++; j++; }
        else if (a<b) i++;
        else j++;
    }
    R.n = k;
    return R;
}

static PostList op_or(const PostList& A, const PostList& B) {
    PostList R;
    R.a = (uint32_t*)std::malloc((size_t)(A.n + B.n) * sizeof(uint32_t));
    if (!R.a) die("out of memory (OR)");
    uint32_t i=0,j=0,k=0;
    while (i<A.n && j<B.n) {
        uint32_t a = A.a[i], b = B.a[j];
        if (a==b) { R.a[k++]=a; i++; j++; }
        else if (a<b) { R.a[k++]=a; i++; }
        else { R.a[k++]=b; j++; }
    }
    while (i<A.n) R.a[k++]=A.a[i++];
    while (j<B.n) R.a[k++]=B.a[j++];
    R.n = k;
    return R;
}

static PostList op_not(const std::vector<uint32_t>& U, const PostList& B) {
    PostList R;
    if (U.empty()) return R;
    R.a = (uint32_t*)std::malloc((size_t)U.size() * sizeof(uint32_t));
    if (!R.a) die("out of memory (NOT)");
    uint32_t i=0, j=0, k=0;
    while (i < (uint32_t)U.size()) {
        uint32_t u = U[i];
        while (j < B.n && B.a[j] < u) j++;
        if (j < B.n && B.a[j] == u) {
        } else {
            R.a[k++] = u;
        }
        i++;
    }
    R.n = k;
    return R;
}

enum TokType { TT_TERM, TT_AND, TT_OR, TT_NOT, TT_LP, TT_RP, TT_END };

struct Tok {
    TokType t;
    std::string s;
};

static bool is_space(char c) { return c==' '||c=='\t'||c=='\n'||c=='\r'; }

static std::vector<Tok> lex(const std::string& q) {
    std::vector<Tok> out;
    size_t i = 0;
    while (i < q.size()) {
        while (i < q.size() && is_space(q[i])) i++;
        if (i >= q.size()) break;
        char c = q[i];
        if (c == '(') { out.push_back({TT_LP,"("}); i++; continue; }
        if (c == ')') { out.push_back({TT_RP,")"}); i++; continue; }
        if (c == '-') { out.push_back({TT_NOT,"-"}); i++; continue; }

        // word / term
        size_t j = i;
        while (j < q.size() && !is_space(q[j]) && q[j] != '(' && q[j] != ')') j++;
        std::string w = q.substr(i, j - i);
        std::string wl = w;
        lowercase_inplace(wl);

        if (wl == "and") out.push_back({TT_AND, w});
        else if (wl == "or") out.push_back({TT_OR, w});
        else if (wl == "not") out.push_back({TT_NOT, w});
        else out.push_back({TT_TERM, w});
        i = j;
    }
    out.push_back({TT_END,""});
    return out;
}

struct Parser {
    const std::vector<Tok>& toks;
    size_t pos = 0;
    Parser(const std::vector<Tok>& t): toks(t) {}
    const Tok& cur() const { return toks[pos]; }
    void eat(TokType t) {
        if (cur().t != t) die("parse error: unexpected token near '" + cur().s + "'");
        pos++;
    }
};

enum NodeType { N_TERM, N_AND, N_OR, N_NOT };

struct Node {
    NodeType t;
    std::string term;
    Node* a = nullptr;
    Node* b = nullptr;
};

static Node* node_new(NodeType t) {
    Node* n = new Node();
    n->t = t;
    return n;
}
static void node_free(Node* n) {
    if (!n) return;
    node_free(n->a);
    node_free(n->b);
    delete n;
}

static Node* parse_expr(Parser& p);

static Node* parse_primary(Parser& p) {
    if (p.cur().t == TT_TERM) {
        Node* n = node_new(N_TERM);
        n->term = p.cur().s;
        p.eat(TT_TERM);
        return n;
    }
    if (p.cur().t == TT_LP) {
        p.eat(TT_LP);
        Node* n = parse_expr(p);
        p.eat(TT_RP);
        return n;
    }
    die("parse error: expected term or '('");
    return nullptr;
}

static Node* parse_unary(Parser& p) {
    if (p.cur().t == TT_NOT) {
        p.eat(TT_NOT);
        Node* n = node_new(N_NOT);
        n->a = parse_unary(p);
        return n;
    }
    return parse_primary(p);
}

static Node* parse_and(Parser& p) {
    Node* left = parse_unary(p);
    while (p.cur().t == TT_AND) {
        p.eat(TT_AND);
        Node* n = node_new(N_AND);
        n->a = left;
        n->b = parse_unary(p);
        left = n;
    }
    return left;
}

static Node* parse_expr(Parser& p) {
    Node* left = parse_and(p);
    while (p.cur().t == TT_OR) {
        p.eat(TT_OR);
        Node* n = node_new(N_OR);
        n->a = left;
        n->b = parse_and(p);
        left = n;
    }
    return left;
}

static PostList eval(Index& ix, Node* n) {
    if (!n) return PostList{};
    if (n->t == N_TERM) {
        std::string t = n->term;
        lowercase_inplace(t);
        return load_postings(ix, t);
    }
    if (n->t == N_NOT) {
        PostList a = eval(ix, n->a);
        PostList r = op_not(ix.universe, a);
        pl_free(a);
        return r;
    }
    if (n->t == N_AND) {
        PostList a = eval(ix, n->a);
        PostList b = eval(ix, n->b);
        PostList r = op_and(a, b);
        pl_free(a); pl_free(b);
        return r;
    }
    if (n->t == N_OR) {
        PostList a = eval(ix, n->a);
        PostList b = eval(ix, n->b);
        PostList r = op_or(a, b);
        pl_free(a); pl_free(b);
        return r;
    }
    return PostList{};
}

static PostList run_query(Index& ix, const std::string& q) {
    auto toks = lex(q);
    Parser p(toks);
    Node* ast = parse_expr(p);
    if (p.cur().t != TT_END) die("parse error: trailing tokens");
    PostList r = eval(ix, ast);
    node_free(ast);
    return r;
}

static void print_results(Index& ix, const PostList& r, uint32_t topn) {
    uint32_t n = r.n;
    uint32_t show = std::min<uint32_t>(n, topn);
    for (uint32_t i = 0; i < show; i++) {
        uint32_t docid = r.a[i];
        const DocRec* d = find_doc(ix, docid);
        if (d) {
            std::printf("%u\t%s\t%s\t%s\n", docid, d->lang.c_str(), d->title.c_str(), d->url.c_str());
        } else {
            std::printf("%u\t?\t?\t?\n", docid);
        }
    }
}

static void usage() {
    std::fprintf(stderr,
        "Usage:\n"
        "  bool_search --index <dir> --query \"<expr>\" [--top N]\n"
        "  bool_search --index <dir>            (reads queries from stdin)\n");
}

int main(int argc, char** argv) {
    fs::path index_dir;
    std::string query;
    uint32_t topn = 20;

    for (int i = 1; i < argc; i++) {
        if (std::strcmp(argv[i], "--index") == 0 && i+1 < argc) index_dir = argv[++i];
        else if (std::strcmp(argv[i], "--query") == 0 && i+1 < argc) query = argv[++i];
        else if (std::strcmp(argv[i], "--top") == 0 && i+1 < argc) topn = (uint32_t)std::strtoul(argv[++i], nullptr, 10);
        else if (std::strcmp(argv[i], "-h") == 0 || std::strcmp(argv[i], "--help") == 0) { usage(); return 0; }
        else { usage(); return 2; }
    }

    if (index_dir.empty()) { usage(); return 2; }

    Index ix;
    uint64_t t0 = now_ms();
    load_docs(ix, index_dir / "docs.tsv");
    load_dict(ix, index_dir / "terms.tsv");
    open_postings(ix, index_dir / "postings.bin");
    uint64_t t1 = now_ms();
    std::fprintf(stderr, "[load] docs=%zu universe=%zu terms=%zu time_ms=%llu\n",
        ix.docs_sorted.size(), ix.universe.size(), ix.dict.size(), (unsigned long long)(t1-t0));

    auto handle_one = [&](const std::string& q) {
        if (q.empty()) return;
        uint64_t q0 = now_ms();
        PostList r = run_query(ix, q);
        uint64_t q1 = now_ms();
        std::fprintf(stderr, "[search] hits=%u time_ms=%llu query=%s\n",
            r.n, (unsigned long long)(q1-q0), q.c_str());
        print_results(ix, r, topn);
        pl_free(r);
    };

    if (!query.empty()) {
        handle_one(query);
        return 0;
    }


    std::string line;
    while (std::getline(std::cin, line)) {
        if (!line.empty() && line[0] == '#') continue;
        handle_one(line);
        std::printf("----\n");
    }
    return 0;
}
