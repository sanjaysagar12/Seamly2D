"""Milestone 3/4 verification: drive AgentSession through a scripted sequence of
canned "Claude" turns (basePoint -> endLine -> line -> pattern.complete), with
_call_claude mocked out so this test needs no live Anthropic credentials/network.
Verifies the loop mechanics: exactly one actiond call per turn, a snapshot + a
checkpoint save after every successful action, error recovery on a bad call, and a
clean stop (with a saved final.val) on pattern.complete.
"""
import sys
import uuid
from pathlib import Path
from types import SimpleNamespace

sys.path.insert(0, str(Path(__file__).resolve().parent.parent))

import pytest

from app import config
from app.actiond_process import ActiondSession
from app.agent_loop import AgentSession
from app.tool_schema import load_tool_catalog


def _tool_use(tool_name: str, **input_):
    return SimpleNamespace(type="tool_use", id=f"toolu_{uuid.uuid4().hex[:8]}", name=tool_name, input=input_)


def _text(text: str):
    return SimpleNamespace(type="text", text=text)


def assert_valid_message_history(messages: list[dict]) -> None:
    """The real Anthropic API rejects a request where any assistant `tool_use` block
    isn't immediately followed by a matching `tool_result` in the very next message
    (400 invalid_request_error) -- this only ever surfaces on a live call, so this
    checks the same invariant directly against a scripted/mocked transcript. Caught a
    real bug: pattern_complete (and the unrecoverable-failure path) used to end the
    turn without ever emitting a tool_result for their own tool_use block, which was
    invisible until a later chat message resent that same history to the real API.
    """
    for i, message in enumerate(messages):
        if message["role"] != "assistant":
            continue
        content = message["content"]
        if isinstance(content, str):
            continue
        tool_use_ids = {b["id"] if isinstance(b, dict) else b.id for b in content if _block_type(b) == "tool_use"}
        if not tool_use_ids:
            continue
        assert i + 1 < len(messages), f"assistant message {i} has tool_use with no following message at all"
        next_content = messages[i + 1]["content"]
        assert not isinstance(next_content, str), f"message {i + 1} must contain tool_result blocks, got plain text"
        result_ids = {
            b["tool_use_id"] for b in next_content if (b.get("type") if isinstance(b, dict) else None) == "tool_result"
        }
        missing = tool_use_ids - result_ids
        assert not missing, f"tool_use id(s) {missing} from message {i} have no tool_result in message {i + 1}"


def _block_type(b) -> str:
    return b["type"] if isinstance(b, dict) else b.type


def _response(*blocks):
    return SimpleNamespace(content=list(blocks))


class ScriptedClaude:
    """Stands in for the real Anthropic call: yields one canned response per turn."""

    def __init__(self, turns: list):
        self._turns = list(turns)
        self.calls = 0

    async def __call__(self):
        self.calls += 1
        return self._turns.pop(0)


@pytest.mark.asyncio
async def test_full_loop_with_scripted_turns(tmp_path):
    catalog = await load_tool_catalog()

    output_dir = tmp_path
    actiond = ActiondSession(output_dir=output_dir)
    await actiond.start()

    events: list[dict] = []

    async def emit(event):
        events.append(event)

    agent = AgentSession(
        session_id="test-session",
        actiond=actiond,
        client=None,  # never touched -- _call_claude is monkeypatched below
        tools=catalog["anthropic_tools"],
        op_metadata=catalog["op_metadata"],
        name_map=catalog["name_map"],
        output_dir=output_dir,
        goal="Draw a single line between two points",
        emit=emit,
        step_limit=10,
    )

    scripted = ScriptedClaude(
        [
            _response(_text("I'll place the first point."), _tool_use(
                "basePoint", name="A", x=0, y=0, draftBlock="front"
            )),
            _response(_text("Now a second point 50mm to the right."), _tool_use(
                "endLine", name="B", basePoint="A", angle="0", length="50"
            )),
            # Deliberately malformed -- references a point that doesn't exist -- to
            # exercise the recover-and-retry path before the real line call.
            _response(_tool_use("line", firstPoint="A", secondPoint="NoSuchPoint")),
            _response(_tool_use("line", firstPoint="A", secondPoint="B")),
            # Real Claude calls the sanitized name from the tools list ("pattern.complete"
            # isn't a valid Anthropic tool name -- dots aren't allowed -- see tool_schema.py).
            _response(_tool_use("pattern_complete", summary="Drew A-B as requested.")),
        ]
    )
    agent._call_claude = scripted  # type: ignore[method-assign]

    await agent.initialize()
    assert agent.status == "paused"

    keep_going = True
    turns = 0
    while keep_going:
        keep_going = await agent.run_step()
        turns += 1
        assert turns <= 10, "loop did not terminate"

    assert agent.status == "complete"
    assert agent.stop_reason == "agent_complete"
    assert agent.final_summary == "Drew A-B as requested."
    assert agent.final_val_path is not None
    assert agent.final_val_path.exists()
    assert scripted.calls == 5
    assert_valid_message_history(agent.messages)

    event_types = [e["type"] for e in events]
    assert event_types.count("action_started") == 4  # basePoint, endLine, line(bad), line(good)
    action_results = [e for e in events if e["type"] == "action_result"]
    assert [r["success"] for r in action_results] == [True, True, False, True, True]  # last True = pattern.complete
    assert any(e["type"] == "session_complete" for e in events)

    snapshot_events = [e for e in events if e["type"] == "snapshot_ready"]
    # initial (step 0) + basePoint + endLine + line(good) = 4 (the failed `line` call
    # never reaches actiond, so it produces no snapshot)
    assert len(snapshot_events) == 4

    checkpoint = output_dir / "checkpoint.val"
    assert checkpoint.exists()


@pytest.mark.asyncio
async def test_actiond_level_failure_does_not_attach_image_to_error_tool_result(tmp_path):
    """Regression test: unlike a schema-validation failure (which returns before ever
    calling actiond), an actiond-level failure -- e.g. a well-formed call referencing a
    point name that doesn't exist -- still falls through to _execute_tool's
    render.snapshot call afterward. The real Anthropic API rejects a tool_result
    outright if is_error is true and its content contains anything but text blocks
    (400 "all content must be type `text` if `is_error` is true"), so that snapshot
    must never be attached when the action itself failed.
    """
    catalog = await load_tool_catalog()
    output_dir = tmp_path
    actiond = ActiondSession(output_dir=output_dir)
    await actiond.start()

    events: list[dict] = []

    async def emit(event):
        events.append(event)

    agent = AgentSession(
        session_id="test-session-error-snapshot",
        actiond=actiond,
        client=None,
        tools=catalog["anthropic_tools"],
        op_metadata=catalog["op_metadata"],
        name_map=catalog["name_map"],
        output_dir=output_dir,
        goal="Draw a line from a point that doesn't exist",
        emit=emit,
        step_limit=10,
    )

    scripted = ScriptedClaude(
        [
            _response(_text("First, a real point."), _tool_use(
                "basePoint", name="A", x=0, y=0, draftBlock="front"
            )),
            # Passes schema validation (well-formed strings) but fails at actiond
            # itself -- a genuine per-action runtime error, not a validation error.
            _response(_tool_use("endLine", name="B", basePoint="NoSuchPoint", angle="0", length="10")),
            _response(_tool_use("pattern_complete", summary="Stopping after the failed call.")),
        ]
    )
    agent._call_claude = scripted  # type: ignore[method-assign]

    await agent.initialize()
    keep_going = True
    turns = 0
    while keep_going:
        keep_going = await agent.run_step()
        turns += 1
        assert turns <= 10, "loop did not terminate"

    assert_valid_message_history(agent.messages)

    action_results = [e for e in events if e["type"] == "action_result"]
    assert [r["success"] for r in action_results] == [True, False, True]

    # Find the tool_result for the failed endLine call and assert it is text-only.
    error_tool_results = [
        block
        for message in agent.messages
        if isinstance(message.get("content"), list)
        for block in message["content"]
        if isinstance(block, dict) and block.get("type") == "tool_result" and block.get("is_error")
    ]
    assert error_tool_results, "expected at least one is_error tool_result in the transcript"
    for result in error_tool_results:
        content = result["content"]
        if isinstance(content, str):
            continue
        assert all(block["type"] == "text" for block in content), (
            f"is_error tool_result must be text-only, got block types "
            f"{[block['type'] for block in content]}"
        )


@pytest.mark.asyncio
async def test_creating_a_piece_switches_current_piece_and_adds_a_closeup_snapshot(tmp_path):
    """Multi-piece regression: piece.addPatternPiece must set AgentSession.current_piece,
    and every successful action from then on must carry a *second* image (a target="piece"
    close-up of that piece, alongside the usual whole-draft snapshot) so the agent can
    actually see the piece it just created/switched to, not just the draft canvas."""
    catalog = await load_tool_catalog()
    output_dir = tmp_path
    actiond = ActiondSession(output_dir=output_dir)
    await actiond.start()

    events: list[dict] = []

    async def emit(event):
        events.append(event)

    agent = AgentSession(
        session_id="test-session-piece",
        actiond=actiond,
        client=None,
        tools=catalog["anthropic_tools"],
        op_metadata=catalog["op_metadata"],
        name_map=catalog["name_map"],
        output_dir=output_dir,
        goal="Draft a small triangular piece named Front",
        emit=emit,
        step_limit=10,
    )

    scripted = ScriptedClaude(
        [
            _response(_tool_use("basePoint", name="A", x=0, y=0, draftBlock="front")),
            _response(_tool_use("endLine", name="B", basePoint="A", angle="0", length="50")),
            _response(_tool_use("endLine", name="C", basePoint="A", angle="90", length="50")),
            _response(_tool_use(
                "piece_addPatternPiece", name="Front", nodes=["A", "B", "C"], seamAllowanceWidth="10"
            )),
            _response(_tool_use("pattern_complete", summary="Created the Front piece.")),
        ]
    )
    agent._call_claude = scripted  # type: ignore[method-assign]

    assert agent.current_piece is None

    await agent.initialize()
    keep_going = True
    turns = 0
    while keep_going:
        keep_going = await agent.run_step()
        turns += 1
        assert turns <= 10, "loop did not terminate"

    assert agent.status == "complete"
    assert agent.current_piece == "Front"
    assert_valid_message_history(agent.messages)

    piece_events = [e for e in events if e["type"] == "piece_snapshot_ready"]
    assert piece_events, "expected at least one piece_snapshot_ready event once a piece existed"
    assert all(e["piece"] == "Front" for e in piece_events)

    # Find the tool_result for the piece.addPatternPiece call itself and confirm it
    # carries two images (draft + piece close-up), not just one.
    add_piece_tool_use_id = None
    for message in agent.messages:
        content = message.get("content")
        if message.get("role") != "assistant" or isinstance(content, str):
            continue
        for block in content:
            if _block_type(block) == "tool_use" and block.name == "piece_addPatternPiece":
                add_piece_tool_use_id = block.id
    assert add_piece_tool_use_id is not None

    result_content = None
    for message in agent.messages:
        content = message.get("content")
        if not isinstance(content, list):
            continue
        for block in content:
            if isinstance(block, dict) and block.get("tool_use_id") == add_piece_tool_use_id:
                result_content = block["content"]
    assert isinstance(result_content, list)
    image_blocks = [b for b in result_content if b["type"] == "image"]
    assert len(image_blocks) == 2, f"expected draft + piece close-up images, got {len(image_blocks)}"


@pytest.mark.asyncio
async def test_step_limit_stops_the_loop(tmp_path):
    catalog = await load_tool_catalog()
    output_dir = tmp_path
    actiond = ActiondSession(output_dir=output_dir)
    await actiond.start()

    events: list[dict] = []

    async def emit(event):
        events.append(event)

    agent = AgentSession(
        session_id="test-session-2",
        actiond=actiond,
        client=None,
        tools=catalog["anthropic_tools"],
        op_metadata=catalog["op_metadata"],
        name_map=catalog["name_map"],
        output_dir=output_dir,
        goal="Keep drawing points forever",
        emit=emit,
        step_limit=2,
    )

    counter = {"n": 0}

    async def infinite_points():
        counter["n"] += 1
        return _response(
            _tool_use("basePoint", name=f"P{counter['n']}", x=counter["n"], y=0, draftBlock=f"block{counter['n']}")
        )

    agent._call_claude = infinite_points  # type: ignore[method-assign]

    await agent.initialize()
    keep_going = True
    while keep_going:
        keep_going = await agent.run_step()

    assert agent.status == "complete"
    assert agent.stop_reason == "step_limit"
    assert agent.step == 2


@pytest.mark.asyncio
async def test_chat_message_revives_a_completed_session(tmp_path):
    """The "chat to edit the pattern" flow: a session runs to pattern_complete
    (closing actiond, per _finish()), then a follow-up user message is injected --
    this must revive actiond from the saved final.val (not lose the prior geometry),
    accept a new scripted turn that builds on it, and be able to complete again."""
    catalog = await load_tool_catalog()
    output_dir = tmp_path
    actiond = ActiondSession(output_dir=output_dir)
    await actiond.start()

    events: list[dict] = []

    async def emit(event):
        events.append(event)

    agent = AgentSession(
        session_id="test-session-3",
        actiond=actiond,
        client=None,
        tools=catalog["anthropic_tools"],
        op_metadata=catalog["op_metadata"],
        name_map=catalog["name_map"],
        output_dir=output_dir,
        goal="Draw a single point",
        emit=emit,
        step_limit=5,
    )

    first_run = ScriptedClaude(
        [
            _response(_tool_use("basePoint", name="A", x=0, y=0, draftBlock="front")),
            _response(_tool_use("pattern_complete", summary="Drew point A.")),
        ]
    )
    agent._call_claude = first_run  # type: ignore[method-assign]

    await agent.initialize()
    keep_going = True
    while keep_going:
        keep_going = await agent.run_step()

    assert agent.status == "complete"
    assert agent.stop_reason == "agent_complete"
    assert not agent.actiond.is_alive  # _finish() closes actiond
    original_step_limit = agent.step_limit
    # Validate *before* injecting the next message -- this is exactly the state a
    # revived session's history starts from, so any orphaned tool_use here is what
    # would make the real API reject the next request with a 400.
    assert_valid_message_history(agent.messages)

    # Now the user asks for an edit via chat.
    await agent.inject_user_message("Also add a point B 50mm to the right of A.")

    assert agent.status == "paused"
    assert agent.stop_reason is None
    assert agent.step_limit == original_step_limit + config.EXTRA_STEPS_PER_MESSAGE
    assert agent.actiond.is_alive
    user_message_events = [e for e in events if e["type"] == "user_message"]
    assert len(user_message_events) == 1
    assert user_message_events[0]["text"] == "Also add a point B 50mm to the right of A."

    # The revived actiond session must still have point A from before the "close".
    dump = await agent.actiond.run_single("pattern.dump")
    assert dump["status"] == "ok"
    assert [o["name"] for o in dump["result"]["objects"]] == ["A"]

    second_run = ScriptedClaude(
        [
            _response(_tool_use("endLine", name="B", basePoint="A", angle="0", length="50")),
            _response(_tool_use("pattern_complete", summary="Added point B.")),
        ]
    )
    agent._call_claude = second_run  # type: ignore[method-assign]

    keep_going = True
    while keep_going:
        keep_going = await agent.run_step()

    assert agent.status == "complete"
    assert agent.stop_reason == "agent_complete"
    assert agent.final_summary == "Added point B."
    assert_valid_message_history(agent.messages)


def test_haiku_omits_thinking_and_effort_kwargs():
    """claude-haiku-4-5 doesn't support thinking:{"type":"adaptive"} or
    output_config.effort -- sending either returns a 400. Every other selectable
    model wants both."""
    assert AgentSession._thinking_and_effort_kwargs("claude-haiku-4-5") == {}

    for model in ("claude-sonnet-5", "claude-sonnet-4-6", "claude-opus-5"):
        kwargs = AgentSession._thinking_and_effort_kwargs(model)
        assert kwargs["thinking"] == {"type": "adaptive", "display": "summarized"}
        assert kwargs["output_config"] == {"effort": config.ANTHROPIC_EFFORT}


def test_selectable_models_are_internally_consistent():
    # Deliberately does NOT assert config.ANTHROPIC_MODEL is one of these: that value
    # is resolved from this developer's own environment/.env at import time (see
    # config.py's load_dotenv()) and legitimately can be set to something outside the
    # frontend's curated dropdown for local testing -- asserting on it here would make
    # this test's pass/fail depend on whoever's machine runs it.
    assert "claude-haiku-4-5" in config.SELECTABLE_MODELS
    assert "claude-sonnet-5" in config.SELECTABLE_MODELS
    assert "claude-sonnet-4-6" in config.SELECTABLE_MODELS
