#!/usr/bin/env python3
"""Per-case assertions for actionlayer Phase 7's fixture set. See README.md for what each
case proves; this file is where that proof is actually checked, one function per case name.

Usage: check_output.py <case_name> <output/<case_name>.out.json>
Exit 0 and prints "PASS" on success; exit 1 and prints "FAIL: <reason>" otherwise.
"""
import json
import sys

# ToPixel(value, Unit.Mm) = (value / 25.4) * 96.0 (src/libs/vmisc/def.cpp's PrintDPI=96.0
# constant) -- base_pattern.val declares <unit>mm</unit>, and shoulder_length is declared in cm
# in every fixture .smis file here, so readMeasurements() converts cm -> mm automatically before
# this conversion ever applies. 13cm == 130mm, 20cm == 200mm.
TOPX_SHOULDER_A = (130.0 / 25.4) * 96.0  # measurements_a.smis: shoulder_length=13cm -> 491.3385826771654 px.
TOPX_SHOULDER_B = (200.0 / 25.4) * 96.0  # measurements_b.smis: shoulder_length=20cm -> 755.9055118110236 px.
EPS = 1e-6  # Floating-point tolerance for the coordinate comparisons below.


def load_results(output_path):
    """Reads a saved actiond response file and returns its top-level "results" array."""
    with open(output_path, "r", encoding="utf-8") as handle:
        return json.load(handle)["results"]


def find_point(dump_action_result, name):
    """Returns the named point's JSON object from a pattern.dump action result, or None."""
    for obj in dump_action_result["value"]["objects"]:
        if obj.get("name") == name:
            return obj
    return None


def approx(actual, expected):
    """True if two floats are equal within EPS -- direct == would fail on harmless FP noise."""
    return abs(actual - expected) < EPS


def normalized_objects(dump_action_result):
    """pattern.dump's "objects" array order comes from a QHash whose iteration order is
    per-process-random (see tests/actionlayer/actionlayer-tests/run_tests.sh's own
    normalize_response_json() for the same issue in Phase 6's fixtures) -- sorting by "id" here
    makes a before/after comparison robust even if that random order ever changed mid-run."""
    return sorted(dump_action_result["value"]["objects"], key=lambda obj: obj["id"])


def fail(reason):
    print(f"FAIL: {reason}")
    sys.exit(1)


def check_01_load_only(results):
    load_result, dump_result = results  # This case's batch is exactly [measurements.load, pattern.dump].
    if not load_result["ok"]:
        fail(f"measurements.load failed: {load_result['error']}")
    if load_result["value"].get("type") != "individual":
        fail(f"expected type \"individual\", got {load_result['value'].get('type')!r}")
    a1 = find_point(dump_result, "A1")
    if a1 is None:
        fail("point \"A1\" missing from pattern.dump")
    if not approx(a1["x"], TOPX_SHOULDER_A):
        fail(f"A1.x changed after a load-only action (no recompute): got {a1['x']}, expected unchanged {TOPX_SHOULDER_A}")


def check_recomputed_to_set_b(results):
    # Shared by case 02 (measurements.load + measurements.recompute) and case 03
    # (measurements.sync alone) -- both batches end with a pattern.dump as their last action.
    dump_result = results[-1]
    a1 = find_point(dump_result, "A1")
    if a1 is None:
        fail("point \"A1\" missing from pattern.dump")
    if not approx(a1["x"], TOPX_SHOULDER_B):
        fail(f"A1.x not recomputed to reflect the new measurement value: got {a1['x']}, expected {TOPX_SHOULDER_B}")


def check_04_switch_measurement_sets(results):
    load_a, dump1, load_b, recompute, dump2 = results
    if not load_a["ok"]:
        fail(f"first measurements.load (set A) failed: {load_a['error']}")
    if not load_b["ok"]:
        fail(f"second measurements.load (set B) failed: {load_b['error']}")
    if not recompute["ok"]:
        fail(f"measurements.recompute failed: {recompute['error']}")
    a1_before = find_point(dump1, "A1")
    a1_after = find_point(dump2, "A1")
    if a1_before is None or a1_after is None:
        fail("point \"A1\" missing from one of the two dumps")
    if not approx(a1_before["x"], TOPX_SHOULDER_A):
        fail(f"dump #1 (set A, no recompute run yet) A1.x wrong: got {a1_before['x']}, expected {TOPX_SHOULDER_A}")
    if not approx(a1_after["x"], TOPX_SHOULDER_B):
        fail(f"dump #2 (set B, after recompute) A1.x wrong: got {a1_after['x']}, expected {TOPX_SHOULDER_B}")


def check_05_missing_measurement_error(results):
    dump_before, load_result, dump_after = results
    if load_result["ok"]:
        fail("expected measurements.load against measurements_incomplete.smis to fail, but it succeeded")
    error = load_result["error"]
    if not isinstance(error, dict) or error.get("type") != "missingMeasurements":
        fail(f"expected a structured missingMeasurements error, got {error!r}")
    if "shoulder_length" not in error.get("missing", []):
        fail(f"expected \"shoulder_length\" in the missing-measurements list, got {error.get('missing')!r}")
    if normalized_objects(dump_before) != normalized_objects(dump_after):
        fail("pattern.dump differs before vs. after the failed load -- state was mutated despite the check failing")


def check_06_type_mismatch_error(results):
    dump_before, load_result, dump_after = results
    if load_result["ok"]:
        fail("expected measurements.load against a multisize file to fail against an individual-type pattern, but it succeeded")
    error = load_result["error"]
    if not isinstance(error, dict) or error.get("type") != "measurementTypeMismatch":
        fail(f"expected a structured measurementTypeMismatch error, got {error!r}")
    if error.get("expected") != "individual" or error.get("actual") != "multisize":
        fail(f"expected expected=\"individual\"/actual=\"multisize\", got expected={error.get('expected')!r} actual={error.get('actual')!r}")
    if normalized_objects(dump_before) != normalized_objects(dump_after):
        fail("pattern.dump differs before vs. after the rejected load -- state was mutated despite the check failing")


CHECKS = {
    "01_load_only": check_01_load_only,
    "02_load_then_recompute": check_recomputed_to_set_b,
    "03_sync_single_action": check_recomputed_to_set_b,
    "04_switch_measurement_sets": check_04_switch_measurement_sets,
    "05_missing_measurement_error": check_05_missing_measurement_error,
    "06_type_mismatch_error": check_06_type_mismatch_error,
}


def main():
    if len(sys.argv) != 3:
        print("usage: check_output.py <case_name> <output.json>")
        sys.exit(2)
    case_name, output_path = sys.argv[1], sys.argv[2]
    check = CHECKS.get(case_name)
    if check is None:
        fail(f"no assertion function registered for case {case_name!r} -- add one to CHECKS in check_output.py")

    try:
        results = load_results(output_path)
    except (OSError, json.JSONDecodeError, KeyError) as error:
        fail(f"could not read/parse {output_path}: {error}")

    check(results)
    print("PASS")


if __name__ == "__main__":
    main()
