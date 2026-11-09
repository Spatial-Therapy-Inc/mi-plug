#!/usr/bin/env bash
# Host tests for home-switch's logic (no device, no ESP-IDF needed).
set -euo pipefail
cd "$(dirname "$0")"
OUT=$(mktemp -d)
cc -std=c11 -Wall -Wextra -Werror -O1 -o "$OUT/test_plug" test_plug.c ../main/mp_crypto.c ../main/miio_proto.c \
  ../main/mjson.c ../main/plug_logic.c
"$OUT/test_plug"
rm -rf "$OUT"

# The config page's import (main/page.html) against an extractor -o file, when Node.js is there.
if command -v node >/dev/null; then
  node test_page.mjs
else
  echo "config page check skipped: no node"
fi
