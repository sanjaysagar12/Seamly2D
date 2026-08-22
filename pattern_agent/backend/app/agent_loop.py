"""The self-verifying agent loop: Claude picks exactly one actiond action per turn,
we execute it, re-render the pattern, and feed the new image + a result summary back
as that turn's tool_result -- so the next turn's reasoning is grounded in an actual
look at the current draft, not just the text description of what happened.
"""
from __future__ import annotations

import base64
import logging
from pathlib import Path
from typing import Any, Awaitable, Callable

import anthropic

from . import config, events
from .actiond_process import ActiondCrashError, ActiondSession, ActiondTimeoutError
from .tool_schema import PATTERN_COMPLETE_OP
from .validation import validate_tool_input

logger = logging.getLogger("pattern_agent.agent_loop")

EmitFn = Callable[[dict[str, Any]], Awaitable[None]]

# Every render.snapshot call below passes both of these explicitly. Leaving width/height
# unset (as this used to) makes actiond's render.snapshot handler render at a 1:1
# scene-unit-to-pixel mapping, capped only at 4096px on its longer side (see
# scene_render_geometry.cpp's own memory-safety cap -- a general-purpose default, not an
# Anthropic-specific one, and this Python layer is the wrong place to touch that C++ code
# per this app's own "does not modify actiond" boundary, see README.md). A pattern that grows
# large enough over a long session pushes past Anthropic's *separate*, stricter constraint on
# multi-image requests: once a single request carries "many" images (every turn appends one to
# self.messages, so any non-trivial session gets there fast), every image in it must be <=2000px
# per dimension or the whole call is rejected with a 400 ("exceed max allowed size for
# many-image requests") -- which surfaces to the user as a hard session-ending API error with no
# actionable fix from their side. Passing both dimensions explicitly (not just one) is
# deliberate: only one dimension is still derived from the pattern's own aspect ratio by
# scene_render_geometry.cpp, so an unusually tall or wide piece could still push the *other*
# dimension over 2000px. Fixing both bounds the image regardless of the pattern's shape, at the
# cost of allowing non-uniform scaling (Qt::IgnoreAspectRatio) for extreme aspect ratios -- a
# mild visual stretch the agent can still read, versus a hard failure it cannot recover from.
SNAPSHOT_WIDTH = 1600
SNAPSHOT_HEIGHT = 1600

SYSTEM_PROMPT = """\
You are a pattern-drafting agent operating Seamly2D headlessly through a fixed set of \
construction tools (points, lines, curves, operations, pieces). You work through a \
single user-stated goal by calling exactly one tool per turn, then looking at the \
resulting rendered image before deciding your next move.

Hard rules:
- Call exactly one tool per turn. Never reason about multiple future steps as if you \
could do them all now -- decide only the next single action.
- Always look at the most recent snapshot image before choosing an action. It is the \
ground truth for what currently exists in the pattern -- more reliable than your own \
memory of prior steps.
- Measurements (if any were provided) are already loaded into the pattern before your \
first turn. Call `pattern_listMeasurements` if you want to see the exact names/values \
available before writing formulas that reference them (e.g. "bust_circ/4+20").
- Point/line/curve tools take an explicit `name` you choose; reference existing objects \
by the exact name you gave them. Use short, readable names (A, A1, A2, B1...) so the \
construction history stays legible.
- `basePoint` starts a brand-new draft block and must only be called once per block -- \
every other point in that block should hang off it via `endLine`/`alongLine`/`normal`/\
etc. (formula-bearing points) or `line`/`spline`/... (edges between existing points).
- Formula fields (`length`, `angle`, etc.) are strings evaluated by Seamly2D's own \
formula engine -- they can be literal numbers ("10"), arithmetic ("bust_circ/4+20"), or \
reference measurement names directly. Literal `x`/`y`/`mx`/`my` fields are plain numbers, \
not formulas.
- If a tool call fails, you'll get back a structured error (type + message, and for a \
name error, the list of names that do exist). Read it, fix the actual problem, and \
retry with corrected arguments on your next turn -- don't repeat the same failing call.
- The pattern is snapshotted and checkpointed to disk automatically after every action \
you take; you do not need to (and cannot) call render/save/close tools yourself.
- When the piece/pattern described in the goal is genuinely complete -- judge this from \
the actual rendered image, not just from having called "enough" tools -- call \
`pattern_complete` with a short justification. This is the only way to end the session \
successfully; do not just stop calling tools.
- Work efficiently: prefer the direct construction sequence a human patternmaker would \
use over unnecessary exploratory or read-only calls.
"""


class AgentSessionError(Exception):
    pass


class AgentSession:
    def __init__(
        self,
        session_id: str,
        actiond: ActiondSession,
        client: anthropic.AsyncAnthropic,
        tools: list[dict[str, Any]],
        op_metadata: dict[str, Any],
        name_map: dict[str, str],
        output_dir: Path,
        goal: str,
        emit: EmitFn,
        step_limit: int = config.DEFAULT_STEP_LIMIT,
        model: str = config.ANTHROPIC_MODEL,
        system_prompt: str | None = None,
    ):
        self.session_id = session_id
        self.actiond = actiond
        self.client = client
        self.tools = tools
        self.op_metadata = op_metadata
        self.name_map = name_map
        self.output_dir = output_dir
        self.goal = goal
        self.emit = emit
        self.step_limit = step_limit
        self.model = model
        # Instance attribute (not just a reference to the module constant) so
        # session_manager.update_settings() can override it per session at runtime, same as
        # self.model/self.client -- see that method's docstring for why none of the three are
        # persisted to the database.
        self.system_prompt = system_prompt or SYSTEM_PROMPT

        self.messages: list[dict[str, Any]] = []
        self.step = 0
        self.status = "idle"  # idle | running | paused | complete | error
        self.stop_reason: str | None = None
        self.final_summary: str | None = None
        self.final_val_path: Path | None = None
        self._stop_requested = False
        self._crash_recoveries = 0
        self._message_count = 0

    async def initialize(self) -> None:
        initial_image_block = await self._try_render_initial_snapshot()

        goal_text = (
            f"Goal: {self.goal}\n\n"
            "Here is the current state of the pattern (a blank/near-empty draft if this "
            "is a fresh session). Begin by reasoning about the first concrete action, "
            "then call exactly one tool."
        )
        content: list[dict[str, Any]] = [{"type": "text", "text": goal_text}]
        if initial_image_block is not None:
            content.append(initial_image_block)
        self.messages.append({"role": "user", "content": content})
        self.status = "paused"
        await self.emit(events.status_changed(self.session_id, "paused"))

    async def _try_render_initial_snapshot(self) -> dict[str, Any] | None:
        try:
            snap = await self.actiond.run_single(
                "render.snapshot", path="step_0000.png", showPointNames=True,
                width=SNAPSHOT_WIDTH, height=SNAPSHOT_HEIGHT,
            )
        except (ActiondCrashError, ActiondTimeoutError) as exc:
            logger.warning("Initial snapshot failed: %s", exc)
            return None
        if snap["status"] != "ok":
            return None
        image_path = Path(snap["result"]["path"])
        await self.emit(events.snapshot_ready(self.session_id, 0, self._url_for(image_path)))
        return self._image_block(image_path)

    def _url_for(self, path: Path) -> str:
        return f"/files/{self.session_id}/{path.name}"

    @staticmethod
    def _image_block(path: Path) -> dict[str, Any]:
        data = base64.standard_b64encode(path.read_bytes()).decode("ascii")
        media_type = "image/png" if path.suffix.lower() == ".png" else "image/jpeg"
        return {
            "type": "image",
            "source": {"type": "base64", "media_type": media_type, "data": data},
        }

    def request_stop(self) -> None:
        self._stop_requested = True

    async def run_step(self) -> bool:
        """Runs exactly one agent turn. Returns True if the session should keep
        going, False if it just reached a terminal state (complete/error/limit)."""
        if self.status in ("complete", "error"):
            return False

        if self.step >= self.step_limit:
            await self._finish("step_limit", summary=None)
            return False

        await self._ensure_actiond_alive()

        self.status = "running"
        await self.emit(events.status_changed(self.session_id, "running"))

        try:
            response = await self._call_claude()
        except Exception as exc:
            # Covers anthropic.APIError (rate limits, refusals, real API failures) as
            # well as errors the SDK raises before a request is even sent -- e.g. a
            # plain TypeError from _validate_headers() when no credentials resolve at
            # all (no ANTHROPIC_API_KEY, no ANTHROPIC_AUTH_TOKEN, no `ant auth login`
            # profile). Both are "the Claude call didn't happen" failures from the
            # user's point of view and deserve the same clear, actionable surfacing.
            if isinstance(exc, anthropic.APIError):
                message = f"Anthropic API error: {exc}"
            else:
                message = f"Could not call the Anthropic API: {exc}"
            await self.emit(events.error(self.session_id, message, fatal=True))
            self.status = "error"
            self.stop_reason = "api_error"
            try:
                await self.actiond.close()
            except Exception:
                pass
            return False

        self.messages.append({"role": "assistant", "content": response.content})

        tool_use_blocks = [b for b in response.content if b.type == "tool_use"]
        if not tool_use_blocks:
            # tool_choice={"type":"any"} should make this unreachable, but stay
            # defensive: nudge and let the step-limit bound the retries.
            self.step += 1
            self.messages.append(
                {
                    "role": "user",
                    "content": "You must call exactly one tool (a construction action, "
                    "an introspection op, or pattern_complete). Try again.",
                }
            )
            await self.emit(events.step_complete(self.session_id, self.step))
            if self._stop_requested:
                await self._finish("user_stopped", summary=None)
                return False
            self.status = "paused"
            await self.emit(events.status_changed(self.session_id, "paused"))
            return True

        primary = tool_use_blocks[0]
        extra_results = [
            {
                "type": "tool_result",
                "tool_use_id": b.id,
                "content": "Skipped: only one tool call is allowed per turn. "
                "This call was ignored -- retry it alone on a future turn if still needed.",
                "is_error": True,
            }
            for b in tool_use_blocks[1:]
        ]

        self.step += 1
        is_complete_call = self.name_map.get(primary.name, primary.name) == PATTERN_COMPLETE_OP

        try:
            if is_complete_call:
                result_block = await self._handle_pattern_complete(primary)
            else:
                result_block = await self._execute_tool(primary)
        except Exception as exc:
            # An unrecoverable failure below the per-crash recovery path (e.g. actiond
            # itself could not be respawned after a crash) -- surface it as a clean
            # terminal error instead of an unhandled exception reaching the caller.
            # Still close out primary's tool_use with a tool_result (even though this
            # session is ending) -- a later chat message can revive a "complete"/"error"
            # session, resending this same history to the API, which rejects any
            # tool_use left without a matching tool_result in the very next message.
            logger.exception("Unrecoverable failure executing %s", primary.name)
            await self.emit(events.error(self.session_id, f"Unrecoverable error: {exc}", fatal=True, step=self.step))
            self.messages.append(
                {
                    "role": "user",
                    "content": [
                        {
                            "type": "tool_result",
                            "tool_use_id": primary.id,
                            "content": f"Unrecoverable orchestrator error: {exc}",
                            "is_error": True,
                        },
                        *extra_results,
                    ],
                }
            )
            self.status = "error"
            self.stop_reason = self.stop_reason or "internal_error"
            try:
                await self.actiond.close()
            except Exception:
                pass
            return False

        self.messages.append({"role": "user", "content": [result_block, *extra_results]})

        if is_complete_call:
            summary = (primary.input or {}).get("summary", "(no summary provided)")
            await self._finish("agent_complete", summary=summary)
            return False

        await self.emit(events.step_complete(self.session_id, self.step))

        if self._stop_requested:
            await self._finish("user_stopped", summary=None)
            return False

        self.status = "paused"
        await self.emit(events.status_changed(self.session_id, "paused"))
        return True

    @staticmethod
    def _block_type(block: Any) -> str:
        return block["type"] if isinstance(block, dict) else block.type

    @staticmethod
    def _block_id(block: Any) -> str:
        return block["id"] if isinstance(block, dict) else block.id

    def _repair_orphaned_tool_uses(self) -> None:
        """Defensive guard, called right before every real API call: if the last
        message is an assistant turn ending in tool_use block(s) with no matching
        tool_result appended after them, the Anthropic API rejects the request outright
        (400 "tool_use ids were found without tool_result blocks"). Every code path
        that appends a tool_use is supposed to append its tool_result before control
        returns to the caller -- this has already been the source of one real bug
        (pattern_complete's own tool_use went unclosed) -- so this is cheap insurance
        against a path that hasn't been found yet, not a substitute for closing tool
        calls out properly at the source.
        """
        if not self.messages:
            return
        last = self.messages[-1]
        if last.get("role") != "assistant":
            return
        content = last["content"]
        if isinstance(content, str):
            return
        orphaned = [self._block_id(b) for b in content if self._block_type(b) == "tool_use"]
        if not orphaned:
            return
        logger.warning(
            "Repairing %d orphaned tool_use block(s) before next API call: %s", len(orphaned), orphaned
        )
        self.messages.append(
            {
                "role": "user",
                "content": [
                    {
                        "type": "tool_result",
                        "tool_use_id": tool_use_id,
                        "content": "(no result was recorded for this tool call -- treat it as failed and retry)",
                        "is_error": True,
                    }
                    for tool_use_id in orphaned
                ],
            }
        )

    @staticmethod
    def _thinking_and_effort_kwargs(model: str) -> dict[str, Any]:
        """claude-haiku-4-5 is the odd one out among SELECTABLE_MODELS: it doesn't
        support `thinking: {"type": "adaptive"}` or `output_config.effort` the way
        Opus/Sonnet 5 and Sonnet 4.6 do -- sending either returns a 400. Simplest
        correct behavior is to just run Haiku without extended thinking rather than
        reach for its older budget_tokens-style config."""
        if model == "claude-haiku-4-5":
            return {}
        return {
            "thinking": {"type": "adaptive", "display": "summarized"},
            "output_config": {"effort": config.ANTHROPIC_EFFORT},
        }

    async def _call_claude(self):
        self._repair_orphaned_tool_uses()
        step_for_events = self.step + 1
        async with self.client.messages.stream(
            model=self.model,
            max_tokens=config.ANTHROPIC_MAX_TOKENS,
            system=self.system_prompt,
            tools=self.tools,
            tool_choice={"type": "any", "disable_parallel_tool_use": True},
            messages=self.messages,
            **self._thinking_and_effort_kwargs(self.model),
        ) as stream:
            async for event in stream:
                if event.type == "content_block_delta":
                    if event.delta.type == "thinking_delta":
                        await self.emit(
                            events.thinking_delta(self.session_id, step_for_events, event.delta.thinking)
                        )
                    elif event.delta.type == "text_delta":
                        await self.emit(
                            events.text_delta(self.session_id, step_for_events, event.delta.text)
                        )
            return await stream.get_final_message()

    async def _execute_tool(self, tool_use) -> dict[str, Any]:
        # tool_use.name is the sanitized name Claude actually called (dots -> "_",
        # since Anthropic tool names must match ^[a-zA-Z0-9_-]{1,128}$ -- see
        # tool_schema.py). Translate back to the real, dotted actiond op name for
        # everything that talks to actiond or looks up its schema.
        op_name = self.name_map.get(tool_use.name, tool_use.name)
        tool_input = tool_use.input or {}

        await self.emit(events.action_started(self.session_id, self.step, op_name, tool_input))

        schema = self.op_metadata.get(op_name, {}).get("input_schema")
        if schema is not None:
            error_msg = validate_tool_input(schema, tool_input)
            if error_msg is not None:
                await self.emit(
                    events.action_result(self.session_id, self.step, op_name, success=False, error=error_msg)
                )
                return {
                    "type": "tool_result",
                    "tool_use_id": tool_use.id,
                    "content": error_msg,
                    "is_error": True,
                }

        try:
            outcome = await self.actiond.run_single(op_name, **tool_input)
        except (ActiondCrashError, ActiondTimeoutError) as exc:
            return await self._recover_from_crash(tool_use, exc)

        success = outcome["status"] == "ok"
        await self.emit(
            events.action_result(
                self.session_id,
                self.step,
                op_name,
                success=success,
                result=outcome.get("result") if success else None,
                error=None if success else outcome.get("error"),
            )
        )

        image_block: dict[str, Any] | None = None
        snapshot_note = ""
        try:
            snap_name = f"step_{self.step:04d}.png"
            snap = await self.actiond.run_single(
                "render.snapshot", path=snap_name, showPointNames=True,
                width=SNAPSHOT_WIDTH, height=SNAPSHOT_HEIGHT,
            )
            if snap["status"] == "ok":
                image_path = Path(snap["result"]["path"])
                await self.emit(
                    events.snapshot_ready(self.session_id, self.step, self._url_for(image_path))
                )
                image_block = self._image_block(image_path)
            else:
                snapshot_note = f" (snapshot render failed: {snap.get('error')})"
        except (ActiondCrashError, ActiondTimeoutError) as exc:
            snapshot_note = f" (could not render snapshot after crash recovery: {exc})"

        try:
            await self.actiond.run_single("session.save", path="checkpoint.val")
        except (ActiondCrashError, ActiondTimeoutError):
            pass  # non-fatal; next successful action will try again

        if success:
            text = f"{op_name} succeeded. Result: {outcome.get('result')}.{snapshot_note}"
        else:
            text = f"{op_name} FAILED. Error: {outcome.get('error')}. Fix the arguments and retry.{snapshot_note}"

        content: list[dict[str, Any]] = [{"type": "text", "text": text}]
        # Anthropic rejects a tool_result outright if is_error is true and content
        # contains anything but text blocks -- so a failed action can only carry the
        # snapshot back to the model on its *next* successful action's tool_result,
        # not on this one.
        if image_block is not None and success:
            content.append(image_block)

        return {
            "type": "tool_result",
            "tool_use_id": tool_use.id,
            "content": content,
            "is_error": not success,
        }

    async def _ensure_actiond_alive(self) -> None:
        """Silently revives self.actiond from the last save if it isn't running --
        covers both a closed-and-completed session (chat revival) and a session that
        was reconstructed "cold" from SQLite on backend startup (see db.py /
        session_manager.load_from_db()), where self.actiond starts out as a never-
        started ActiondSession placeholder (is_alive is False for that the same way
        it is for a closed one, so this needs no special-casing for "cold" vs
        "closed"). Unlike _recover_from_crash, this never emits a crash/recovery
        message -- there was no crash, nothing was lost, so the agent shouldn't be
        told to avoid repeating an action it never actually attempted."""
        if self.actiond.is_alive:
            return
        revive_from = self.final_val_path or (self.output_dir / "checkpoint.val")
        await self._respawn_actiond(revive_from if revive_from.exists() else None)

    async def _respawn_actiond(self, pattern_path: Path | None) -> None:
        """(Re)spawns actiond from a saved .val, replacing self.actiond. Used both for
        post-crash recovery and for reviving a closed session (session was already
        finished/session.close'd) when the user sends a follow-up chat message."""
        measurements_path = self.actiond.measurements_path
        try:
            await self.actiond.close(graceful_timeout=2.0)
        except Exception:
            pass

        new_session = ActiondSession(
            output_dir=self.output_dir,
            pattern_path=pattern_path,
            measurements_path=measurements_path,
        )
        await new_session.start()
        self.actiond = new_session

    async def _recover_from_crash(self, tool_use, exc: Exception) -> dict[str, Any]:
        op_name = self.name_map.get(tool_use.name, tool_use.name)
        self._crash_recoveries += 1
        await self.emit(
            events.error(
                self.session_id,
                f"actiond crashed while running {op_name}: {exc}. "
                "Attempting to recover from the last checkpoint.",
                fatal=False,
                step=self.step,
            )
        )

        checkpoint = self.output_dir / "checkpoint.val"
        try:
            await self._respawn_actiond(checkpoint if checkpoint.exists() else None)
        except Exception as start_exc:
            await self.emit(
                events.error(
                    self.session_id,
                    f"Could not recover actiond after crash: {start_exc}",
                    fatal=True,
                    step=self.step,
                )
            )
            self.status = "error"
            self.stop_reason = "actiond_crash"
            raise

        error_text = (
            f"actiond crashed while executing {op_name} with input {tool_use.input!r}. "
            "The process has been restarted from the last saved checkpoint, so that action "
            "was NOT applied. Do not repeat the exact same call -- it's the likely cause of "
            "the crash. Try a different approach to this step."
        )
        return {
            "type": "tool_result",
            "tool_use_id": tool_use.id,
            "content": error_text,
            "is_error": True,
        }

    async def _handle_pattern_complete(self, tool_use) -> dict[str, Any]:
        summary = (tool_use.input or {}).get("summary", "(no summary provided)")
        await self.emit(
            events.action_result(self.session_id, self.step, "pattern.complete", success=True, result=summary)
        )
        # Every tool_use needs a matching tool_result in the very next message, or the
        # Anthropic API rejects the *next* request built from this history with a 400
        # -- and there will be a next request if a later chat message revives this
        # session. This one is never actually sent this turn (the session ends right
        # after), but it keeps self.messages valid for whenever it next is.
        return {
            "type": "tool_result",
            "tool_use_id": tool_use.id,
            "content": "Session marked complete.",
        }

    async def _finish(self, reason: str, summary: str | None) -> None:
        val_path = self.output_dir / "final.val"
        try:
            save_result = await self.actiond.run_single("session.save", path="final.val")
            if save_result["status"] == "ok":
                self.final_val_path = val_path
        except (ActiondCrashError, ActiondTimeoutError) as exc:
            logger.warning("Could not save final .val on session end: %s", exc)

        self.status = "complete"
        self.stop_reason = reason
        self.final_summary = summary

        val_url = f"/files/{self.session_id}/final.val" if self.final_val_path else None
        await self.emit(events.session_complete(self.session_id, reason, summary, val_url))

        try:
            await self.actiond.close()
        except Exception:
            pass

    async def inject_user_message(self, text: str) -> None:
        """Adds a new user instruction to an existing conversation and reopens the
        session for more turns -- the "chat to edit the pattern" entry point. Works
        whether the session is currently paused (mid-run, single-step mode) or already
        complete (actiond was closed in _finish() -- revived here from final.val /
        checkpoint.val, same mechanism as post-crash recovery)."""
        if self.status == "running":
            raise ValueError("Cannot send a message while the agent is actively running -- stop it first.")

        await self._ensure_actiond_alive()

        self._repair_orphaned_tool_uses()
        self._message_count += 1
        image_block: dict[str, Any] | None = None
        try:
            snap = await self.actiond.run_single(
                "render.snapshot", path=f"resume_{self._message_count:03d}.png", showPointNames=True,
                width=SNAPSHOT_WIDTH, height=SNAPSHOT_HEIGHT,
            )
            if snap["status"] == "ok":
                image_path = Path(snap["result"]["path"])
                await self.emit(events.snapshot_ready(self.session_id, self.step, self._url_for(image_path)))
                image_block = self._image_block(image_path)
        except (ActiondCrashError, ActiondTimeoutError) as exc:
            logger.warning("Could not render grounding snapshot before injecting message: %s", exc)

        content: list[dict[str, Any]] = [
            {
                "type": "text",
                "text": f"New instruction from the user: {text}\n\n"
                "This continues the same pattern -- keep every point/line/curve name "
                "already in use, and edit or extend the existing draft rather than "
                "starting over. Look at the current snapshot below before deciding "
                "your next action.",
            }
        ]
        if image_block is not None:
            content.append(image_block)
        self.messages.append({"role": "user", "content": content})

        self.step_limit += config.EXTRA_STEPS_PER_MESSAGE
        self.status = "paused"
        self.stop_reason = None
        self.final_summary = None

        await self.emit(events.user_message(self.session_id, text, self.step))
        await self.emit(events.status_changed(self.session_id, "paused"))
