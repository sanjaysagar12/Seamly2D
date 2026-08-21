"""Converts `actiond --list-tools --format=ai` output into the Anthropic API's
`tools` array shape.

The daemon's own --format=ai output is already very close to
{"name", "description", "input_schema"} -- see the project README's Step 0 findings.
This module:
  - strips the actiond-specific "category"/"status"/"statusReason" fields Anthropic's
    API doesn't expect on a tool definition (they're kept for our own bookkeeping,
    e.g. surfacing reliability caveats in the UI, just not sent to the model)
  - excludes ops that are unsafe to let an autonomous agent call at all
  - excludes ops the orchestrator itself drives every turn, so the agent isn't tempted
    to spend its one-tool-call-per-turn budget on session bookkeeping
  - appends the backend-only `pattern.complete` stop-signal tool (not a real actiond
    op -- see agent_loop.py)
"""
from __future__ import annotations

import asyncio
import json
import logging
from typing import Any

from . import config

logger = logging.getLogger("pattern_agent.tool_schema")

# piece.union reproducibly segfaults the actiond process (see
# docs/action-layer-schema.md in the Seamly2D repo, and --list-tools --format=ai's
# own "status":"partial" / statusReason on this op). A crash mid-session loses all
# in-memory pattern state, so unlike every other failure mode (which comes back as a
# clean JSON error the agent can retry past), this one is not worth exposing to the
# model at all.
UNSAFE_OPS = {"piece.union"}

# These ops are driven by the orchestrator itself every turn (agent_loop.py), not by
# the agent's own tool choice:
#   - render.snapshot runs automatically after every turn (the "view and verify" step
#     the task spec requires) -- letting the agent also call it would just burn its
#     one-tool-call-per-turn budget on a no-op.
#   - session.save runs automatically as a per-step checkpoint (crash recovery) and
#     once more at session end.
#   - session.close is driven by the backend when the session actually ends (on
#     pattern.complete, the step-limit, or a user stop) -- the agent signals "done" via
#     pattern.complete instead, so its stop path always goes through our own bookkeeping.
BACKEND_MANAGED_OPS = {"render.snapshot", "session.save", "session.close"}

EXCLUDED_OPS = UNSAFE_OPS | BACKEND_MANAGED_OPS

# actiond op names use dots as category separators (e.g. "pattern.dump",
# "measurements.load", "piece.addPatternPiece") but Anthropic's tool `name` must match
# `^[a-zA-Z0-9_-]{1,128}$` -- dots are rejected with a 400. So every tool name sent to
# the model is the dotted op name with '.' -> '_' (verified none of the 48 registered
# ops already contain '_', so this can't collide), and `name_map` in the built catalog
# translates a tool_use.name back to the real op name before it's ever handed to
# actiond or looked up in op_metadata. See agent_loop.py's _execute_tool.
PATTERN_COMPLETE_OP = "pattern.complete"


def sanitize_tool_name(op: str) -> str:
    return op.replace(".", "_")


PATTERN_COMPLETE_TOOL = {
    "name": sanitize_tool_name(PATTERN_COMPLETE_OP),
    "description": (
        "Call this when the pattern/piece described in the user's goal is finished "
        "and the current draft (as seen in the latest snapshot image) satisfies it. "
        "This ends the session -- no further actions will run. Do not call it for a "
        "sub-step; only when the whole requested piece is done."
    ),
    "input_schema": {
        "type": "object",
        "properties": {
            "summary": {
                "type": "string",
                "description": (
                    "A short justification: what was built and why it satisfies the "
                    "goal. Shown to the user as the completion reason."
                ),
            }
        },
        "required": ["summary"],
    },
}


class ToolSchemaError(Exception):
    pass


def _to_anthropic_tool(raw: dict[str, Any]) -> dict[str, Any]:
    schema = dict(raw["input_schema"])
    schema.setdefault("type", "object")
    schema.setdefault("properties", {})
    return {
        "name": sanitize_tool_name(raw["name"]),
        "description": raw["description"],
        "input_schema": schema,
    }


async def fetch_raw_catalog() -> list[dict[str, Any]]:
    """Runs `actiond --list-tools --format=ai` and returns the parsed JSON array."""
    if not config.ACTIOND_EXE.exists():
        raise ToolSchemaError(f"actiond executable not found at {config.ACTIOND_EXE}")

    proc = await asyncio.create_subprocess_exec(
        str(config.ACTIOND_EXE),
        "--list-tools",
        "--format=ai",
        stdout=asyncio.subprocess.PIPE,
        stderr=asyncio.subprocess.PIPE,
    )
    stdout, stderr = await proc.communicate()
    if proc.returncode != 0:
        raise ToolSchemaError(
            f"actiond --list-tools --format=ai exited {proc.returncode}: "
            f"{stderr.decode('utf-8', errors='replace')}"
        )
    try:
        catalog = json.loads(stdout.decode("utf-8"))
    except json.JSONDecodeError as exc:
        raise ToolSchemaError(f"Could not parse --list-tools --format=ai output: {exc}") from exc

    if not isinstance(catalog, list):
        raise ToolSchemaError("Expected --list-tools --format=ai to return a JSON array")
    return catalog


def build_tool_catalog(raw_catalog: list[dict[str, Any]]) -> dict[str, Any]:
    """Returns {"anthropic_tools": [...], "op_metadata": {op: raw_entry},
    "name_map": {anthropic_tool_name: real_op_name}, "excluded": [...]}."""
    anthropic_tools: list[dict[str, Any]] = []
    op_metadata: dict[str, Any] = {}
    name_map: dict[str, str] = {}
    excluded: list[str] = []

    for raw in raw_catalog:
        name = raw.get("name")
        if not name:
            continue
        if name in EXCLUDED_OPS:
            excluded.append(name)
            op_metadata[name] = raw
            continue
        anthropic_tools.append(_to_anthropic_tool(raw))
        op_metadata[name] = raw
        name_map[sanitize_tool_name(name)] = name

    anthropic_tools.append(PATTERN_COMPLETE_TOOL)
    op_metadata[PATTERN_COMPLETE_OP] = {
        "name": PATTERN_COMPLETE_OP,
        "category": "session",
        "description": PATTERN_COMPLETE_TOOL["description"],
    }
    name_map[PATTERN_COMPLETE_TOOL["name"]] = PATTERN_COMPLETE_OP

    logger.info(
        "Built tool catalog: %d tools exposed, %d excluded (%s)",
        len(anthropic_tools),
        len(excluded),
        ", ".join(excluded) or "none",
    )
    return {
        "anthropic_tools": anthropic_tools,
        "op_metadata": op_metadata,
        "name_map": name_map,
        "excluded": excluded,
    }


async def load_tool_catalog() -> dict[str, Any]:
    raw = await fetch_raw_catalog()
    return build_tool_catalog(raw)
