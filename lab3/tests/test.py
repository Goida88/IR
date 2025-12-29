#!/usr/bin/env python3
import subprocess, sys, os, pathlib

ROOT = pathlib.Path(__file__).resolve().parents[1]
EXE = ROOT / ("tokenize.exe" if os.name == "nt" else "tokenize")
SAMPLE = ROOT / "tests" / "sample.txt"

def main():
    if not EXE.exists():
        print("Build tokenizer first (tokenize.exe missing).", file=sys.stderr)
        return 2
    p = subprocess.run([str(EXE), "--input", str(SAMPLE), "--print"], capture_output=True, text=True, errors="replace")
    out = p.stdout + "\n" + p.stderr
    must = ["covid-19", "cd4+", "t-cells", "alzheimer's", "человек-нейрон", "3.14", "β-amyloid", "μ-opioid", "receptors"]
    ok = all(m in out for m in must)
    print("OK" if ok else "FAIL")
    if not ok:
        for m in must:
            if m not in out:
                print("Missing:", m, file=sys.stderr)
    return 0 if ok else 1

if __name__ == "__main__":
    raise SystemExit(main())
