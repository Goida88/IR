#!/usr/bin/env python3
import os, subprocess, sys

BIN = os.environ.get("BOOL_SEARCH_BIN", "/content/lab8_boolean_search/bool_search")
IDX = os.environ.get("INDEX_DIR", "/content/bool_index_full")

def run(q):
    p = subprocess.run([BIN, "--index", IDX, "--query", q, "--top", "5"], capture_output=True, text=True, errors="replace")
    return p.returncode, p.stdout, p.stderr

def main():
    if not os.path.exists(BIN):
        print("bool_search binary not found:", BIN, file=sys.stderr)
        return 2
    if not os.path.exists(IDX):
        print("index_dir not found:", IDX, file=sys.stderr)
        return 2

    rc, out, err = run("neuroscience")
    assert rc == 0 and "neuroscience" in err

    rc, out2, err2 = run("neuroscience AND brain")
    assert rc == 0

    rc, out3, err3 = run("neuroscience AND NOT disorder")
    assert rc == 0

    print("OK")
    return 0

if __name__ == "__main__":
    raise SystemExit(main())
