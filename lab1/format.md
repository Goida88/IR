# Формат корпуса

Структура:
- enwiki/raw/*.wiki — сырой документ (wikitext, вики-разметка)
- enwiki/text/*.txt — выделенный текст (UTF-8)
- enwiki/metadata.json — метаданные
Аналогично для ruwiki.

Доп. мета-информация:
- metadata.json: docid, title, pageid, url, words, пути к raw/text
- в начале каждого text-файла: Title, PageID, URL, LengthBytes, Words
