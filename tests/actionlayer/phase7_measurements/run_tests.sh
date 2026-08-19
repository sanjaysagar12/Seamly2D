#!/usr/bin/env bash
# Drives actiond (src/app/actiond/actiond.pro's TARGET) over every multi-action batch in
# cases/*.json, each run against the same fixtures/base_pattern.val + fixtures/measurements_a.smis
# starting state, and checks the saved response against check_output.py's per-case assertions.
#
# CLI contract (src/app/actiond/main.cpp): --pattern/--measurements/--actions are file paths (no
# stdin), --save-pattern is optional; actiond prints one compact JSON line to stdout. Matches
# tests/actionlayer/actionlayer-tests/run_tests.sh's own invocation shape (Phase 6) exactly.
#
# Usage:
#   ./run_tests.sh                          # run every case
#   ./run_tests.sh 05_missing_measurement_error   # run just one
#
set -uo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
CASES_DIR="$SCRIPT_DIR/cases"
OUTPUT_DIR="$SCRIPT_DIR/output"
FIXTURES_DIR="$SCRIPT_DIR/fixtures"
CHECK_SCRIPT="$SCRIPT_DIR/check_output.py"

PATTERN_FILE="$FIXTURES_DIR/base_pattern.val"                 # Baked with measurements_a.smis; see fixtures generation note in README.md.
INITIAL_MEASUREMENTS_FILE="$FIXTURES_DIR/measurements_a.smis" # Matches the file base_pattern.val was baked with, so its baseline geometry is exactly what each case's first dump should show.

# Path to the built daemon binary. actiond.pro's TARGET is "actiond" (DESTDIR "bin") --
# same override convention as tests/actionlayer/actionlayer-tests/run_tests.sh (Phase 6).
ACTIOND="${ACTIOND_BIN:-$SCRIPT_DIR/../../../build/src/app/actiond/bin/actiond.exe}"

if [[ ! -x "$ACTIOND" ]]; then
  echo "actiond binary not found or not executable at: $ACTIOND"
  echo "Set ACTIOND_BIN=/path/to/actiond(.exe) or build the actiond target first."
  exit 1
fi

if [[ ! -f "$PATTERN_FILE" ]]; then
  echo "base_pattern.val not found at: $PATTERN_FILE"
  echo "See README.md's \"Regenerating fixtures/base_pattern.val\" section to rebuild it."
  exit 1
fi

if ! command -v python3 >/dev/null 2>&1; then
  echo "python3 not found on PATH -- required by check_output.py to assert on each case's output."
  exit 1
fi

mkdir -p "$OUTPUT_DIR"

pass=0
fail=0

run_one() {
  local case_path="$1"
  local name
  name="$(basename "$case_path" .json)"
  local out_json="$OUTPUT_DIR/$name.out.json"
  local err_log="$OUTPUT_DIR/$name.stderr.log"
  local saved_pattern="$OUTPUT_DIR/$name.pattern.val"

  echo "=== $name ==="

  QT_QPA_PLATFORM=offscreen "$ACTIOND" \
    --pattern "$PATTERN_FILE" \
    --measurements "$INITIAL_MEASUREMENTS_FILE" \
    --actions "$case_path" \
    --save-pattern "$saved_pattern" \
    > "$out_json" \
    2> "$err_log"
  local exit_code=$?

  if [[ $exit_code -ne 0 ]]; then
    echo "FAIL (actiond exited $exit_code, see $err_log)"
    fail=$((fail + 1))
    return
  fi

  # check_output.py itself prints "PASS" or "FAIL: <reason>"; just relay it and track the count.
  if python3 "$CHECK_SCRIPT" "$name" "$out_json"; then
    pass=$((pass + 1))
  else
    fail=$((fail + 1))
  fi
}

if [[ $# -eq 1 ]]; then
  run_one "$CASES_DIR/$1.json"
else
  for f in "$CASES_DIR"/*.json; do
    run_one "$f"
  done
fi

echo
echo "passed: $pass   failed: $fail"
[[ $fail -eq 0 ]]
