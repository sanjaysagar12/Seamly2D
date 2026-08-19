#!/usr/bin/env bash
# run_action_tests.sh -- Phase 5 (mutating actions) test runner.
#
# Runs every JSON script under scripts/ through actiond (the headless action-daemon CLI already
# built in Phase 3/4: src/app/actiond), against a fresh copy of fixtures/empty_pattern.val each
# time. actiond is the one existing entry point that can invoke ActionEngine against a real .val
# file and (as of Phase 5's --save-pattern option) persist the result, so this script reuses it
# rather than building a second harness.
#
# For a script with a matching expected/<name>.json, the actual JSON response is diffed against
# it (structurally, via jq/python -- not raw text, so key order never causes a false failure) and
# PASS/FAIL is reported. Scripts without an expected file (001, 004) just have their response
# printed for manual inspection, exactly as the task asks.
#
# Each run's mutated pattern is saved to output/<script-name>.val for inspection. No state leaks
# between runs: every invocation reads the SAME fixtures/empty_pattern.val, and actiond only ever
# writes to --save-pattern's path -- it never mutates its --pattern input -- so "starting fresh
# each time" is automatic, not something this script has to arrange.
set -u

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
FIXTURE_PATTERN="$SCRIPT_DIR/fixtures/empty_pattern.val"
FIXTURE_MEASUREMENTS="$SCRIPT_DIR/fixtures/empty_measurements.smis"
SCRIPTS_DIR="$SCRIPT_DIR/scripts"
EXPECTED_DIR="$SCRIPT_DIR/expected"
OUTPUT_DIR="$SCRIPT_DIR/output"

# ACTIOND_EXE can be overridden by the caller (e.g. a CI job with a different build layout); this
# default matches where build_actiond.bat / the qmake out-of-source tree puts it.
ACTIOND_EXE="${ACTIOND_EXE:-$SCRIPT_DIR/../../out/src/app/actiond/bin/actiond.exe}"

if [ ! -x "$ACTIOND_EXE" ]; then
    echo "ERROR: actiond executable not found or not executable at: $ACTIOND_EXE" >&2
    echo "Build it first (see build_actiond.bat), or set ACTIOND_EXE to its path." >&2
    exit 2
fi

mkdir -p "$OUTPUT_DIR"

# Structurally compares two JSON files, ignoring key order, using whichever JSON tool is
# available on this machine. Prints nothing; signals the result via exit code (0 = equal).
compare_json() {
    local actual_file="$1"
    local expected_file="$2"
    if command -v jq >/dev/null 2>&1; then
        diff <(jq -S . "$actual_file") <(jq -S . "$expected_file") >/dev/null 2>&1
        return $?
    elif command -v python3 >/dev/null 2>&1; then
        python3 - "$actual_file" "$expected_file" <<'PYEOF'
import json, sys
with open(sys.argv[1]) as f: a = json.load(f)
with open(sys.argv[2]) as f: b = json.load(f)
sys.exit(0 if a == b else 1)
PYEOF
        return $?
    else
        # Last resort: exact text compare. Only reliable if the actual response happens to use
        # the same key order/whitespace as the expected file (QJsonDocument::Compact's own key
        # order, which is what actiond prints); noted so a failure here isn't mistaken for a bug.
        echo "  (no jq or python3 found -- falling back to exact text compare)"
        diff -q "$actual_file" "$expected_file" >/dev/null 2>&1
        return $?
    fi
}

overall_status=0 # 0 = every script produced a handled result (crash-free); set to 1 on any crash or FAIL.

for script_path in "$SCRIPTS_DIR"/*.json; do
    name="$(basename "$script_path" .json)"
    output_pattern="$OUTPUT_DIR/${name}.val"
    actual_json="$OUTPUT_DIR/${name}.actual.json"

    echo "=== $name ==="

    "$ACTIOND_EXE" --pattern "$FIXTURE_PATTERN" \
                    --measurements "$FIXTURE_MEASUREMENTS" \
                    --actions "$script_path" \
                    --save-pattern "$output_pattern" \
                    > "$actual_json" 2> "$OUTPUT_DIR/${name}.stderr.log"
    exit_code=$?

    # actiond's own exit codes: 0 = ran to completion (even if individual actions in the batch
    # report ok:false -- that is a handled, structured error, not a crash); 1 = a VException/
    # std::exception was caught in main() and reported as JSON on stderr (also handled); 2 = a
    # command-line usage error. Anything else -- in particular the >=128 codes this shell reports
    # for a process killed by a signal (segfault, etc., see EXIT=139 during Phase 5 development)
    # -- means a C++ exception or fault actually escaped the process uncaught.
    if [ "$exit_code" -gt 2 ]; then
        echo "  CRASHED (unhandled failure, exit code $exit_code) -- see $OUTPUT_DIR/${name}.stderr.log"
        overall_status=1
        continue
    fi

    expected_file="$EXPECTED_DIR/${name}.json"
    if [ -f "$expected_file" ]; then
        if compare_json "$actual_json" "$expected_file"; then
            echo "  PASS"
        else
            echo "  FAIL (actual response did not match $expected_file)"
            echo "  actual:   $(cat "$actual_json")"
            echo "  expected: $(cat "$expected_file")"
            overall_status=1
        fi
    else
        echo "  (no expected/${name}.json -- printing response for manual inspection)"
        cat "$actual_json"
        echo
    fi

    echo "  saved pattern: $output_pattern"
done

echo
if [ "$overall_status" -eq 0 ]; then
    echo "All scripts ran without crashing (and every scripted comparison passed)."
else
    echo "At least one script crashed or failed its comparison -- see above."
fi
exit "$overall_status"
