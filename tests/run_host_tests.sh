#!/usr/bin/env bash
# Platform-independent point-to-point simulation tests.
# They compile the exact game core + JJ1 runtime used by the ROM together
# with the converted original level data and verify geometry and physics.
set -euo pipefail
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
CC_BIN="${CC:-cc}"
mkdir -p "$ROOT/tests/.tmp"
"$CC_BIN" -std=c99 -Wall -Wextra -Werror -DJAZZ_HOST -DJAZZ_JJ1_RUNTIME \
  -I"$ROOT/inc" -I"$ROOT/src" -I"$ROOT/packs/ep1" \
  "$ROOT/src/game_core.c" "$ROOT/src/jj1_runtime.c" "$ROOT/src/jj1_eventset.c" \
  "$ROOT/tests/host_data.c" "$ROOT/tests/test_game_core.c" \
  -o "$ROOT/tests/.tmp/game_core_tests"
"$ROOT/tests/.tmp/game_core_tests"

# Deliberately compiled WITHOUT -DJAZZ_JJ1_RUNTIME: a build that misses the
# flag must still select the JJ1 runtime, not the legacy prototype stage.
"$CC_BIN" -std=c99 -Wall -Wextra -Werror -DJAZZ_HOST \
  -I"$ROOT/inc" -I"$ROOT/src" -I"$ROOT/packs/ep1" \
  "$ROOT/src/game_core.c" "$ROOT/src/jj1_runtime.c" "$ROOT/src/jj1_eventset.c" \
  "$ROOT/tests/host_data.c" "$ROOT/tests/test_default_runtime.c" \
  -o "$ROOT/tests/.tmp/default_runtime_test"
"$ROOT/tests/.tmp/default_runtime_test"

# Verifies the event -> animation -> sprite chain used to extract enemy art,
# against synthetic files in the original formats (no game data needed).
python3 "$ROOT/tests/test_sprite_extraction.py"

# Bonus-level simulation against the converted bonus data.
"$CC_BIN" -std=c99 -Wall -Wextra -Werror -DJAZZ_HOST -I"$ROOT/inc" -I"$ROOT/src" -I"$ROOT/packs/ep1" \
  "$ROOT/src/bonus_core.c" "$ROOT/tests/test_bonus.c" -o "$ROOT/tests/.tmp/bonus_tests"
"$ROOT/tests/.tmp/bonus_tests" "$ROOT/res/bonus" "$ROOT/packs/ep1"

# The bonus diamond -> bonus level hand-off in the game core.
"$CC_BIN" -std=c99 -Wall -Wextra -Werror -DJAZZ_HOST -DJAZZ_JJ1_RUNTIME \
  -I"$ROOT/inc" -I"$ROOT/src" -I"$ROOT/packs/ep1" \
  "$ROOT/src/game_core.c" "$ROOT/src/jj1_runtime.c" "$ROOT/src/jj1_eventset.c" \
  "$ROOT/tests/host_data.c" "$ROOT/tests/test_bonus_link.c" \
  -o "$ROOT/tests/.tmp/bonus_link_test"
"$ROOT/tests/.tmp/bonus_link_test"

# Secret button sequence, overlap recovery and rejected simultaneous inputs.
"$CC_BIN" -std=c99 -Wall -Wextra -Werror -I"$ROOT/inc" \
  "$ROOT/tests/test_stage_cheat.c" -o "$ROOT/tests/.tmp/stage_cheat_test"
"$ROOT/tests/.tmp/stage_cheat_test"
python3 "$ROOT/tests/test_bonus_sprite_decode.py"
python3 "$ROOT/tests/test_scene_decode.py"

# Weapon IDs, declared XGM resources, and original PCM/data when supplied.
"$CC_BIN" -std=c99 -Wall -Wextra -Werror -I"$ROOT/inc" \
  "$ROOT/tests/test_shot_sfx.c" -o "$ROOT/tests/.tmp/shot_sfx_test"
"$ROOT/tests/.tmp/shot_sfx_test"
python3 "$ROOT/tests/test_shot_sounds.py"

