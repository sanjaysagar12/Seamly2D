#!/usr/bin/env python3
"""run_batch.py -- Phase 8 tests/actionlayer/cases/ harness.

Runs one actions.json file through actiond (the headless action-daemon CLI, src/app/actiond),
against a given --pattern/--measurements pair, and collects the result.

actiond's real CLI contract (see src/app/actiond/main.cpp) is a single-shot process:

    actiond --pattern <path.val> --measurements <path.smis|.smms|.vst>
            --actions <path.json> [--save-pattern <path.val>]

There is no NDJSON/stdin protocol -- the whole actions script is one JSON file passed via
--actions, and the whole result is one JSON object printed to stdout on success (or a single
{"error": "..."} line on stderr if the process itself failed to start/parse/load, exit code 1;
exit code 2 is a command-line usage error). This script drives that exact contract.

Usage:
    python3 run_batch.py <actions.json> <output-dir>
        [--pattern PATTERN_VAL] [--measurements MEASUREMENTS_FILE]
        [--save-pattern NAME] [--actiond PATH]

Defaults (relative to this script's own directory, i.e. tests/actionlayer/):
    --pattern       fixtures/empty_pattern.val
    --measurements  fixtures/base_measurements.smis
    --save-pattern  pattern.val   (written inside <output-dir>)
    --actiond       ../../out/src/app/actiond/bin/actiond.exe   (override with $ACTIOND_EXE too)

Any "path" field inside an action (e.g. render.snapshot's "path": "square.png") that is NOT
already absolute is resolved relative to <output-dir> -- actiond itself is invoked with its
working directory set to <output-dir>, so those files simply land there directly; there is
nothing to separately "copy" after the fact.

Output, always written to <output-dir>:
    responses.json  -- actiond's raw JSON response (the {"results": [...]} object), or
                        {"error": "..."} if actiond itself failed to run the batch at all.
    stderr.log      -- actiond's raw stderr (logs/warnings only; never response data).
    <save-pattern>  -- the (possibly mutated) pattern actiond saved, per --save-pattern.
    plus whatever render.snapshot/etc. paths the actions.json itself named (see above).

Exit codes:
    0  -- actiond ran to completion AND every action in the batch reported "ok": true.
    1  -- actiond ran to completion but at least one action reported "ok": false; every
          failing action/index/op/error is printed to stderr before exiting.
    2  -- actiond itself could not be found, or failed to run the batch at all (crashed,
          exited non-zero, or produced unparsable stdout); the failure is printed to stderr.

stdout carries only the pass/fail summary a human or CI log wants to see; stderr carries actiond's
own log noise (via stderr.log) plus this script's own diagnostic messages -- kept separate so a
caller piping stdout alone gets a clean summary.
"""

import argparse
import json
import os
import subprocess
import sys


def default_actiond_path(script_dir):
    override = os.environ.get("ACTIOND_EXE")
    if override:
        return override
    return os.path.normpath(os.path.join(script_dir, "..", "..", "out", "src", "app", "actiond", "bin", "actiond.exe"))


def main():
    script_dir = os.path.dirname(os.path.abspath(__file__))

    parser = argparse.ArgumentParser(description="Run one actions.json file through actiond and report pass/fail.")
    parser.add_argument("actions_file", help="Path to the actions.json file to run.")
    parser.add_argument("output_dir", help="Directory to write responses.json/stderr.log/saved pattern/renders into.")
    parser.add_argument("--pattern", default=os.path.join(script_dir, "fixtures", "empty_pattern.val"),
                         help="Pattern (.val) file to load. Default: fixtures/empty_pattern.val")
    parser.add_argument("--measurements", default=os.path.join(script_dir, "fixtures", "base_measurements.smis"),
                         help="Measurements file to load. Default: fixtures/base_measurements.smis")
    parser.add_argument("--save-pattern", dest="save_pattern", default="pattern.val",
                         help="Filename (relative to output-dir, unless absolute) to save the resulting pattern to. Default: pattern.val")
    parser.add_argument("--actiond", default=None,
                         help="Path to actiond.exe. Default: $ACTIOND_EXE, else ../../out/src/app/actiond/bin/actiond.exe")
    args = parser.parse_args()

    actiond_exe = args.actiond or default_actiond_path(script_dir)
    actions_file = os.path.abspath(args.actions_file)
    pattern_file = os.path.abspath(args.pattern)
    measurements_file = os.path.abspath(args.measurements)
    output_dir = os.path.abspath(args.output_dir)
    save_pattern_path = args.save_pattern if os.path.isabs(args.save_pattern) else os.path.join(output_dir, args.save_pattern)

    if not os.path.isfile(actiond_exe):
        print("ERROR: actiond executable not found at: %s" % actiond_exe, file=sys.stderr)
        print("Build it first (see build_actiond.bat at the repo root), or set --actiond / $ACTIOND_EXE.", file=sys.stderr)
        return 2
    if not os.path.isfile(actions_file):
        print("ERROR: actions file not found: %s" % actions_file, file=sys.stderr)
        return 2
    if not os.path.isfile(pattern_file):
        print("ERROR: pattern file not found: %s" % pattern_file, file=sys.stderr)
        return 2
    if not os.path.isfile(measurements_file):
        print("ERROR: measurements file not found: %s" % measurements_file, file=sys.stderr)
        return 2

    os.makedirs(output_dir, exist_ok=True)

    cmd = [
        actiond_exe,
        "--pattern", pattern_file,
        "--measurements", measurements_file,
        "--actions", actions_file,
        "--save-pattern", save_pattern_path,
    ]
    print("Running: %s" % " ".join(cmd), file=sys.stderr)

    # cwd=output_dir: any relative "path" field inside the actions.json (render.snapshot's
    # "path": "square.png", etc.) resolves relative to here, landing the file directly in
    # output_dir with no separate copy step needed. See module docstring.
    proc = subprocess.run(cmd, cwd=output_dir, capture_output=True, text=True)

    stderr_log_path = os.path.join(output_dir, "stderr.log")
    with open(stderr_log_path, "w", encoding="utf-8") as f:
        f.write(proc.stderr)

    responses_path = os.path.join(output_dir, "responses.json")

    # exit code >2 (a signal-killed/crashed process on this platform) is the one case actiond's
    # own contract (0/1/2 -- see main.cpp) never produces on purpose; treat it as a hard failure
    # distinct from a clean, reported error.
    if proc.returncode > 2:
        print("CRASHED: actiond exited with code %d (a signal-killed/faulted process, not a clean error)."
              % proc.returncode, file=sys.stderr)
        print("stderr (also saved to %s):" % stderr_log_path, file=sys.stderr)
        print(proc.stderr, file=sys.stderr)
        with open(responses_path, "w", encoding="utf-8") as f:
            json.dump({"error": "actiond crashed", "returncode": proc.returncode, "stderr": proc.stderr}, f, indent=2)
        return 2

    if proc.returncode == 2:
        print("ERROR: actiond reported a usage error (exit 2):", file=sys.stderr)
        print(proc.stderr, file=sys.stderr)
        return 2

    if proc.returncode == 1:
        # A clean, single-line {"error": "..."} on stderr -- see main.cpp's reportError(). The
        # whole batch never ran (e.g. the pattern file itself failed to load).
        print("ERROR: actiond failed to run the batch at all (exit 1):", file=sys.stderr)
        print(proc.stderr, file=sys.stderr)
        try:
            error_obj = json.loads(proc.stderr.strip().splitlines()[-1])
        except (ValueError, IndexError):
            error_obj = {"error": proc.stderr.strip()}
        with open(responses_path, "w", encoding="utf-8") as f:
            json.dump(error_obj, f, indent=2)
        return 2

    # returncode == 0: actiond ran the whole batch to completion. stdout is one compact JSON line.
    try:
        result = json.loads(proc.stdout)
    except ValueError as exc:
        print("ERROR: actiond exited 0 but stdout was not valid JSON: %s" % exc, file=sys.stderr)
        print("stdout was:", file=sys.stderr)
        print(proc.stdout, file=sys.stderr)
        return 2

    with open(responses_path, "w", encoding="utf-8") as f:
        json.dump(result, f, indent=2)

    results = result.get("results", [])
    failures = [(i, r) for i, r in enumerate(results) if not r.get("ok", False)]

    print("%d action(s) ran; %d failed." % (len(results), len(failures)))
    if failures:
        print("FAILED actions:", file=sys.stderr)
        for i, r in failures:
            print("  [%d] op=%s error=%s" % (i, r.get("op"), r.get("error")), file=sys.stderr)
        print("See %s for the full response and %s for actiond's stderr log." % (responses_path, stderr_log_path),
              file=sys.stderr)
        return 1

    print("All actions succeeded. Response: %s" % responses_path)
    if os.path.isfile(save_pattern_path):
        print("Saved pattern: %s" % save_pattern_path)
    return 0


if __name__ == "__main__":
    sys.exit(main())
