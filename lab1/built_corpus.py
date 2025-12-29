import os
import re
import json
import time
import random
import argparse
from collections import deque
import requests

try:
    from tqdm import tqdm
except Exception:
    tqdm = None

UA = "MAI-IR-Lab1-WikiCorpus"

SEED_EN_WIDE = [
    "Category:Medicine",
    "Category:Biology",
    "Category:Human_anatomy",
    "Category:Physiology",
    "Category:Pathology",
    "Category:Medical_specialties",
    "Category:Diseases",
    "Category:Symptoms_and_signs",
    "Category:Medical_diagnosis",
    "Category:Medical_tests",
    "Category:Medical_treatments",
    "Category:Pharmacology",
    "Category:Drugs",
    "Category:Biochemistry",
    "Category:Molecular_biology",
    "Category:Cell_biology",
    "Category:Genetics",
    "Category:Immunology",
    "Category:Microbiology",
    "Category:Epidemiology",
    "Category:Public_health",
    "Category:Medical_imaging",
    "Category:Neuroscience",
    "Category:Neurology",
    "Category:Brain",
    "Category:Nervous_system",
    "Category:Psychiatry",
    "Category:Mental_disorders",
    "Category:Neurological_disorders",
]

SEED_RU_WIDE = [
    "Категория:Медицина",
    "Категория:Биология",
    "Категория:Анатомия",
    "Категория:Физиология",
    "Категория:Патология",
    "Категория:Болезни",
    "Категория:Диагностика",
    "Категория:Лечение",
    "Категория:Фармакология",
    "Категория:Психиатрия",
    "Категория:Психология",
    "Категория:Неврология",
    "Категория:Нейронаука",
    "Категория:Психические_расстройства",
    "Категория:Неврологические_расстройства",
]

def jload(path, default):
    if os.path.exists(path):
        with open(path, "r", encoding="utf-8") as f:
            return json.load(f)
    return default

def jdump(path, obj):
    tmp = path + ".tmp"
    with open(tmp, "w", encoding="utf-8") as f:
        json.dump(obj, f, ensure_ascii=False, indent=2)
    os.replace(tmp, path)

def count_words(s: str) -> int:
    return len([w for w in re.split(r"\s+", (s or "").strip()) if w])

class WikiCorpus:
    def __init__(self, root: str, lang: str, target: int, seeds, min_words: int, depth: int, sleep_s: float):
        self.root = root
        self.lang = lang
        self.target = target
        self.seeds = list(seeds)
        self.min_words = min_words
        self.depth = depth
        self.sleep_s = sleep_s

        part = "enwiki" if lang == "en" else "ruwiki"
        self.part_dir = os.path.join(root, part)
        self.raw_dir = os.path.join(self.part_dir, "raw")
        self.text_dir = os.path.join(self.part_dir, "text")
        os.makedirs(self.raw_dir, exist_ok=True)
        os.makedirs(self.text_dir, exist_ok=True)

        self.meta_path = os.path.join(self.part_dir, "metadata.json")
        self.meta = jload(self.meta_path, [])

        self.seen_pageids = set()
        self.docid = 0
        for it in self.meta:
            try:
                self.seen_pageids.add(int(it["pageid"]))
                self.docid = max(self.docid, int(it["docid"]))
            except Exception:
                pass

        self.api = f"https://{lang}.wikipedia.org/w/api.php"
        self.sess = requests.Session()
        self.sess.headers.update({"User-Agent": UA, "Accept": "application/json"})

    def api_get(self, params, retries=7):
        backoff = 1.0
        for _ in range(retries):
            try:
                r = self.sess.get(self.api, params=params, timeout=30)
                if r.status_code in (429, 500, 502, 503):
                    time.sleep(backoff + random.random() * 0.2)
                    backoff = min(backoff * 1.8, 20)
                    continue
                r.raise_for_status()
                time.sleep(self.sleep_s)
                return r.json()
            except Exception:
                time.sleep(backoff + random.random() * 0.2)
                backoff = min(backoff * 1.8, 20)
        return None

    def iter_category_members(self, cat_title: str):
        cmcontinue = None
        while True:
            params = {
                "action": "query",
                "format": "json",
                "formatversion": "2",
                "list": "categorymembers",
                "cmtitle": cat_title,
                "cmtype": "page|subcat",
                "cmnamespace": "0|14",
                "cmlimit": "500",
                "cmshow": "!redirect",
            }
            if cmcontinue:
                params["cmcontinue"] = cmcontinue

            data = self.api_get(params)
            if not data:
                return

            members = data.get("query", {}).get("categorymembers", [])
            for m in members:
                yield m

            cmcontinue = data.get("continue", {}).get("cmcontinue")
            if not cmcontinue:
                return

    def fetch_pages(self, pageids):
        params = {
            "action": "query",
            "format": "json",
            "formatversion": "2",
            "prop": "extracts|revisions|info",
            "inprop": "url",
            "explaintext": "1",
            "exsectionformat": "wiki",
            "rvslots": "main",
            "rvprop": "content",
            "pageids": "|".join(str(x) for x in pageids),
        }
        data = self.api_get(params)
        if not data:
            return []
        return data.get("query", {}).get("pages", [])

    def save_doc(self, page) -> bool:
        if "missing" in page:
            return False

        pid = int(page.get("pageid", 0))
        if not pid or pid in self.seen_pageids:
            return False

        text = (page.get("extract") or "").strip()
        if not text:
            return False

        words = count_words(text)
        if words < self.min_words:
            return False

        title = page.get("title", "")
        url = page.get("fullurl", "")
        length_bytes = int(page.get("length", 0))

        wikitext = ""
        revs = page.get("revisions", [])
        if revs:
            wikitext = (revs[0].get("slots", {}).get("main", {}).get("content", "")) or ""

        self.docid += 1
        txt_name = f"{self.docid:05d}.txt"
        raw_name = f"{self.docid:05d}.wiki"

        with open(os.path.join(self.text_dir, txt_name), "w", encoding="utf-8") as f:
            f.write(f"Title: {title}\n")
            f.write(f"PageID: {pid}\n")
            f.write(f"URL: {url}\n")
            f.write(f"LengthBytes: {length_bytes}\n")
            f.write(f"Words: {words}\n\n")
            f.write(text + "\n")

        with open(os.path.join(self.raw_dir, raw_name), "w", encoding="utf-8") as f:
            f.write(wikitext)

        self.meta.append({
            "lang": self.lang,
            "docid": self.docid,
            "title": title,
            "pageid": pid,
            "url": url,
            "length_bytes": length_bytes,
            "words": words,
            "text_file": f"text/{txt_name}",
            "raw_file": f"raw/{raw_name}",
        })
        self.seen_pageids.add(pid)
        return True

    def run(self, save_every=200):
        need = self.target - len(self.seen_pageids)
        if need <= 0:
            print(f"[{self.lang}] already have {len(self.seen_pageids)} docs")
            return

        q = deque((c, 0) for c in self.seeds)
        seen_cats = set()
        buf = []

        bar = tqdm(total=need, desc=f"Download {self.lang}") if tqdm else None
        added = 0

        while q and added < need:
            cat, d = q.popleft()
            if cat in seen_cats:
                continue
            seen_cats.add(cat)

            for m in self.iter_category_members(cat):
                if added >= need:
                    break

                ns = m.get("ns")
                if ns == 14 and d < self.depth:
                    sub = m.get("title")
                    if sub and sub not in seen_cats:
                        q.append((sub, d + 1))
                elif ns == 0:
                    pid = int(m.get("pageid", 0))
                    if pid and pid not in self.seen_pageids:
                        buf.append(pid)

                if len(buf) >= 50:
                    pages = self.fetch_pages(buf[:50])
                    buf = buf[50:]
                    for p in pages:
                        if self.save_doc(p):
                            added += 1
                            if bar: bar.update(1)
                            if added % save_every == 0:
                                jdump(self.meta_path, self.meta)
                        if added >= need:
                            break

        while buf and added < need:
            pages = self.fetch_pages(buf[:50])
            buf = buf[50:]
            for p in pages:
                if self.save_doc(p):
                    added += 1
                    if bar: bar.update(1)
                    if added % save_every == 0:
                        jdump(self.meta_path, self.meta)
                if added >= need:
                    break

        if bar: bar.close()
        jdump(self.meta_path, self.meta)
        print(f"[{self.lang}] done: total={len(self.seen_pageids)} added={added}")

def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--root", default="corpus", help="папка корпуса")
    ap.add_argument("--en", type=int, default=28000, help="сколько документов EN")
    ap.add_argument("--ru", type=int, default=2000, help="сколько документов RU")
    ap.add_argument("--min_words_en", type=int, default=450)
    ap.add_argument("--min_words_ru", type=int, default=350)
    ap.add_argument("--depth_en", type=int, default=4)
    ap.add_argument("--depth_ru", type=int, default=3)
    ap.add_argument("--sleep", type=float, default=0.06)
    args = ap.parse_args()

    os.makedirs(args.root, exist_ok=True)

    en = WikiCorpus(args.root, "en", args.en, SEED_EN_WIDE, args.min_words_en, args.depth_en, args.sleep)
    ru = WikiCorpus(args.root, "ru", args.ru, SEED_RU_WIDE, args.min_words_ru, args.depth_ru, args.sleep)

    en.run()
    ru.run()

if __name__ == "__main__":
    main()
