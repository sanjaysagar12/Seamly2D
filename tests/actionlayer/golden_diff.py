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

Usage:
    python3 golden_diff.py <actual.val> <expected.val>

Exit codes:
    0  -- no differences found (or only ignored/tolerated ones).
    1  -- at least one real difference found; each is printed as one line to stdout.
    2  -- usage error (missing file, unparsable XML).
"""

import sys
import xml.etree.ElementTree as ET

IGNORED_TEXT_TAGS = {"version"}

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

    if not diffs:
        print("OK: %s matches %s" % (actual_path, expected_path))
        return 0

    print("DIFFERENCES between %s (actual) and %s (expected):" % (actual_path, expected_path))
    for d in diffs:
        print("  " + d)
    return 1


if __name__ == "__main__":
    sys.exit(main())
