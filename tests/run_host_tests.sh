#!/usr/bin/env bash
# Platform-independent point-to-point simulation tests.
# They compile the exact game core + JJ1 runtime used by the ROM together
# with the converted original level data and verify geometry and physics.
set -euo pipefail
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
CC_BIN="${CC:-cc}"
mkdir -p "$ROOT/tests/.tmp"
"$CC_BIN" -std=c99 -Wall -Wextra -Werror -DJAZZ_HOST -DJAZZ_JJ1_RUNTIME \
  -I"$ROOT/inc" -I"$ROOT/src" \
  "$ROOT/src/game_core.c" "$ROOT/src/jj1_runtime.c" "$ROOT/src/jj1_eventset.c" \
  "$ROOT/tests/host_data.c" "$ROOT/tests/test_game_core.c" \
  -o "$ROOT/tests/.tmp/game_core_tests"
"$ROOT/tests/.tmp/game_core_tests"
