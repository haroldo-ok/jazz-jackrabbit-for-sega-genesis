#!/usr/bin/env bash
# Full point-to-point suite: host simulation tests against the converted
# original data, the on-target 68000 self-test, and a scripted headless
# emulator playthrough.  BlastEm remains available as an optional X-based
# variant via tests/run_emulator_test.sh.
set -euo pipefail
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
"$ROOT/tests/run_host_tests.sh"
"$ROOT/tests/run_headless_emulator_test.sh"
