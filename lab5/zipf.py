import argparse, os, re, time, json
from collections import Counter
from pathlib import Path

import numpy as np
import matplotlib.pyplot as plt


def iter_text_files(corpus_dir: Path):
    for lang_dir in sorted(corpus_dir.iterdir()):
        if not lang_dir.is_dir():
            continue
        text_dir = lang_dir / "text"
        if not text_dir.exists():
            continue
        for p in sorted(text_dir.glob("*.txt")):
            yield p


def read_body(path: Path) -> str:
    with path.open("r", encoding="utf-8", errors="replace") as f:
        for _ in range(6):
            if f.readline() == "":
                break
        return f.read()


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--corpus", required=True)
    ap.add_argument("--out", default="zipf.png")
    ap.add_argument("--stats", default="zipf_stats.json")
    ap.add_argument("--max_docs", type=int, default=0)
    ap.add_argument("--fit_from", type=int, default=10)
    ap.add_argument("--fit_to", type=int, default=50000)
    ap.add_argument("--min_freq", type=int, default=1)
    args = ap.parse_args()

    corpus_dir = Path(args.corpus)
    rx = re.compile(r"[0-9A-Za-zА-Яа-яЁё]+(?:[-_][0-9A-Za-zА-Яа-яЁё]+)*")
    tf = Counter()
    docs = 0
    bytes_total = 0
    t0 = time.time()

    for fp in iter_text_files(corpus_dir):
        body = read_body(fp)
        bytes_total += len(body.encode("utf-8", errors="replace"))
        toks = rx.findall(body.lower())
        if toks:
            tf.update(toks)
        docs += 1
        if docs % 1000 == 0:
            dt = time.time() - t0
            kb = bytes_total / 1024.0
            sp = kb / dt if dt > 0 else 0.0
            print(f"[zipf] docs={docs} unique_terms={len(tf)} kb={kb:.1f} sec={dt:.2f} kb_per_sec={sp:.1f}")
        if args.max_docs and docs >= args.max_docs:
            break

    t1 = time.time()
    total_tokens = int(sum(tf.values()))
    items = [(term, c) for term, c in tf.items() if c >= args.min_freq]
    items.sort(key=lambda x: x[1], reverse=True)

    freqs = np.array([c for _, c in items], dtype=np.float64)
    ranks = np.arange(1, freqs.size + 1, dtype=np.float64)

    fit_from = max(1, int(args.fit_from))
    fit_to = min(int(args.fit_to), int(freqs.size))
    if fit_to <= fit_from:
        fit_from = 1
        fit_to = min(50000, int(freqs.size))

    x = np.log(ranks[fit_from - 1 : fit_to])
    y = np.log(freqs[fit_from - 1 : fit_to])
    m, b = np.polyfit(x, y, 1)
    s = float(-m)
    C = float(np.exp(b))

    plt.figure(figsize=(9, 6))
    plt.loglog(ranks, freqs, marker=".", linestyle="None", markersize=2, label="Corpus TF")
    plt.loglog(ranks, C / (ranks ** s), linewidth=2, label=f"Zipf (C/r^s), s={s:.3f}")
    plt.xlabel("Rank r")
    plt.ylabel("Frequency f")
    plt.grid(True, which="both", linestyle="--", linewidth=0.5)
    plt.legend()
    plt.tight_layout()
    plt.savefig(args.out, dpi=150)

    stats = {
        "docs": int(docs),
        "bytes_text_utf8": int(bytes_total),
        "total_tokens": int(total_tokens),
        "unique_terms": int(len(tf)),
        "min_freq": int(args.min_freq),
        "fit_from": int(fit_from),
        "fit_to": int(fit_to),
        "zipf_s": s,
        "zipf_C": C,
        "time_sec": float(t1 - t0),
        "speed_kb_per_sec": float((bytes_total / 1024.0) / (t1 - t0) if (t1 - t0) > 0 else 0.0),
        "top_terms": items[:20],
    }
    with open(args.stats, "w", encoding="utf-8") as f:
        json.dump(stats, f, ensure_ascii=False, indent=2)

    print(f"[zipf] done docs={docs} total_tokens={total_tokens} unique_terms={len(tf)} s={s:.6f} out={args.out} stats={args.stats}")


if __name__ == "__main__":
    main()
