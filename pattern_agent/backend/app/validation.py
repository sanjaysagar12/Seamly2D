"""Local, fail-fast validation of a tool call's arguments against its own
input_schema, before anything is sent to the actiond subprocess."""
from __future__ import annotations

from typing import Any

import jsonschema


def validate_tool_input(schema: dict[str, Any], tool_input: dict[str, Any]) -> str | None:
    """Returns None if valid, or a human-readable error message if not."""
    try:
        jsonschema.validate(instance=tool_input, schema=schema)
    except jsonschema.ValidationError as exc:
        path = "/".join(str(p) for p in exc.path) or "(root)"
        return f"Invalid arguments at {path}: {exc.message}"
    return None
