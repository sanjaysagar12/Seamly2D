"""Event shapes streamed from the agent loop to the frontend (over WebSocket, one
JSON message per event -- see main.py's /ws/{session_id}).

Every event is a plain dict {"type": <name>, "sessionId": ..., ...}. Kept as plain
dicts (not pydantic models) since they're produced in a hot loop and consumed only as
JSON on the wire.
"""
from __future__ import annotations

from typing import Any


def _event(event_type: str, session_id: str, **fields: Any) -> dict[str, Any]:
    return {"type": event_type, "sessionId": session_id, **fields}


def thinking_delta(session_id: str, step: int, text: str) -> dict[str, Any]:
    return _event("thinking_delta", session_id, step=step, text=text)


def text_delta(session_id: str, step: int, text: str) -> dict[str, Any]:
    return _event("text_delta", session_id, step=step, text=text)


def action_started(session_id: str, step: int, op: str, input_: dict[str, Any]) -> dict[str, Any]:
    return _event("action_started", session_id, step=step, op=op, input=input_)


def action_result(
    session_id: str,
    step: int,
    op: str,
    success: bool,
    result: Any = None,
    error: Any = None,
) -> dict[str, Any]:
    return _event(
        "action_result", session_id, step=step, op=op, success=success, result=result, error=error
    )


def snapshot_ready(session_id: str, step: int, url: str) -> dict[str, Any]:
    return _event("snapshot_ready", session_id, step=step, url=url)


def piece_snapshot_ready(session_id: str, step: int, piece: str, url: str) -> dict[str, Any]:
    return _event("piece_snapshot_ready", session_id, step=step, piece=piece, url=url)


def step_complete(session_id: str, step: int) -> dict[str, Any]:
    return _event("step_complete", session_id, step=step)


def session_complete(session_id: str, reason: str, summary: str | None, val_url: str | None) -> dict[str, Any]:
    return _event(
        "session_complete", session_id, reason=reason, summary=summary, valUrl=val_url
    )


def error(session_id: str, message: str, fatal: bool = False, step: int | None = None) -> dict[str, Any]:
    return _event("error", session_id, message=message, fatal=fatal, step=step)


def status_changed(session_id: str, status: str) -> dict[str, Any]:
    return _event("status_changed", session_id, status=status)


def user_message(session_id: str, text: str, after_step: int) -> dict[str, Any]:
    return _event("user_message", session_id, text=text, afterStep=after_step)
