#!/usr/bin/env bash
set -e
g++ -O2 -std=c++17 bool_index.cpp -o bool_index
./bool_index build --corpus /content/corpus --out /content/bool_index_test --limit 200
./bool_index lookup --index /content/bool_index_test --term neuroscience | head
