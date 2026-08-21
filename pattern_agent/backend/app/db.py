"""SQLite persistence for sessions: metadata, the agent's own conversation
(`messages_json` -- what actually lets a chat message resume reasoning after a
backend restart), and the event history (what lets the frontend timeline replay a
session's whole run after a restart, the same way it already replays a live session
to a newly-connecting WebSocket client).

Written on every emitted event (session_manager.py's `emit` closure), read once at
startup (session_manager.load_from_db()). A single small aiosqlite connection is fine
here -- writes are one row per agent turn (roughly one per Claude call), not a hot
per-request path.
"""
from __future__ import annotations

import json
from pathlib import Path
from typing import Any

import aiosqlite

from . import config

_SCHEMA = """
CREATE TABLE IF NOT EXISTS sessions (
    session_id TEXT PRIMARY KEY,
    goal TEXT NOT NULL,
    step_limit INTEGER NOT NULL,
    status TEXT NOT NULL,
    step INTEGER NOT NULL,
    stop_reason TEXT,
    final_summary TEXT,
    measurements_path TEXT,
    output_dir TEXT NOT NULL,
    val_path TEXT,
    messages_json TEXT NOT NULL DEFAULT '[]',
    created_at TEXT NOT NULL,
    updated_at TEXT NOT NULL
);

CREATE TABLE IF NOT EXISTS events (
    id INTEGER PRIMARY KEY AUTOINCREMENT,
    session_id TEXT NOT NULL,
    seq INTEGER NOT NULL,
    event_json TEXT NOT NULL,
    FOREIGN KEY (session_id) REFERENCES sessions(session_id)
);
CREATE INDEX IF NOT EXISTS idx_events_session_seq ON events(session_id, seq);
"""

_connection: aiosqlite.Connection | None = None


async def init_db(db_path: Path = config.DB_PATH) -> None:
    global _connection
    db_path.parent.mkdir(parents=True, exist_ok=True)
    _connection = await aiosqlite.connect(str(db_path))
    _connection.row_factory = aiosqlite.Row
    await _connection.executescript(_SCHEMA)
    await _connection.commit()


async def close_db() -> None:
    global _connection
    if _connection is not None:
        await _connection.close()
        _connection = None


def _conn() -> aiosqlite.Connection:
    if _connection is None:
        raise RuntimeError("db.init_db() must be called before using the database")
    return _connection


def _block_to_jsonable(block: Any) -> Any:
    if isinstance(block, dict):
        return block
    if hasattr(block, "model_dump"):
        # Real Anthropic SDK content blocks (ThinkingBlock/TextBlock/ToolUseBlock, ...).
        return block.model_dump(mode="json")
    if hasattr(block, "__dict__"):
        # Anything else object-shaped (e.g. a test double) -- best-effort plain dict.
        return dict(vars(block))
    return block


def serialize_messages(messages: list[dict[str, Any]]) -> str:
    """Converts a live AgentSession.messages list (a mix of plain dicts and Anthropic
    SDK content-block objects, e.g. ThinkingBlock/TextBlock/ToolUseBlock on assistant
    turns) into a JSON string safe to store and, later, hand straight back to the API
    as plain dicts -- the Messages API accepts either form for message content."""
    out = []
    for message in messages:
        content = message["content"]
        if isinstance(content, str):
            out.append({"role": message["role"], "content": content})
        else:
            out.append({"role": message["role"], "content": [_block_to_jsonable(b) for b in content]})
    return json.dumps(out)


def deserialize_messages(messages_json: str) -> list[dict[str, Any]]:
    return json.loads(messages_json)


async def upsert_session(row: dict[str, Any]) -> None:
    conn = _conn()
    await conn.execute(
        """
        INSERT INTO sessions
            (session_id, goal, step_limit, status, step, stop_reason, final_summary,
             measurements_path, output_dir, val_path, messages_json, created_at, updated_at)
        VALUES
            (:session_id, :goal, :step_limit, :status, :step, :stop_reason, :final_summary,
             :measurements_path, :output_dir, :val_path, :messages_json, :created_at, :updated_at)
        ON CONFLICT(session_id) DO UPDATE SET
            step_limit = excluded.step_limit,
            status = excluded.status,
            step = excluded.step,
            stop_reason = excluded.stop_reason,
            final_summary = excluded.final_summary,
            val_path = excluded.val_path,
            messages_json = excluded.messages_json,
            updated_at = excluded.updated_at
        """,
        row,
    )
    await conn.commit()


async def append_event(session_id: str, seq: int, event: dict[str, Any]) -> None:
    conn = _conn()
    await conn.execute(
        "INSERT INTO events (session_id, seq, event_json) VALUES (?, ?, ?)",
        (session_id, seq, json.dumps(event)),
    )
    await conn.commit()


async def load_all_sessions() -> list[dict[str, Any]]:
    conn = _conn()
    async with conn.execute("SELECT * FROM sessions ORDER BY created_at") as cursor:
        rows = await cursor.fetchall()
    return [dict(row) for row in rows]


async def load_events(session_id: str) -> list[dict[str, Any]]:
    conn = _conn()
    async with conn.execute(
        "SELECT event_json FROM events WHERE session_id = ? ORDER BY seq", (session_id,)
    ) as cursor:
        rows = await cursor.fetchall()
    return [json.loads(row["event_json"]) for row in rows]
