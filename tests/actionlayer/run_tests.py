#!/usr/bin/env python3
"""Runs the three Task-4 scenarios against the seamly2d-actiond persistent NDJSON daemon.

Each scenario spawns a fresh actiond process in daemon mode (no --actions flag), writes one
NDJSON request line to its stdin, reads one NDJSON response line back from its stdout, and waits
for the process to exit (every scenario script's own last action is "session.close", so the daemon
exits on its own once that response is written -- no separate close request is needed).

Stdlib only: subprocess, json, os, sys, pathlib. No third-party dependencies.

Usage:
    python3 run_tests.py

Environment:
    ACTIOND_EXE  Path to actiond.exe/actiond. Defaults to
                 <repo-root>/out/src/app/actiond/bin/actiond(.exe), matching every other harness
                 under tests/actionlayer/ (see that directory's own README.md).

Exit code: 0 if every scenario passes, 1 otherwise.
"""

import json
import os
import subprocess
import sys
from pathlib import Path

HERE = Path(__file__).resolve().parent
REPO_ROOT = HERE.parent.parent
SCRIPTS_DIR = HERE / "scripts"
FIXTURES_DIR = HERE / "fixtures"
OUTPUT_DIR = HERE / "output"

DEFAULT_EXE_NAME = "actiond.exe" if os.name == "nt" else "actiond"
ACTIOND_EXE = Path(os.environ.get(
    "ACTIOND_EXE", str(REPO_ROOT / "out" / "src" / "app" / "actiond" / "bin" / DEFAULT_EXE_NAME)))

TIMEOUT_SECONDS = 60


class ScenarioFailure(Exception):
    """Raised to fail one scenario loudly, with the full context needed to diagnose it."""


def run_scenario(name, script_path, pattern_path, expected_output_files):
    OUTPUT_DIR.mkdir(parents=True, exist_ok=True)

    with open(script_path, "r", encoding="utf-8") as f:
        request_text = f.read()
    # Re-serialize compactly: the protocol requires one line with no embedded newlines, and the
    # scripts/*.json files are pretty-printed for human readability, not written as single lines.
    request_line = json.dumps(json.loads(request_text), separators=(",", ":"))

    args = [str(ACTIOND_EXE), "--output-dir", str(OUTPUT_DIR)]
    if pattern_path is not None:
        args += ["--pattern", str(pattern_path)]

    proc = subprocess.Popen(
        args, stdin=subprocess.PIPE, stdout=subprocess.PIPE, stderr=subprocess.PIPE,
        text=True, encoding="utf-8", bufsize=1)

    try:
        proc.stdin.write(request_line + "\n")
        proc.stdin.flush()
        proc.stdin.close()

        response_line = proc.stdout.readline()
        if not response_line:
            stderr_output = proc.stderr.read()
            raise ScenarioFailure(
                f"no response line from actiond (process may have crashed on startup).\nstderr:\n{stderr_output}")

        try:
            response = json.loads(response_line)
        except json.JSONDecodeError as error:
            raise ScenarioFailure(f"response line is not valid JSON ({error}):\n{response_line}")

        try:
            exit_code = proc.wait(timeout=TIMEOUT_SECONDS)
        except subprocess.TimeoutExpired:
            proc.kill()
            raise ScenarioFailure(
                "actiond did not exit after the session.close response (expected the daemon "
                "to exit once that batch's response was written)")

        if exit_code != 0:
            stderr_output = proc.stderr.read()
            raise ScenarioFailure(f"actiond exited with code {exit_code}.\nstderr:\n{stderr_output}")
    finally:
        if proc.poll() is None:
            proc.kill()

    print(f"  response: {json.dumps(response, indent=2)}")

    if response.get("status") != "ok":
        raise ScenarioFailure(f"expected top-level status \"ok\", got the response above")

    for result in response.get("results", []):
        if result.get("status") != "ok":
            raise ScenarioFailure(f"action {result.get('index')} (\"{result.get('op')}\") failed: {result.get('error')}")

    for filename in expected_output_files:
        path = OUTPUT_DIR / filename
        if not path.exists():
            raise ScenarioFailure(f"expected output file missing: {path}")
        if path.stat().st_size == 0:
            raise ScenarioFailure(f"expected output file is empty: {path}")


def main():
    if not ACTIOND_EXE.exists():
        print(f"ERROR: actiond executable not found at {ACTIOND_EXE}")
        print("Build it first (see build_actiond.bat at the repo root), or set ACTIOND_EXE.")
        return 1

    scenarios = [
        # 01/02 start from a genuinely empty pattern via PatternSession::createEmpty() (no
        # --pattern flag at all) -- a schema-valid file *can't* represent "zero draft blocks" (the
        # schema requires draftBlock+, one or more), so there is no fixtures/empty.val to load
        # here; fixtures/empty.val instead exists only as a minimal (one base point) fixture for
        # manually smoke-testing `actiond --pattern` itself, per this directory's README.
        ("01_draw_square", SCRIPTS_DIR / "01_draw_square.json", None,
         ["square.png", "square.val"]),
        ("02_draw_l_shape", SCRIPTS_DIR / "02_draw_l_shape.json", None,
         ["l_shape_drawn.png", "l_shape_drawn.val"]),
        ("03_import_l_and_convert_to_rectangle",
         SCRIPTS_DIR / "03_import_l_and_convert_to_rectangle.json", FIXTURES_DIR / "l_shape.val",
         ["l_to_rectangle.png", "l_to_rectangle.val"]),
    ]

    results = []
    for name, script_path, pattern_path, expected_output_files in scenarios:
        print(f"=== {name} ===")
        try:
            run_scenario(name, script_path, pattern_path, expected_output_files)
        except ScenarioFailure as error:
            print(f"  FAIL: {error}")
            results.append((name, False))
        else:
            print("  PASS")
            results.append((name, True))
        print()

    print("=== Summary ===")
    all_passed = True
    for name, passed in results:
        print(f"  {'PASS' if passed else 'FAIL'}  {name}")
        all_passed = all_passed and passed

    return 0 if all_passed else 1


if __name__ == "__main__":
    sys.exit(main())
