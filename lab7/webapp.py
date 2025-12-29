# MAI-IR Lab 8 (🔴): simple web UI (Flask) for boolean search
# Run in Colab:
#   !pip -q install flask
#   !python webapp.py --index /content/bool_index_full --bin /content/lab8_boolean_search/bool_search --port 8080
#
# Then use Colab "Running on public URL" link.
import argparse, subprocess, html, os
from flask import Flask, request

app = Flask(__name__)
CFG = {"index": "", "bin": "", "top": 20}

HTML_PAGE = """<!doctype html>
<html><head><meta charset="utf-8"><title>MAI-IR Boolean Search</title>
<style>
body{font-family:Arial, sans-serif; max-width:900px; margin:20px auto;}
input[type=text]{width:100%; padding:10px; font-size:16px;}
button{padding:10px 14px; font-size:16px;}
pre{background:#f5f5f5; padding:10px; overflow:auto;}
.small{color:#666; font-size:12px;}
</style></head>
<body>
<h2>Boolean Search (Lab 8)</h2>
<form method="GET">
  <div class="small">Query examples: <code>neuroscience AND brain</code>,
  <code>мозг AND нейрон</code>, <code>neuroscience AND NOT disorder</code></div>
  <p><input type="text" name="q" value="{q}" placeholder="Enter boolean query..."></p>
  <p><button type="submit">Search</button></p>
</form>
{content}
</body></html>
"""

def run_search(q: str) -> tuple[str, str]:
    cmd = [CFG["bin"], "--index", CFG["index"], "--query", q, "--top", str(CFG["top"])]
    p = subprocess.run(cmd, capture_output=True, text=True, errors="replace")
    return p.stdout, p.stderr

@app.route("/", methods=["GET"])
def home():
    q = request.args.get("q", "").strip()
    if not q:
        return HTML_PAGE.format(q="", content="")
    out, err = run_search(q)
    esc_out = html.escape(out)
    esc_err = html.escape(err)
    content = f"<h3>Results</h3><pre>{esc_out}</pre><h3>Debug</h3><pre>{esc_err}</pre>"
    return HTML_PAGE.format(q=html.escape(q), content=content)

def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--index", required=True)
    ap.add_argument("--bin", required=True)
    ap.add_argument("--port", type=int, default=8080)
    ap.add_argument("--top", type=int, default=20)
    args = ap.parse_args()
    CFG["index"] = args.index
    CFG["bin"] = args.bin
    CFG["top"] = args.top
    app.run(host="0.0.0.0", port=args.port, debug=False)

if __name__ == "__main__":
    main()
