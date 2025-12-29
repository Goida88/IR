#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <stdint.h>

#ifndef _WIN32
#define strtok_s strtok_r
#endif

static void rstrip_newline(char* s) {
    size_t n = strlen(s);
    while (n > 0 && (s[n-1] == '\n' || s[n-1] == '\r')) {
        s[n-1] = 0;
        n--;
    }
}

static int ends_with_bytes(const char* s, const char* suf) {
    size_t n = strlen(s), m = strlen(suf);
    if (m > n) return 0;
    return memcmp(s + (n - m), suf, m) == 0;
}

static void cut_suffix(char* s, size_t suf_len) {
    size_t n = strlen(s);
    if (suf_len > n) return;
    s[n - suf_len] = 0;
}

static int has_cyrillic_utf8(const char* s) {
    const unsigned char* p = (const unsigned char*)s;
    while (*p) {
        if (*p == 0xD0 || *p == 0xD1) return 1;
        p++;
    }
    return 0;
}

static int is_vowel_en(char c) {
    return c=='a'||c=='e'||c=='i'||c=='o'||c=='u';
}

static int has_vowel_en(const char* s) {
    for (size_t i = 0; s[i]; i++) {
        char c = s[i];
        if (is_vowel_en(c)) return 1;
        if (c=='y' && i>0) return 1;
    }
    return 0;
}

static int is_consonant_en(char c) {
    if (c<'a' || c>'z') return 0;
    return !is_vowel_en(c) && c!='y';
}

static int ends_with_double_consonant_en(const char* s) {
    size_t n = strlen(s);
    if (n < 2) return 0;
    char a = s[n-1], b = s[n-2];
    if (a != b) return 0;
    return is_consonant_en(a);
}

static void stem_en(char* tok) {
    size_t n = strlen(tok);
    if (n < 3) return;

    if (ends_with_bytes(tok, "sses")) { cut_suffix(tok, 2); }           
    else if (ends_with_bytes(tok, "ies")) { cut_suffix(tok, 2); }        
    else if (ends_with_bytes(tok, "ss")) { /* keep */ }
    else if (ends_with_bytes(tok, "s") && n > 3) {                        
        char tmp[512];
        if (n >= sizeof(tmp)) return;
        strcpy(tmp, tok);
        tmp[n-1] = 0;
        if (has_vowel_en(tmp)) cut_suffix(tok, 1);
    }

    n = strlen(tok);
    if (n < 3) return;

    if (ends_with_bytes(tok, "eed")) {
        if (strlen(tok) > 4) { tok[strlen(tok)-1] = 0; } // eed -> ee (drop 'd')
    } else if (ends_with_bytes(tok, "ed")) {
        char tmp[512];
        if (strlen(tok) >= sizeof(tmp)) return;
        strcpy(tmp, tok);
        cut_suffix(tmp, 2);
        if (has_vowel_en(tmp)) cut_suffix(tok, 2);
    } else if (ends_with_bytes(tok, "ing")) {
        char tmp[512];
        if (strlen(tok) >= sizeof(tmp)) return;
        strcpy(tmp, tok);
        cut_suffix(tmp, 3);
        if (has_vowel_en(tmp)) cut_suffix(tok, 3);
    }

    n = strlen(tok);
    if (n >= 2) {
        const char* tail2 = tok + (n - 2);
        if (strcmp(tail2, "at") == 0 || strcmp(tail2, "bl") == 0 || strcmp(tail2, "iz") == 0) {
            if (n + 1 < 512) { tok[n] = 'e'; tok[n+1] = 0; }
        }
    }

    n = strlen(tok);
    if (ends_with_double_consonant_en(tok)) {
        char last = tok[n-1];
        if (last != 'l' && last != 's' && last != 'z') tok[n-1] = 0;
    }

    n = strlen(tok);
    if (n >= 2 && tok[n-1] == 'y') {
        char tmp[512];
        if (n >= sizeof(tmp)) return;
        strcpy(tmp, tok);
        tmp[n-1] = 0;
        if (has_vowel_en(tmp)) tok[n-1] = 'i';
    }
}

static size_t min_left_bytes_ru(void) {
    return 4;
}

static int strip_first_match_ru(char* tok, const char* const* sufs, size_t count) {
    size_t n = strlen(tok);
    for (size_t i = 0; i < count; i++) {
        const char* suf = sufs[i];
        size_t m = strlen(suf);
        if (m == 0) continue;
        if (m < n && ends_with_bytes(tok, suf) && (n - m) >= min_left_bytes_ru()) {
            cut_suffix(tok, m);
            return 1;
        }
    }
    return 0;
}

static void stem_ru(char* tok) {
    static const char* reflexive[] = {"ся","сь"};

    static const char* adj[] = {
        "ыми","ими","ого","ему","ому","ее","ие","ое","ая","яя","ый","ий","ой","ые","ие","ых","их","ую","юю"
    };

    static const char* verb[] = {
        "ившись","ывшись","вшись",
        "иться",
        "ать","ять","еть","ить","ыть","нуть",
        "ала","яла","ела","ила","ыла","али","яли","ели","или","ылы",
        "ает","яет","еет","ит","ют","уют","яют","ешь","ишь","ем","им","ете","ите",
        "ал","ял","ел","ил","ыл"
    };

    static const char* noun[] = {
        "иями","ями","ами",
        "ов","ев","ей","ам","ям","ах","ях","ом","ем","ой","ей","ою","ею",
        "а","я","у","ю","е","о","ы","и","ь"
    };

    (void)strip_first_match_ru(tok, reflexive, sizeof(reflexive)/sizeof(reflexive[0]));

    if (strip_first_match_ru(tok, adj, sizeof(adj)/sizeof(adj[0]))) {
        strip_first_match_ru(tok, noun, sizeof(noun)/sizeof(noun[0]));
        return;
    }
    if (strip_first_match_ru(tok, verb, sizeof(verb)/sizeof(verb[0]))) return;
    strip_first_match_ru(tok, noun, sizeof(noun)/sizeof(noun[0]));
}

static void stem_auto(char* tok) {
    if (has_cyrillic_utf8(tok)) stem_ru(tok);
    else stem_en(tok);
}

static void usage() {
    fprintf(stderr,
        "Usage: stem --lang auto|en|ru [--input tokens.txt]\n"
        "Reads tokens (one per line), writes stems to stdout.\n");
}

int main(int argc, char** argv) {
    const char* lang = "auto";
    const char* input = NULL;

    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--lang") == 0 && i + 1 < argc) {
            lang = argv[++i];
        } else if (strcmp(argv[i], "--input") == 0 && i + 1 < argc) {
            input = argv[++i];
        } else if (strcmp(argv[i], "--help") == 0 || strcmp(argv[i], "-h") == 0) {
            usage();
            return 0;
        } else {
            fprintf(stderr, "Unknown arg: %s\n", argv[i]);
            usage();
            return 2;
        }
    }

    FILE* in = stdin;
    if (input) {
        in = fopen(input, "rb");
        if (!in) {
            fprintf(stderr, "Failed to open input: %s\n", input);
            return 2;
        }
    }

    char buf[8192];
    unsigned long long lines = 0, changed = 0;

    while (fgets(buf, sizeof(buf), in)) {
        rstrip_newline(buf);
        if (buf[0] == 0) continue;

        char orig[8192];
        strncpy(orig, buf, sizeof(orig)-1);
        orig[sizeof(orig)-1] = 0;

        if (strcmp(lang, "en") == 0) stem_en(buf);
        else if (strcmp(lang, "ru") == 0) stem_ru(buf);
        else stem_auto(buf);

        if (strcmp(orig, buf) != 0) changed++;

        printf("%s\n", buf);
        lines++;
    }

    if (input) fclose(in);

    fprintf(stderr, "[stem] tokens_in=%llu changed=%llu lang=%s\n", lines, changed, lang);
    return 0;
}
