"""Milestone 2 verification: convert --list-tools --format=ai into Anthropic tool
defs and spot-check a couple of known ops by hand."""
import re
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent.parent))

import pytest

from app.tool_schema import (
    EXCLUDED_OPS,
    PATTERN_COMPLETE_OP,
    UNSAFE_OPS,
    build_tool_catalog,
    load_tool_catalog,
    sanitize_tool_name,
)

# The exact pattern the Anthropic API enforces on tools[].name (a 400
# invalid_request_error otherwise). actiond op names use '.' as a category separator
# (pattern.dump, measurements.load, ...), which this pattern rejects -- this is the
# regression test for that mismatch (see tool_schema.sanitize_tool_name).
ANTHROPIC_TOOL_NAME_RE = re.compile(r"^[a-zA-Z0-9_-]{1,128}$")


@pytest.mark.asyncio
async def test_catalog_shape_and_known_ops():
    catalog = await load_tool_catalog()
    tools = catalog["anthropic_tools"]
    names = {t["name"] for t in tools}

    # Every returned entry must satisfy the Anthropic tools[] shape, including the
    # name pattern the real API enforces -- this is what a 400 on tools.N.custom.name
    # looks like before it ever reaches the API.
    for t in tools:
        assert set(t.keys()) == {"name", "description", "input_schema"}
        assert ANTHROPIC_TOOL_NAME_RE.match(t["name"]), f"invalid tool name: {t['name']!r}"
        assert t["input_schema"]["type"] == "object"
        assert "properties" in t["input_schema"]

    # piece.union must never reach the model (segfaults actiond -- see tool_schema.py).
    assert "piece_union" not in names
    assert UNSAFE_OPS == {"piece.union"}

    # render.snapshot/session.save/session.close are orchestrator-driven, not agent-driven.
    assert not ({"render_snapshot", "session_save", "session_close"} & names)
    assert EXCLUDED_OPS == {"piece.union", "render.snapshot", "session.save", "session.close"}

    # Our backend-only completion signal must be present, under its sanitized name.
    assert "pattern_complete" in names
    assert "pattern.complete" not in names  # the raw, dotted form must never be a tool name

    # Spot-check basePoint (no dot -> sanitizes to itself) against what
    # --list-tools --format=ai actually returns.
    base_point = next(t for t in tools if t["name"] == "basePoint")
    assert set(base_point["input_schema"]["required"]) == {"name", "x", "y", "draftBlock"}
    assert base_point["input_schema"]["properties"]["x"]["type"] == "number"

    # Spot-check a dotted op: the model-facing name is sanitized, but name_map routes
    # it back to the real, dotted actiond op for both execution and op_metadata lookup.
    assert "pattern_dump" in names
    assert "pattern.dump" not in names
    assert catalog["name_map"]["pattern_dump"] == "pattern.dump"
    assert catalog["op_metadata"]["pattern.dump"]["name"] == "pattern.dump"

    # render.snapshot must still be resolvable via op_metadata for the orchestrator's
    # own internal use, even though it's excluded from the model-facing tool list.
    assert catalog["op_metadata"]["render.snapshot"]["name"] == "render.snapshot"

    # 48 real ops - 4 excluded (piece.union, render.snapshot, session.save,
    # session.close) + 1 synthetic (pattern.complete) = 45
    assert len(tools) == 45

    # Every sanitized name in name_map must round-trip to a real op or the synthetic
    # completion signal, and no two ops must sanitize to the same name (dot -> '_'
    # collisions would silently shadow one tool with another).
    assert len(catalog["name_map"]) == len(tools)
    for sanitized, original in catalog["name_map"].items():
        assert sanitize_tool_name(original) == sanitized


def test_build_tool_catalog_is_pure():
    raw = [
        {
            "name": "foo.bar",
            "category": "point",
            "description": "d",
            "input_schema": {"type": "object", "properties": {}, "required": []},
        },
        {
            "name": "piece.union",
            "category": "piece",
            "description": "d2",
            "status": "partial",
            "statusReason": "segfaults",
            "input_schema": {"type": "object", "properties": {}, "required": []},
        },
    ]
    catalog = build_tool_catalog(raw)
    names = {t["name"] for t in catalog["anthropic_tools"]}
    assert names == {"foo_bar", "pattern_complete"}
    assert catalog["excluded"] == ["piece.union"]
    assert catalog["name_map"] == {"foo_bar": "foo.bar", "pattern_complete": PATTERN_COMPLETE_OP}


if __name__ == "__main__":
    import asyncio
    import json

    async def main():
        catalog = await load_tool_catalog()
        print(json.dumps(catalog["anthropic_tools"][:2], indent=2))
        print(f"\n{len(catalog['anthropic_tools'])} tools total; excluded: {catalog['excluded']}")

    asyncio.run(main())
