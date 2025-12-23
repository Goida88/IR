
.PHONY: run stats clean test

run:
	python crawler_project/crawler.py crawler_project/config.yaml

stats:
	python -c "import yaml,pymongo;cfg=yaml.safe_load(open('crawler_project/config.yaml'));db=pymongo.MongoClient(cfg['db']['uri'])[cfg['db']['name']]['documents'];print(f'Documents: {db.count_documents({})}')"

clean:
	rm -rf __pycache__ *.pyc crawler_project/__pycache__ crawler_project/*.pyc

test:
	@echo "Тест: robot works - $$(make stats)"
