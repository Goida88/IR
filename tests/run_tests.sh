#!/usr/bin/env bash
set -euo pipefail

CORPUS="${1:-/content/corpus}"
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
LOG="$ROOT/tests/test_output.txt"

echo "Corpus: $CORPUS" | tee "$LOG"
echo "--- verify_corpus.py ---" | tee -a "$LOG"
python3 "$ROOT/scripts/verify_corpus.py" "$CORPUS" | tee -a "$LOG"

echo "--- compute_stats.py ---" | tee -a "$LOG"
python3 "$ROOT/scripts/compute_stats.py" "$CORPUS" | tee -a "$LOG"

echo "OK" | tee -a "$LOG"
