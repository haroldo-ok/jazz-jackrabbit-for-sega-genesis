#!/usr/bin/env bash
set -euo pipefail
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
"$ROOT/tests/run_host_tests.sh"
"$ROOT/tests/run_emulator_test.sh"
