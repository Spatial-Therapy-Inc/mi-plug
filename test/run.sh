#!/usr/bin/env bash
# Host tests for mi-plug's logic (no device, no ESP-IDF needed).
set -euo pipefail
cd "$(dirname "$0")"
OUT=$(mktemp -d)
cc -std=c11 -Wall -Wextra -Werror -O1 -o "$OUT/test_plug" test_plug.c ../main/mp_crypto.c ../main/miio_proto.c \
  ../main/mjson.c ../main/plug_logic.c
"$OUT/test_plug"
rm -rf "$OUT"
