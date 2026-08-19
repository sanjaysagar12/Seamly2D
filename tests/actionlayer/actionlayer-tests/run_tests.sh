#!/usr/bin/env bash
# Drives seamly2d-actiond (TARGET "actiond" in src/app/actiond/actiond.pro) over every fixture in
# fixtures/, one process per fixture.
#
# actiond's real CLI contract (src/app/actiond/main.cpp) is:
#   actiond --pattern <path.val> --measurements <path.smis|.smms> --actions <path.json>
#           [--save-pattern <path.val>]
# It takes NO stdin and has no --output-dir flag; it prints one compact JSON line to stdout
# ({"results": [...]}) and, on a load/parse failure, one compact JSON line to stderr
# ({"error": "..."}). A fixture's own "measurements"/"id" JSON fields are NOT read by actiond
# itself (ActionHost::runActions() only reads the "actions" array) -- they exist only so this
# script can pick the right --measurements file per fixture; the earlier draft of this script
# assumed a stdin/--output-dir daemon protocol that Phase 2/6 never implemented, so it has been
# rewritten to match the actual contract above.
#
# Any "render.snapshot"/"session.save" action's own "path" field (e.g. "snapshot.png") is
# resolved relative to actiond's current working directory (see render_handlers.cpp's QFileInfo
# usage), so this script cd's into each fixture's own output/<name>/ directory before invoking
# actiond, rather than passing an --output-dir flag that does not exist.
#
# "session.save" is NOT a registered JSON action op (Phase 6 implements the six formula-point
# tools only; see fixtures/09_integration_multi_tool.json's own trailing action) -- it is a
# leftover from an earlier draft of this fixture that assumed a JSON-level save action. Left as
# written (per this task's instruction not to invent different fixture content), it produces one
# ordinary {"ok": false, "error": "Unknown action op: 'session.save'"} entry inside "results"
# (ActionEngine::run() isolates one unknown op to a single failed result, not a whole-batch
# abort -- see action_engine.cpp), which does not fail the run. The pattern.val a caller would
# have expected from that action is instead produced by unconditionally passing --save-pattern
# for every fixture below, which is the real (host-level, not per-action) way actiond persists a
# script's mutations to disk.
#
# Usage:
#   ./run_tests.sh                          # run every fixture
#   ./run_tests.sh 03_endline_with_measurement_formula.json   # run just one
#   UPDATE_EXPECTED=1 ./run_tests.sh        # (re)generate expected/ from output/
#
set -uo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
FIXTURES_DIR="$SCRIPT_DIR/fixtures"
OUTPUT_DIR="$SCRIPT_DIR/output"
EXPECTED_DIR="$SCRIPT_DIR/expected"
MEASUREMENTS_DIR="$SCRIPT_DIR/measurements"

# Reused verbatim from the Phase 5 actionlayer test fixtures: a minimal-but-schema-valid .val
# (one seed draftBlock named "_seed", which none of these fixtures' own basePoint names collide
# with) and a measurements file with no individual measurements defined, for fixtures that don't
# need any.
PATTERN_FILE="$SCRIPT_DIR/../fixtures/empty_pattern.val"
DEFAULT_MEASUREMENTS_FILE="$SCRIPT_DIR/../fixtures/empty_measurements.smis"

# Path to the built daemon binary. actiond.pro's TARGET is "actiond" (DESTDIR "bin"), not literally
# named "seamly2d-actiond" -- adjust ACTIOND_BIN if your build directory differs from ../../../build.
ACTIOND="${ACTIOND_BIN:-$SCRIPT_DIR/../../../build/src/app/actiond/bin/actiond.exe}"

if [[ ! -x "$ACTIOND" ]]; then
  echo "actiond binary not found or not executable at: $ACTIOND"
  echo "Set ACTIOND_BIN=/path/to/actiond(.exe) or build the actiond target first."
  exit 1
fi

if [[ ! -f "$PATTERN_FILE" ]]; then
  echo "Seed pattern file not found: $PATTERN_FILE"
  exit 1
fi

pass=0
fail=0

# Extracts a top-level "measurements": "..." string field from a fixture JSON without depending
# on jq (not guaranteed present in every dev/CI environment this script runs in). Fixtures only
# ever use this field as a flat string, so a single-line regex is sufficient; anything fancier
# belongs in a real JSON parse, not a bash test runner.
extract_measurements_path() {
  local fixture_path="$1"
  grep -o '"measurements"[[:space:]]*:[[:space:]]*"[^"]*"' "$fixture_path" \
    | head -n1 \
    | sed -E 's/.*:[[:space:]]*"([^"]*)"/\1/'
}

# Prints a response.json's content with every "objects"/"history" array (order-unstable, per the
# comment at this function's call site) sorted by "id", and every object's keys sorted, so two
# runs that differ only in QHash iteration order compare equal. Prints nothing (and lets the
# caller's diff report a mismatch) if the file isn't valid JSON in the expected shape.
normalize_response_json() {
  python3 -c '
import json, sys

def normalize(value):
    if isinstance(value, list):
        items = [normalize(v) for v in value]
        if items and all(isinstance(v, dict) and "id" in v for v in items):
            items.sort(key=lambda v: v["id"])
        return items
    if isinstance(value, dict):
        return {k: normalize(v) for k, v in value.items()}
    return value

with open(sys.argv[1], "r", encoding="utf-8") as fh:
    data = json.load(fh)
print(json.dumps(normalize(data), sort_keys=True, indent=1))
' "$1" 2>/dev/null
}

run_one() {
  local fixture_path="$1"
  local name
  name="$(basename "$fixture_path" .json)"
  local out_dir="$OUTPUT_DIR/$name"
  mkdir -p "$out_dir"

  echo "=== $name ==="

  local rel_measurements
  rel_measurements="$(extract_measurements_path "$fixture_path")"
  local measurements_file
  if [[ -n "$rel_measurements" ]]; then
    # The fixture's "measurements" path is relative to fixtures/ itself (e.g.
    # "../measurements/male_shirt.smis"), matching how every fixture in this repo already writes it.
    measurements_file="$(cd "$FIXTURES_DIR" && cd "$(dirname "$rel_measurements")" && pwd)/$(basename "$rel_measurements")"
  else
    measurements_file="$DEFAULT_MEASUREMENTS_FILE"
  fi
  if [[ ! -f "$measurements_file" ]]; then
    echo "FAIL (measurements file not found: $measurements_file)"
    fail=$((fail + 1))
    return
  fi

  # cd into out_dir first: every relative "path" a fixture's render.snapshot/session.save action
  # names (e.g. "snapshot.png") resolves against actiond's own current working directory, and
  # --save-pattern below is given as an absolute path so it lands here regardless.
  ( cd "$out_dir" && QT_QPA_PLATFORM=offscreen "$ACTIOND" \
      --pattern "$PATTERN_FILE" \
      --measurements "$measurements_file" \
      --actions "$fixture_path" \
      --save-pattern "$out_dir/pattern.val" \
      > "$out_dir/response.json" \
      2> "$out_dir/stderr.log" )
  local exit_code=$?

  if [[ "$name" == 08_error_* ]]; then
    # Error fixtures: success = a structured JSON error for the bad-formula action inside
    # response.json's "results" array (actiond itself still exits 0 -- the failure is isolated to
    # one action's result, not a load/parse failure -- see action_engine.cpp/main.cpp), and no
    # hang/crash, which the fact this function returned at all already proves.
    if grep -q '"error"' "$out_dir/response.json" 2>/dev/null; then
      echo "PASS (structured error returned, as expected)"
      pass=$((pass + 1))
    else
      echo "FAIL (expected a structured JSON error in response.json, got none)"
      fail=$((fail + 1))
    fi

    # Per fixture 08's own "_comment": confirm the daemon-hosting *process model* isn't corrupted
    # by a bad formula, by running fixture 02 in a brand-new process right afterward (actiond is
    # one-shot per invocation in this phase, so "fresh process" is simply "run it again").
    local followup_dir="$OUTPUT_DIR/${name}_followup_02"
    mkdir -p "$followup_dir"
    ( cd "$followup_dir" && QT_QPA_PLATFORM=offscreen "$ACTIOND" \
        --pattern "$PATTERN_FILE" \
        --measurements "$DEFAULT_MEASUREMENTS_FILE" \
        --actions "$FIXTURES_DIR/02_endline_basic.json" \
        > "$followup_dir/response.json" \
        2> "$followup_dir/stderr.log" )
    if [[ $? -eq 0 ]] && grep -q '"ok":true' "$followup_dir/response.json" 2>/dev/null; then
      echo "  follow-up: fixture 02 still succeeds in a fresh process after the bad-formula run"
    else
      echo "  FAIL: fixture 02 did not succeed in a fresh process after the bad-formula run -- see $followup_dir"
      fail=$((fail + 1))
    fi
    return
  fi

  if [[ $exit_code -ne 0 ]]; then
    echo "FAIL (actiond exited $exit_code) -- see $out_dir/stderr.log"
    fail=$((fail + 1))
    return
  fi

  if [[ "${UPDATE_EXPECTED:-0}" == "1" ]]; then
    mkdir -p "$EXPECTED_DIR/$name"
    cp -f "$out_dir"/* "$EXPECTED_DIR/$name/" 2>/dev/null
    echo "UPDATED expected/$name"
    pass=$((pass + 1))
    return
  fi

  if [[ -d "$EXPECTED_DIR/$name" ]]; then
    local diff_failed=0
    for f in "$EXPECTED_DIR/$name"/*; do
      local base
      base="$(basename "$f")"
      if [[ "$base" == "stderr.log" ]]; then continue; fi
      if [[ "$base" == *.png ]]; then
        # Images: exact-byte diff here as a baseline; swap for a perceptual diff
        # (e.g. a small ImageMagick `compare` call) once renders are non-deterministic
        # in trivial ways (timestamps in metadata, AA differences across Qt versions).
        cmp -s "$f" "$out_dir/$base" || { echo "  MISMATCH: $base"; diff_failed=1; }
      elif [[ "$base" == "response.json" ]] && command -v python3 >/dev/null 2>&1; then
        # pattern.dump's "objects"/"history" arrays come from VContainer::DataGObjects(), a
        # QHash -- Qt randomizes QHash iteration order per-process (hash-flooding protection), so
        # two otherwise-identical runs can legitimately emit the same objects in a different
        # array order. A byte-exact diff would treat that as a mismatch, so both files are
        # normalized (arrays holding "id"-keyed objects sorted by id, all object keys sorted) with
        # the same small Python snippet before comparing. Falls back to a raw diff below if
        # python3 isn't on PATH.
        diff -q <(normalize_response_json "$f") <(normalize_response_json "$out_dir/$base") >/dev/null \
          || { echo "  MISMATCH: $base"; diff_failed=1; }
      else
        diff -q "$f" "$out_dir/$base" >/dev/null || { echo "  MISMATCH: $base"; diff_failed=1; }
      fi
    done
    if [[ $diff_failed -eq 0 ]]; then
      echo "PASS"
      pass=$((pass + 1))
    else
      echo "FAIL (see MISMATCH lines above)"
      fail=$((fail + 1))
    fi
  else
    echo "NO EXPECTED DATA YET -- run with UPDATE_EXPECTED=1 after manually verifying output/$name/"
  fi
}

if [[ $# -eq 1 ]]; then
  run_one "$FIXTURES_DIR/$1"
else
  for f in "$FIXTURES_DIR"/*.json; do
    run_one "$f"
  done
fi

echo
echo "passed: $pass   failed: $fail"
[[ $fail -eq 0 ]]
