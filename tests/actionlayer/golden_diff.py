#!/usr/bin/env python3
"""golden_diff.py -- compares an actual .val pattern file against a golden expected/*.val file.

Ignores volatile fields:
  - The "created with Seamly2D vX.Y.Z" comment near the top of every .val file: Python's
    xml.etree.ElementTree drops XML comments entirely while parsing, so this is excluded for
    free (never appears as a node in either tree).
  - The <version> element's own text (the pattern *schema* version, e.g. "0.7.4"): present in
    both trees structurally, but its text is not compared, so a schema-version bump alone never
    fails a golden comparison.
  - Small floating-point noise in numeric attribute values (x=, y=, mx=, my=, length=, ...):
    compared with a small absolute+relative tolerance instead of exact string equality, so a
    golden file regenerated on a different machine/compiler/Qt version is not falsely flagged
    for last-bit rounding differences.

Everything else -- element tags, element order, attribute names/values (non-numeric exactly,
numeric within tolerance), element text (outside the ignored tags), and tree shape -- is compared
exactly. Any real difference is a geometry or structure regression worth failing the build over.

Also runs a structural "no forward/wrong-section references" check against the ACTUAL file (see
check_calculation_references() below) independent of the golden-file comparison -- this is the
regression test for a real bug found while authoring tests/actionlayer/cases/
03_import_l_shape_to_rectangle: NameResolver could resolve a calculation-context name (e.g.
"basePoint": "B") to a same-named <point type="modeling"> piece-node clone instead of the real
<calculation> point, producing a saved file where a <calculation> element's reference attribute
(basePoint=, firstPoint=, ...) pointed at an id that is only ever *defined* later in <modeling> --
which Seamly2D's own GUI parser (walking the file strictly in document order) then fails to open,
with "ExceptionBadId: Can't find object Id: ..., id = <n>". golden_diff.py catches this
independently of whether Seamly2D itself is available to actually try opening the file (see
tests/actionlayer/README.md's "Known gaps" section for the fix and why this check exists).

Usage:
    python3 golden_diff.py <actual.val> <expected.val>

Exit codes:
    0  -- no differences found (or only ignored/tolerated ones), and the structural reference
          check found no forward/wrong-section references.
    1  -- at least one real difference and/or reference-check problem found; each is printed as
          one line to stdout.
    2  -- usage error (missing file, unparsable XML).
"""

import sys
import xml.etree.ElementTree as ET

IGNORED_TEXT_TAGS = {"version"}

# Attribute names known to hold an object reference (another element's own "id" value) inside a
# <calculation> element -- not an exhaustive schema-derived list, just every reference attribute
# this test suite's own cases currently produce plus the other point/line/curve tools' obvious
# equivalents (see src/libs/actionlayer/handlers/*.cpp for the JSON fields these come from).
# Extend as new cases exercise more tool types. "idObject" is deliberately excluded: it is how a
# *modeling*-section element legitimately references its *calculation*-section source object --
# the intentional cross-section reference this schema relies on, not the bug this check looks for.
CALCULATION_REFERENCE_ATTRS = {
    "basePoint", "firstPoint", "secondPoint", "thirdPoint", "center",
    "firstArc", "secondArc", "firstCurve", "secondCurve",
    "firstLinePoint", "secondLinePoint", "firstCircleCenter", "secondCircleCenter",
    "p1Line", "p2Line", "p1Line1", "p2Line1", "p1Line2", "p2Line2",
    "axisP1", "axisP2", "pShoulder", "tangentPoint", "circleCenter",
    "originPoint", "origin", "spline", "arc", "curve",
}


def check_calculation_references(root):
    """Walks every <calculation> element in document order, tracking which ids have been defined
    so far (by that point in the same document-order walk, across every <calculation> block in the
    file -- ids are a single global sequence in this schema, not per-draftBlock). For each
    element's CALCULATION_REFERENCE_ATTRS values, flags a problem if the referenced id has not
    been defined yet by a <calculation> element at this point -- which is true both for a genuine
    forward reference *within* <calculation> and for a reference to an id that is only ever
    defined in <modeling> (since modeling-section ids are never added to the defined set here):
    both are the same underlying mistake from a document-order parser's point of view, and both
    are exactly what this check exists to catch. Returns a list of human-readable problem strings
    (empty if none)."""
    problems = []
    defined_calc_ids = set()

    for draft_block in root.iter("draftBlock"):
        calculation = draft_block.find("calculation")
        if calculation is None:
            continue
        for element in calculation:
            for attr_name in sorted(CALCULATION_REFERENCE_ATTRS & set(element.attrib.keys())):
                ref_value = element.attrib[attr_name]
                if not ref_value.isdigit():
                    continue  # Not an id-shaped value (e.g. a formula token); nothing to check.
                ref_id = int(ref_value)
                if ref_id not in defined_calc_ids:
                    problems.append(
                        "<calculation>/<%s id=%s>: attribute \"%s\"=%s references an id that is "
                        "not yet defined by an earlier <calculation> element (forward reference, "
                        "or a reference to a <modeling>-only id) -- this file would fail to "
                        "reopen in Seamly2D's own document-order parser"
                        % (strip_ns(element.tag), element.attrib.get("id", "?"), attr_name, ref_value))
            element_id = element.attrib.get("id")
            if element_id is not None and element_id.isdigit():
                defined_calc_ids.add(int(element_id))

    return problems

# Attribute names whose values are always compared numerically (with tolerance) when both sides
# parse as a float, regardless of which element they appear on -- covers every coordinate/formula-
# result attribute this schema uses without having to enumerate every element tag that carries one.
NUMERIC_ATTRS = {"x", "y", "mx", "my", "length", "angle", "radius", "width", "height"}

ABS_TOL = 1e-6
REL_TOL = 1e-9


def strip_ns(tag):
    return tag.split("}", 1)[-1] if "}" in tag else tag


def numbers_close(a, b):
    try:
        fa, fb = float(a), float(b)
    except ValueError:
        return None  # Not both numeric; caller falls back to exact string comparison.
    return abs(fa - fb) <= max(ABS_TOL, REL_TOL * max(abs(fa), abs(fb)))


def compare_elements(actual, expected, path, diffs):
    a_tag = strip_ns(actual.tag)
    e_tag = strip_ns(expected.tag)
    if a_tag != e_tag:
        diffs.append("%s: tag mismatch: actual=<%s> expected=<%s>" % (path, a_tag, e_tag))
        return

    node_path = "%s/%s" % (path, a_tag)

    a_attrs = actual.attrib
    e_attrs = expected.attrib
    a_keys = set(a_attrs.keys())
    e_keys = set(e_attrs.keys())
    for missing in sorted(e_keys - a_keys):
        diffs.append("%s: missing attribute \"%s\" (expected=%r)" % (node_path, missing, e_attrs[missing]))
    for extra in sorted(a_keys - e_keys):
        diffs.append("%s: unexpected attribute \"%s\" (actual=%r)" % (node_path, extra, a_attrs[extra]))
    for key in sorted(a_keys & e_keys):
        av, ev = a_attrs[key], e_attrs[key]
        if av == ev:
            continue
        if key in NUMERIC_ATTRS or key not in NUMERIC_ATTRS:
            close = numbers_close(av, ev)
            if close is True:
                continue
        diffs.append("%s: attribute \"%s\" mismatch: actual=%r expected=%r" % (node_path, key, av, ev))

    if a_tag not in IGNORED_TEXT_TAGS:
        a_text = (actual.text or "").strip()
        e_text = (expected.text or "").strip()
        if a_text != e_text:
            close = numbers_close(a_text, e_text)
            if close is not True:
                diffs.append("%s: text mismatch: actual=%r expected=%r" % (node_path, a_text, e_text))

    a_children = list(actual)
    e_children = list(expected)
    if len(a_children) != len(e_children):
        diffs.append("%s: child count mismatch: actual=%d expected=%d (%s vs %s)" % (
            node_path, len(a_children), len(e_children),
            [strip_ns(c.tag) for c in a_children], [strip_ns(c.tag) for c in e_children]))
        # Still compare the overlapping prefix so a single extra/missing child doesn't hide
        # every other real difference underneath it.
    for a_child, e_child in zip(a_children, e_children):
        compare_elements(a_child, e_child, node_path, diffs)


def main():
    if len(sys.argv) != 3:
        print(__doc__, file=sys.stderr)
        return 2

    actual_path, expected_path = sys.argv[1], sys.argv[2]

    try:
        actual_root = ET.parse(actual_path).getroot()
    except (ET.ParseError, OSError) as exc:
        print("ERROR: could not parse actual file %s: %s" % (actual_path, exc), file=sys.stderr)
        return 2
    try:
        expected_root = ET.parse(expected_path).getroot()
    except (ET.ParseError, OSError) as exc:
        print("ERROR: could not parse expected file %s: %s" % (expected_path, exc), file=sys.stderr)
        return 2

    diffs = []
    compare_elements(actual_root, expected_root, "", diffs)

    reference_problems = check_calculation_references(actual_root)

    if not diffs and not reference_problems:
        print("OK: %s matches %s (and its <calculation> references are all well-formed)" % (actual_path, expected_path))
        return 0

    if diffs:
        print("DIFFERENCES between %s (actual) and %s (expected):" % (actual_path, expected_path))
        for d in diffs:
            print("  " + d)

    if reference_problems:
        print("STRUCTURAL REFERENCE PROBLEMS in %s:" % actual_path)
        for p in reference_problems:
            print("  " + p)

    return 1


if __name__ == "__main__":
    sys.exit(main())
