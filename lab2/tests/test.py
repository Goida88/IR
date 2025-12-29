#!/usr/bin/env python3
# -*- coding: utf-8 -*-

import os
import tempfile
import yaml
import json
import subprocess
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]

def main():
    mongo_uri = os.environ.get("MONGO_URI")
    if not mongo_uri:
        print("Set env MONGO_URI to your MongoDB Atlas connection string for this smoke test.")
        return 2

    tmp = Path(tempfile.mkdtemp())
    # fake metadata.json with 3 wiki pages
    meta = [
        {"lang": "enwiki", "docid": "00001", "pageid": 1, "title": "Python (programming language)", "url": "https://en.wikipedia.org/wiki/Python_(programming_language)"},
        {"lang": "enwiki", "docid": "00002", "pageid": 2, "title": "Neuroscience", "url": "https://en.wikipedia.org/wiki/Neuroscience"},
        {"lang": "ruwiki", "docid": "00001", "pageid": 3, "title": "Психическое здоровье", "url": "https://ru.wikipedia.org/wiki/%D0%9F%D1%81%D0%B8%D1%85%D0%B8%D1%87%D0%B5%D1%81%D0%BA%D0%BE%D0%B5_%D0%B7%D0%B4%D0%BE%D1%80%D0%BE%D0%B2%D1%8C%D0%B5"},
    ]
    (tmp / "metadata.json").write_text(json.dumps(meta, ensure_ascii=False), encoding="utf-8")

    cfg = {
        "db": {"mongo_uri": mongo_uri, "mongo_db": "mai_ir_test", "mongo_collection": "pages"},
        "logic": {"delay_sec": 0.1, "timeout_sec": 20, "refetch_after_sec": 60, "retry_after_sec": 30},
        "seeds": {"metadata_paths": [str(tmp / "metadata.json")]},
    }
    (tmp / "config.yaml").write_text(yaml.safe_dump(cfg, allow_unicode=True), encoding="utf-8")

    print("Running robot for ~10 seconds; then Ctrl+C automatically not handled here, so we just timeout process.")
    p = subprocess.Popen(["python", str(ROOT / "robot.py"), str(tmp / "config.yaml")], cwd=str(ROOT))
    try:
        p.wait(timeout=10)
    except subprocess.TimeoutExpired:
        p.terminate()
        print("OK: terminated robot after 10 seconds. Check Atlas DB mai_ir_test.pages")

    return 0

if __name__ == "__main__":
    raise SystemExit(main())
