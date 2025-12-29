#!/usr/bin/env python3
# -*- coding: utf-8 -*-

from __future__ import annotations

import sys
import os
import json
import time
import socket
import hashlib
from dataclasses import dataclass
from typing import Any, Dict, Iterable, Optional, Tuple, List
from urllib.parse import urlparse, urlunparse, parse_qsl, urlencode, quote, unquote

import requests
import yaml
from pymongo import MongoClient, ReturnDocument
from pymongo.collection import Collection


def now_ts() -> int:
    return int(time.time())


def sha256_text(s: str) -> str:
    h = hashlib.sha256()
    h.update(s.encode("utf-8", errors="replace"))
    return h.hexdigest()


def normalize_url(url: str) -> str:
    url = url.strip()
    if not url:
        return url
    p = urlparse(url)

    scheme = (p.scheme or "http").lower()
    netloc = p.netloc.lower()

    if not netloc and p.path.startswith(("http://", "https://")):
        p = urlparse(p.path)
        scheme = (p.scheme or "http").lower()
        netloc = p.netloc.lower()

    if ":" in netloc:
        host, port = netloc.rsplit(":", 1)
        if (scheme == "http" and port == "80") or (scheme == "https" and port == "443"):
            netloc = host

    path = p.path or "/"
    path = quote(unquote(path), safe="/:@-._~!$&'()*+,;=")

    q = parse_qsl(p.query, keep_blank_values=True)
    q.sort()
    query = urlencode(q, doseq=True)

    fragment = ""

    return urlunparse((scheme, netloc, path, p.params, query, fragment))


@dataclass
class Config:
    mongo_uri: str
    mongo_db: str
    mongo_collection: str

    delay_sec: float
    user_agent: str
    timeout_sec: float

    lock_ttl_sec: int
    refetch_after_sec: int
    retry_after_sec: int

    seed_metadata_paths: List[str]


def load_config(path: str) -> Config:
    with open(path, "r", encoding="utf-8") as f:
        raw = yaml.safe_load(f)

    db = raw.get("db", {})
    logic = raw.get("logic", {})
    seeds = raw.get("seeds", {})

    mongo_uri = db["mongo_uri"]
    mongo_db = db.get("mongo_db", "mai_ir")
    mongo_collection = db.get("mongo_collection", "pages")

    delay_sec = float(logic.get("delay_sec", 0.3))
    user_agent = str(logic.get("user_agent", "MAI-IR-Lab2/1.0 (+https://example.invalid)"))
    timeout_sec = float(logic.get("timeout_sec", 20))

    lock_ttl_sec = int(logic.get("lock_ttl_sec", 600))
    refetch_after_sec = int(logic.get("refetch_after_sec", 7 * 24 * 3600))
    retry_after_sec = int(logic.get("retry_after_sec", 15 * 60))

    seed_metadata_paths = list(seeds.get("metadata_paths", []) or [])

    return Config(
        mongo_uri=mongo_uri,
        mongo_db=mongo_db,
        mongo_collection=mongo_collection,
        delay_sec=delay_sec,
        user_agent=user_agent,
        timeout_sec=timeout_sec,
        lock_ttl_sec=lock_ttl_sec,
        refetch_after_sec=refetch_after_sec,
        retry_after_sec=retry_after_sec,
        seed_metadata_paths=seed_metadata_paths,
    )


def connect_pages(cfg: Config) -> Collection:
    client = MongoClient(cfg.mongo_uri)
    db = client[cfg.mongo_db]
    pages = db[cfg.mongo_collection]

    pages.create_index("norm_url", unique=True)
    pages.create_index("fetched_at")
    pages.create_index("next_refetch_at")
    pages.create_index("lock.until")

    return pages


def iter_metadata_records(path: str) -> Iterable[Dict[str, Any]]:
    with open(path, "r", encoding="utf-8") as f:
        data = json.load(f)
    if isinstance(data, list):
        for rec in data:
            if isinstance(rec, dict):
                yield rec
    elif isinstance(data, dict) and "items" in data and isinstance(data["items"], list):
        for rec in data["items"]:
            if isinstance(rec, dict):
                yield rec


def guess_source_name(metadata_path: str, rec: Dict[str, Any]) -> str:
    lang = rec.get("lang")
    if isinstance(lang, str) and lang:
        return lang.strip().lower()
    parts = os.path.normpath(metadata_path).split(os.sep)
    for p in reversed(parts):
        if p.lower() in ("enwiki", "ruwiki", "en", "ru"):
            return p.lower()
    return "unknown"


def import_seeds_if_needed(pages: Collection, metadata_paths: List[str]) -> int:
    if not metadata_paths:
        return 0

    inserted = 0
    for mp in metadata_paths:
        for rec in iter_metadata_records(mp):
            url = rec.get("url")
            if not isinstance(url, str) or not url.strip():
                continue
            norm = normalize_url(url)
            source = guess_source_name(mp, rec)
            meta = {
                "lang": rec.get("lang"),
                "docid": rec.get("docid"),
                "pageid": rec.get("pageid"),
                "title": rec.get("title"),
            }
            update = {
                "$setOnInsert": {
                    "norm_url": norm,
                    "url": norm,
                    "source": source,
                    "fetched_at": None,
                    "created_at": now_ts(),
                    "fail_count": 0,
                },
                "$set": {
                    "meta": meta,
                    "updated_at": now_ts(),
                },
            }
            res = pages.update_one({"norm_url": norm}, update, upsert=True)
            if res.upserted_id is not None:
                inserted += 1
    return inserted


def take_job(pages: Collection, cfg: Config, owner: str) -> Optional[Dict[str, Any]]:
    now = now_ts()
    lock_until = now + cfg.lock_ttl_sec

    lock_filter = {
        "$or": [
            {"lock.until": {"$exists": False}},
            {"lock.until": {"$lte": now}},
        ]
    }

    q_new = {
        "fetched_at": None,
        "$and": [lock_filter],
        "$or": [
            {"next_retry_at": {"$exists": False}},
            {"next_retry_at": {"$lte": now}},
        ],
    }
    upd = {"$set": {"lock": {"owner": owner, "until": lock_until, "taken_at": now}}}
    job = pages.find_one_and_update(q_new, upd, sort=[("created_at", 1)], return_document=ReturnDocument.AFTER)
    if job:
        job["_job_kind"] = "first_fetch"
        return job

    q_re = {
        "fetched_at": {"$ne": None},
        "next_refetch_at": {"$lte": now},
        "$and": [lock_filter],
        "$or": [
            {"next_retry_at": {"$exists": False}},
            {"next_retry_at": {"$lte": now}},
        ],
    }
    job = pages.find_one_and_update(q_re, upd, sort=[("next_refetch_at", 1)], return_document=ReturnDocument.AFTER)
    if job:
        job["_job_kind"] = "refetch"
        return job

    return None


def release_lock(pages: Collection, norm_url: str, owner: str) -> None:
    pages.update_one({"norm_url": norm_url, "lock.owner": owner}, {"$unset": {"lock": ""}})


def update_after_success(
    pages: Collection,
    cfg: Config,
    norm_url: str,
    owner: str,
    status_code: int,
    html: Optional[str],
    etag: Optional[str],
    last_modified: Optional[str],
    changed: Optional[bool],
) -> None:
    now = now_ts()
    base_set = {
        "status_code": status_code,
        "last_checked_at": now,
        "next_refetch_at": now + cfg.refetch_after_sec,
        "updated_at": now,
        "last_error": None,
        "next_retry_at": None,
    }
    upd: Dict[str, Any] = {"$set": base_set, "$unset": {"lock": ""}}
    if etag is not None:
        base_set["etag"] = etag
    if last_modified is not None:
        base_set["last_modified"] = last_modified
    if html is not None:
        base_set["html"] = html
        base_set["content_hash"] = sha256_text(html)
        base_set["fetched_at"] = now
        base_set["changed"] = bool(changed) if changed is not None else None
    pages.update_one({"norm_url": norm_url, "lock.owner": owner}, upd)


def update_after_error(pages: Collection, cfg: Config, norm_url: str, owner: str, err: str) -> None:
    now = now_ts()
    pages.update_one(
        {"norm_url": norm_url, "lock.owner": owner},
        {
            "$set": {
                "last_error": err[:500],
                "updated_at": now,
                "next_retry_at": now + cfg.retry_after_sec,
            },
            "$inc": {"fail_count": 1},
            "$unset": {"lock": ""},
        },
    )


def fetch_page(cfg: Config, job: Dict[str, Any]) -> Tuple[int, Optional[str], Optional[str], Optional[str], Optional[bool]]:
    url = job.get("url") or job.get("norm_url")
    if not isinstance(url, str) or not url:
        return (0, None, None, None, None)

    headers = {"User-Agent": cfg.user_agent}
    if job.get("_job_kind") == "refetch":
        if isinstance(job.get("etag"), str) and job["etag"]:
            headers["If-None-Match"] = job["etag"]
        if isinstance(job.get("last_modified"), str) and job["last_modified"]:
            headers["If-Modified-Since"] = job["last_modified"]

    r = requests.get(url, headers=headers, timeout=cfg.timeout_sec, allow_redirects=True)
    status = int(r.status_code)

    etag = r.headers.get("ETag")
    last_mod = r.headers.get("Last-Modified")

    if status == 304:
        return (status, None, etag, last_mod, False)

    html = r.text

    old_hash = job.get("content_hash")
    new_hash = sha256_text(html)
    changed = None
    if isinstance(old_hash, str) and old_hash:
        changed = (new_hash != old_hash)

    return (status, html, etag, last_mod, changed)


def main() -> int:
    if len(sys.argv) != 2:
        print("Usage: python robot.py path/to/config.yaml", file=sys.stderr)
        return 2

    cfg_path = sys.argv[1]
    cfg = load_config(cfg_path)
    pages = connect_pages(cfg)

    owner = f"{socket.gethostname()}:{os.getpid()}"
    print(f"[robot] owner={owner}")
    inserted = import_seeds_if_needed(pages, cfg.seed_metadata_paths)
    if inserted:
        print(f"[robot] imported new seeds: {inserted}")

    print("[robot] start crawling; stop with Ctrl+C")

    processed = 0
    fetched = 0
    unchanged_304 = 0
    changed_cnt = 0
    errors = 0

    try:
        while True:
            job = take_job(pages, cfg, owner)
            if not job:
                time.sleep(min(5.0, max(0.5, cfg.delay_sec)))
                continue

            norm_url = job["norm_url"]
            kind = job.get("_job_kind")
            processed += 1

            try:
                status, html, etag, last_mod, changed = fetch_page(cfg, job)
                if status == 304:
                    unchanged_304 += 1
                    update_after_success(pages, cfg, norm_url, owner, status, None, etag, last_mod, False)
                elif status >= 200 and status < 400:
                    store_html = html
                    if kind == "refetch" and changed is False:
                        store_html = None
                    if kind == "refetch" and changed is True:
                        changed_cnt += 1
                    if kind == "first_fetch":
                        fetched += 1
                    update_after_success(pages, cfg, norm_url, owner, status, store_html, etag, last_mod, changed)
                else:
                    errors += 1
                    update_after_error(pages, cfg, norm_url, owner, f"HTTP {status}")
            except Exception as e:
                errors += 1
                update_after_error(pages, cfg, norm_url, owner, repr(e))

            time.sleep(max(0.0, cfg.delay_sec))

            if processed % 50 == 0:
                print(f"[robot] processed={processed} fetched={fetched} changed={changed_cnt} 304={unchanged_304} errors={errors}")

    except KeyboardInterrupt:
        print("\n[robot] stopped by user (Ctrl+C)")

    print(f"[robot] final: processed={processed} fetched={fetched} changed={changed_cnt} 304={unchanged_304} errors={errors}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
