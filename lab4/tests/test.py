#!/usr/bin/env python3
import subprocess, os, sys, pathlib

ROOT = pathlib.Path(__file__).resolve().parents[1]
EXE = ROOT / ("stem.exe" if os.name == "nt" else "stem")
SAMPLE = ROOT / "tests" / "sample_tokens.txt"

def main():
    if not EXE.exists():
        print("Build stemmer first (stem.exe missing).", file=sys.stderr)
        return 2
    p = subprocess.run([str(EXE), "--lang", "auto", "--input", str(SAMPLE)], capture_output=True, text=True, errors="replace")
    out = [x for x in p.stdout.splitlines() if x.strip()]
    expected = {
        "covid-19": "covid-19",
        "t-cells": "t-cell",      # drops plural s
        "modeling": "model",
        "studies": "studi",
        "memory": "memori",
        "alzheimer's": "alzheimer's",
        "нейронауки": "нейронаук",
        "психических": "психическ",
        "исследования": "исследован",
        "лечиться": "леч"
    }
    inp = SAMPLE.read_text(encoding="utf-8").splitlines()
    got = dict(zip(inp, out))
    ok = True
    for k, v in expected.items():
        if got.get(k) != v:
            print(f"Mismatch for {k}: got={got.get(k)} expected={v}", file=sys.stderr)
            ok = False
    print("OK" if ok else "FAIL")
    return 0 if ok else 1

if __name__ == "__main__":
    raise SystemExit(main())
