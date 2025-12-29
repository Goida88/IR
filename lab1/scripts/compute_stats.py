#!/usr/bin/env python3
import os, json, statistics

def load_meta(p):
    with open(p, "r", encoding="utf-8") as f:
        return json.load(f)

def folder_size_bytes(path):
    total = 0
    for root, _, files in os.walk(path):
        for fn in files:
            fp = os.path.join(root, fn)
            try:
                total += os.path.getsize(fp)
            except:
                pass
    return total

def mb(x): return x/(1024*1024)

def stats_one(src_dir, name):
    meta = load_meta(os.path.join(src_dir, "metadata.json"))
    raw_dir = os.path.join(src_dir, "raw")
    txt_dir = os.path.join(src_dir, "text")
    raw_sz = folder_size_bytes(raw_dir)
    txt_sz = folder_size_bytes(txt_dir)

    words = []
    for it in meta:
        try:
            words.append(int(it.get("words", 0)))
        except:
            pass
    words.sort()

    out = {
        "name": name,
        "docs": len(meta),
        "raw_size_mb": round(mb(raw_sz), 2),
        "text_size_mb": round(mb(txt_sz), 2),
        "avg_raw_bytes_per_doc": round(raw_sz/max(1,len(meta)), 2),
        "avg_text_bytes_per_doc": round(txt_sz/max(1,len(meta)), 2),
        "avg_words_per_doc": round(sum(words)/max(1,len(words)), 2) if words else 0,
        "median_words_per_doc": int(words[len(words)//2]) if words else 0,
        "min_words": int(words[0]) if words else 0,
        "max_words": int(words[-1]) if words else 0,
    }
    return out

def main():
    import sys
    root = sys.argv[1] if len(sys.argv) > 1 else "."
    en = stats_one(os.path.join(root, "enwiki"), "ENWIKI")
    ru = stats_one(os.path.join(root, "ruwiki"), "RUWIKI")
    total = {
        "docs": en["docs"] + ru["docs"],
        "raw_size_mb": round(en["raw_size_mb"] + ru["raw_size_mb"], 2),
        "text_size_mb": round(en["text_size_mb"] + ru["text_size_mb"], 2),
    }
    print(json.dumps({"enwiki": en, "ruwiki": ru, "total": total}, ensure_ascii=False, indent=2))

if __name__ == "__main__":
    main()
