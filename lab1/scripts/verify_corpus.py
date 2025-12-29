#!/usr/bin/env python3
import os, json, sys
from collections import Counter

def load_meta(path):
    with open(path, "r", encoding="utf-8") as f:
        return json.load(f)

def count_ext(path, ext):
    return sum(1 for x in os.listdir(path) if x.endswith(ext)) if os.path.exists(path) else 0

def check_part(root, part):
    src = os.path.join(root, part)
    meta_path = os.path.join(src, "metadata.json")
    if not os.path.exists(meta_path):
        return [f"{part}: missing metadata.json"]

    meta = load_meta(meta_path)
    tdir = os.path.join(src, "text")
    rdir = os.path.join(src, "raw")

    problems = []
    txt_n = count_ext(tdir, ".txt")
    raw_n = count_ext(rdir, ".wiki")

    if txt_n != len(meta):
        problems.append(f"{part}: text files {txt_n} != metadata {len(meta)}")
    if raw_n != len(meta):
        problems.append(f"{part}: raw files {raw_n} != metadata {len(meta)}")

    miss = 0
    for it in meta:
        tp = os.path.join(src, it["text_file"])
        rp = os.path.join(src, it["raw_file"])
        if not (os.path.exists(tp) and os.path.exists(rp)):
            miss += 1
    if miss:
        problems.append(f"{part}: missing refs = {miss}")

    pids = [int(it.get("pageid", -1)) for it in meta]
    dup = sum(1 for _,v in Counter(pids).items() if v > 1)
    if dup:
        problems.append(f"{part}: duplicate pageid groups = {dup}")

    return problems

def main():
    root = sys.argv[1] if len(sys.argv) > 1 else "."
    problems = []
    for part in ["enwiki", "ruwiki"]:
        problems += check_part(root, part)

    if problems:
        print("FAIL")
        for p in problems:
            print("-", p)
        sys.exit(1)

    print("OK")
    sys.exit(0)

if __name__ == "__main__":
    main()
