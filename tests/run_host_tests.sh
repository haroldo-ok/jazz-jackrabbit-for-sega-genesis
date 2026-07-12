#!/usr/bin/env bash
# Platform-independent point-to-point simulation tests.
set -euo pipefail
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
CC_BIN="${CC:-cc}"
mkdir -p "$ROOT/tests/.tmp"
"$CC_BIN" -std=c99 -Wall -Wextra -Werror -DJAZZ_HOST -I"$ROOT/inc" \
  "$ROOT/src/game_core.c" "$ROOT/tests/test_game_core.c" \
  -o "$ROOT/tests/.tmp/game_core_tests"
"$ROOT/tests/.tmp/game_core_tests"
