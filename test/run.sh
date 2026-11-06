#!/usr/bin/env bash
# Host tests for the template's app logic (no device, no ESP-IDF needed).
set -euo pipefail
cd "$(dirname "$0")"
OUT=$(mktemp -d)
cc -std=c11 -Wall -Wextra -Werror -O1 -o "$OUT/test_counter" test_counter.c ../main/counter.c
"$OUT/test_counter"
rm -rf "$OUT"
