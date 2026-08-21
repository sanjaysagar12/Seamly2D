"""Verifies the SQLite persistence flow requested alongside the chat feature: a
session's metadata, conversation, and event history survive a simulated backend
restart (close the DB connection, reopen the same file, build a fresh SessionManager
and call load_from_db()) -- and the reloaded session is actually usable afterward
(chat can revive it and keep building on the same geometry)."""
import sys
import uuid
from pathlib import Path
from types import SimpleNamespace

sys.path.insert(0, str(Path(__file__).resolve().parent.parent))

import pytest

from app import db
from app.session_manager import SessionManager
from tests.test_agent_loop import assert_valid_message_history


def _tool_use(tool_name: str, **input_):
    return SimpleNamespace(type="tool_use", id=f"toolu_{uuid.uuid4().hex[:8]}", name=tool_name, input=input_)


def _response(*blocks):
    return SimpleNamespace(content=list(blocks))


class ScriptedClaude:
    def __init__(self, turns: list):
        self._turns = list(turns)
        self.calls = 0

    async def __call__(self):
        self.calls += 1
        return self._turns.pop(0)


@pytest.mark.asyncio
async def test_session_survives_a_simulated_restart(tmp_path, monkeypatch):
    from app import config

    monkeypatch.setattr(config, "SESSIONS_DIR", tmp_path / "sessions")
    db_path = tmp_path / "test.db"
    await db.init_db(db_path)

    sm1 = SessionManager()
    design_session = await sm1.start_session(
        goal="Draw a single point named A",
        measurements_path=None,
        step_limit=5,
        autorun=False,
        model="claude-haiku-4-5",
    )
    session_id = design_session.session_id
    assert design_session.agent.model == "claude-haiku-4-5"

    first_run = ScriptedClaude(
        [
            _response(_tool_use("basePoint", name="A", x=0, y=0, draftBlock="front")),
            _response(_tool_use("pattern_complete", summary="Drew point A.")),
        ]
    )
    design_session.agent._call_claude = first_run  # type: ignore[method-assign]

    keep_going = True
    while keep_going:
        keep_going = await design_session.agent.run_step()

    assert design_session.agent.status == "complete"
    assert design_session.agent.stop_reason == "agent_complete"
    events_before_restart = list(design_session.history)
    assert len(events_before_restart) > 0

    # --- simulate a backend restart: close and reopen the same DB file ---
    await db.close_db()
    await db.init_db(db_path)

    sm2 = SessionManager()
    await sm2.load_from_db()

    reloaded = sm2.get(session_id)
    assert reloaded.agent.goal == "Draw a single point named A"
    assert reloaded.agent.model == "claude-haiku-4-5"
    assert reloaded.agent.status == "complete"
    assert reloaded.agent.stop_reason == "agent_complete"
    assert reloaded.agent.final_summary == "Drew point A."
    assert reloaded.agent.final_val_path is not None and reloaded.agent.final_val_path.exists()
    assert not reloaded.agent.actiond.is_alive  # cold placeholder, not a live subprocess
    assert len(reloaded.agent.messages) == len(design_session.agent.messages)
    assert_valid_message_history(reloaded.agent.messages)

    # The event history (what the frontend timeline replays on WS connect) survived too.
    assert [e["type"] for e in reloaded.history] == [e["type"] for e in events_before_restart]

    # The sessions list (GET /api/sessions) must see it immediately, pre-any-interaction.
    listed = {s["sessionId"]: s for s in sm2.list_sessions()}
    assert session_id in listed
    assert listed[session_id]["status"] == "complete"

    # And it must actually still be usable: chat should revive actiond from the
    # persisted final.val and keep building on the same geometry.
    second_run = ScriptedClaude(
        [
            _response(_tool_use("endLine", name="B", basePoint="A", angle="0", length="25")),
            _response(_tool_use("pattern_complete", summary="Added point B.")),
        ]
    )
    reloaded.agent._call_claude = second_run  # type: ignore[method-assign]

    await sm2.send_message(session_id, "Add a point B 25mm to the right of A.", auto_resume=False)
    keep_going = True
    while keep_going:
        keep_going = await reloaded.agent.run_step()

    assert reloaded.agent.status == "complete"
    assert reloaded.agent.final_summary == "Added point B."
    assert_valid_message_history(reloaded.agent.messages)

    await reloaded.agent.actiond.close()
    await db.close_db()


@pytest.mark.asyncio
async def test_a_session_running_at_restart_reloads_as_paused(tmp_path, monkeypatch):
    """A session whose last known status was "running" (backend went down mid-turn)
    must not come back stuck showing "running" forever with no run_task ever going to
    advance it -- it should reload as "paused" so Step/Run/chat can pick it up."""
    from app import config

    monkeypatch.setattr(config, "SESSIONS_DIR", tmp_path / "sessions")
    db_path = tmp_path / "test2.db"
    await db.init_db(db_path)

    sm1 = SessionManager()
    design_session = await sm1.start_session(
        goal="Draw something", measurements_path=None, step_limit=5, autorun=False
    )
    session_id = design_session.session_id

    # Manually persist a "running" snapshot, as if the process died mid-call.
    design_session.agent.status = "running"
    await db.upsert_session(design_session.db_row())
    await db.close_db()

    await db.init_db(db_path)
    sm2 = SessionManager()
    await sm2.load_from_db()

    assert sm2.get(session_id).agent.status == "paused"

    await db.close_db()
