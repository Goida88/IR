import yaml
import sys
import time
import requests
import hashlib
import json
from urllib.parse import urljoin, urlparse, unquote
from pathlib import Path
from collections import deque
from bs4 import BeautifulSoup

def get_hash(html):
    return hashlib.md5(html.encode()).hexdigest()

def normalize_url(url):
    parsed = urlparse(url)
    return unquote(f"{parsed.scheme}://{parsed.netloc}{parsed.path}")

def load_existing_corpus(corpus_dir):
    urls = []
    wiki_dir = Path(corpus_dir) / "raw_wikipedia"
    if wiki_dir.exists():
        for html_file in wiki_dir.glob("*.html"):
            title = html_file.stem.replace("_", " ")
            urls.append(f"https://en.wikipedia.org/wiki/{title}")
        print(f"Загружено {len(urls)} URL из корпуса")
    return urls

if len(sys.argv) != 2:
    print("Usage: python crawler.py config.yaml")
    sys.exit(1)

config = yaml.safe_load(open(sys.argv[1]))

db_file = Path("serbia_crawler.json")
if db_file.exists():
    documents = json.loads(db_file.read_text())
else:
    documents = {}

corpus_dir = config['logic'].get('corpus_dir', '')
seeds = deque()
if corpus_dir:
    seeds.extend(load_existing_corpus(corpus_dir))

print(f"Начальный размер очереди: {len(seeds)}")
processed = 0

while seeds and processed < config['logic']['max_docs']:
    url = seeds.popleft()
    norm_url = normalize_url(url)
    
    existing = documents.get(norm_url)
    
    try:
        resp = requests.get(url, timeout=30)
        resp.raise_for_status()
        html = resp.text
        content_hash = get_hash(html)
        
        doc = {
            'html': html,
            'source': urlparse(url).netloc,
            'fetch_time': int(time.time()),
            'content_hash': content_hash,
            'last_checked': int(time.time())
        }
        
        if existing:
            if existing['content_hash'] != content_hash:
                documents[norm_url] = doc
                print(f"Обновлен документ: {norm_url}")
            else:
                documents[norm_url]['last_checked'] = int(time.time())
        else:
            documents[norm_url] = doc
            print(f"Добавлен документ: {norm_url}")
        
        processed += 1
        
        soup = BeautifulSoup(html, 'html.parser')
        for a in soup.find_all('a', href=True):
            new_url = urljoin(url, a['href'])
            if urlparse(new_url).netloc == urlparse(url).netloc:
                seeds.append(new_url)
                
    except Exception as e:
        print(f"Ошибка при обработке {url}: {e}")
    
    time.sleep(config['logic']['delay'])

db_file.write_text(json.dumps(documents, ensure_ascii=False))
print(f"Обработано документов: {processed}")
print(f"Всего в БД: {len(documents)}")
