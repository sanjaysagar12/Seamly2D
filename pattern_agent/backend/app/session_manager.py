"""Ties one actiond subprocess + one AgentSession + event fan-out together per
design session, and manages the set of active sessions."""
from __future__ import annotations

import asyncio
import logging
import re
import shutil
import tempfile
import uuid
from datetime import datetime, timezone
from pathlib import Path
from typing import Any

import anthropic

from . import config, db
from .actiond_process import ActiondSession
from .agent_loop import SNAPSHOT_HEIGHT, SNAPSHOT_WIDTH
from .agent_loop import SYSTEM_PROMPT as DEFAULT_SYSTEM_PROMPT
from .agent_loop import AgentSession
from .tool_schema import load_tool_catalog

_UNSAFE_FILENAME_CHARS = re.compile(r"[^A-Za-z0-9_-]+")

logger = logging.getLogger("pattern_agent.session_manager")


def _now() -> str:
    return datetime.now(timezone.utc).isoformat()


class SessionNotFoundError(Exception):
    pass


class DesignSession:
    def __init__(self, session_id: str, agent: AgentSession, created_at: str):
        self.session_id = session_id
        # No separate `actiond` reference here on purpose: AgentSession.actiond is the
        # single source of truth and gets reassigned in place on crash recovery /
        # chat-message revival (_respawn_actiond) -- a second reference here would go
        # stale the moment that happens, and shutdown() closing a stale reference
        # would leak the real live process.
        self.agent = agent
        self.created_at = created_at
        self.subscribers: set[asyncio.Queue] = set()
        self.run_task: asyncio.Task | None = None
        self.history: list[dict[str, Any]] = []  # replay buffer for late-joining subscribers

    async def broadcast(self, event: dict[str, Any]) -> None:
        self.history.append(event)
        if len(self.history) > 2000:
            self.history.pop(0)
        for queue in list(self.subscribers):
            await queue.put(event)

    def subscribe(self) -> asyncio.Queue:
        queue: asyncio.Queue = asyncio.Queue()
        self.subscribers.add(queue)
        return queue

    def unsubscribe(self, queue: asyncio.Queue) -> None:
        self.subscribers.discard(queue)

    def db_row(self) -> dict[str, Any]:
        agent = self.agent
        return {
            "session_id": self.session_id,
            "goal": agent.goal,
            "model": agent.model,
            "system_prompt": agent.system_prompt,
            "step_limit": agent.step_limit,
            "status": agent.status,
            "step": agent.step,
            "stop_reason": agent.stop_reason,
            "final_summary": agent.final_summary,
            "measurements_path": str(agent.actiond.measurements_path) if agent.actiond.measurements_path else None,
            "output_dir": str(agent.output_dir),
            "val_path": str(agent.final_val_path) if agent.final_val_path else None,
            "messages_json": db.serialize_messages(agent.messages),
            "created_at": self.created_at,
            "updated_at": _now(),
            "current_piece": agent.current_piece,
        }


class SessionManager:
    def __init__(self):
        self._sessions: dict[str, DesignSession] = {}
        self._tool_catalog: dict[str, Any] | None = None
        self._client: anthropic.AsyncAnthropic | None = None

    async def _get_tool_catalog(self) -> dict[str, Any]:
        if self._tool_catalog is None:
            self._tool_catalog = await load_tool_catalog()
        return self._tool_catalog

    def _get_client(self) -> anthropic.AsyncAnthropic:
        if self._client is None:
            if not config.ANTHROPIC_API_KEY:
                # Falls through to the SDK's own credential resolution (env var,
                # ANTHROPIC_AUTH_TOKEN, or an `ant auth login` profile) if unset here.
                logger.info("ANTHROPIC_API_KEY not set; falling back to the SDK's own credential resolution.")
                self._client = anthropic.AsyncAnthropic()
            else:
                key = config.ANTHROPIC_API_KEY
                masked = f"{key[:8]}...{key[-4:]} (len={len(key)})" if len(key) > 12 else "(too short to mask safely)"
                logger.info("Using ANTHROPIC_API_KEY from config: %s", masked)
                self._client = anthropic.AsyncAnthropic(api_key=key)
        return self._client

    def get(self, session_id: str) -> DesignSession:
        session = self._sessions.get(session_id)
        if session is None:
            raise SessionNotFoundError(session_id)
        return session

    def list_sessions(self) -> list[dict[str, Any]]:
        return [
            {
                "sessionId": s.session_id,
                "status": s.agent.status,
                "step": s.agent.step,
                "goal": s.agent.goal,
                "stopReason": s.agent.stop_reason,
                "model": s.agent.model,
            }
            for s in self._sessions.values()
        ]

    @staticmethod
    async def _persist_session(design_session: DesignSession, event: dict[str, Any] | None = None) -> None:
        """Persistence is always best-effort, never a hard dependency for the app's
        core session management -- e.g. the DB may not be initialized at all in a
        lightweight test harness that talks to the ASGI app directly without running
        FastAPI's lifespan, and that must not turn every session operation into a 500."""
        try:
            if event is not None:
                await db.append_event(design_session.session_id, len(design_session.history), event)
            await db.upsert_session(design_session.db_row())
        except Exception:
            logger.exception("Failed to persist state for session %s", design_session.session_id)

    def _make_emit(self, design_session_ref: dict[str, DesignSession]):
        """Returns an `emit` closure bound to a session that isn't constructed yet at
        the point the closure itself must be created (AgentSession needs `emit` before
        DesignSession can wrap it). `design_session_ref` is a one-item box the caller
        fills in with the real DesignSession right after constructing it."""

        async def emit(event: dict[str, Any]) -> None:
            design_session = design_session_ref["value"]
            await design_session.broadcast(event)
            await self._persist_session(design_session, event)

        return emit

    async def start_session(
        self,
        goal: str,
        measurements_path: Path | None,
        pattern_path: Path | None = None,
        step_limit: int = config.DEFAULT_STEP_LIMIT,
        autorun: bool = True,
        model: str | None = None,
        system_prompt: str | None = None,
        focus_piece: str | None = None,
    ) -> DesignSession:
        if model is not None and model not in config.SELECTABLE_MODELS:
            raise ValueError(
                f"Unknown model {model!r}; must be one of {sorted(config.SELECTABLE_MODELS)}"
            )
        resolved_model = model or config.ANTHROPIC_MODEL

        session_id = uuid.uuid4().hex[:12]
        output_dir = config.SESSIONS_DIR / session_id
        output_dir.mkdir(parents=True, exist_ok=True)

        catalog = await self._get_tool_catalog()

        actiond = ActiondSession(
            output_dir=output_dir,
            pattern_path=pattern_path,
            measurements_path=measurements_path,
        )
        await actiond.start()

        ref: dict[str, DesignSession] = {}
        agent = AgentSession(
            session_id=session_id,
            actiond=actiond,
            client=self._get_client(),
            tools=catalog["anthropic_tools"],
            op_metadata=catalog["op_metadata"],
            name_map=catalog["name_map"],
            output_dir=output_dir,
            goal=goal,
            emit=self._make_emit(ref),
            step_limit=step_limit,
            model=resolved_model,
            system_prompt=system_prompt,
            current_piece=focus_piece,
        )

        design_session = DesignSession(session_id, agent, created_at=_now())
        ref["value"] = design_session
        self._sessions[session_id] = design_session
        await self._persist_session(design_session)

        await agent.initialize()

        if autorun:
            design_session.run_task = asyncio.create_task(self._run_loop(design_session))

        return design_session

    async def list_pattern_pieces(self, pattern_path: Path) -> list[dict[str, Any]]:
        """Briefly loads a base pattern file into its own throwaway actiond process just
        to run piece.list -- lets the home page's pattern picker show which pieces (e.g.
        Front/Back/Sleeve) an uploaded .val/.sm2d already contains before any real
        session exists. Never touches self._sessions."""
        tmp_dir = Path(tempfile.mkdtemp(prefix="piece_preview_", dir=str(config.DATA_DIR)))
        actiond = ActiondSession(output_dir=tmp_dir, pattern_path=pattern_path)
        try:
            await actiond.start()
            outcome = await actiond.run_single("piece.list")
            if outcome["status"] != "ok":
                raise ValueError(outcome.get("error") or "piece.list failed")
            return outcome["result"]["pieces"]
        finally:
            try:
                await actiond.close(graceful_timeout=2.0)
            except Exception:
                pass
            shutil.rmtree(tmp_dir, ignore_errors=True)

    async def list_session_pieces(self, session_id: str) -> list[dict[str, Any]]:
        """Pieces that currently exist in a live (or revivable) session -- backs the
        session page's piece switcher."""
        agent = self.get(session_id).agent
        await agent._ensure_actiond_alive()
        outcome = await agent.actiond.run_single("piece.list")
        if outcome["status"] != "ok":
            raise ValueError(outcome.get("error") or "piece.list failed")
        return outcome["result"]["pieces"]

    async def render_piece_snapshot(self, session_id: str, piece: str) -> dict[str, Any]:
        """Renders a fresh, on-demand close-up of one piece -- used when the user
        switches which piece they're looking at in the session page, independent of
        whatever the agent loop's own automatic snapshots are doing."""
        agent = self.get(session_id).agent
        await agent._ensure_actiond_alive()

        # Path params always arrive as strings; actiond only treats a *JSON number* as a
        # literal piece id (a JSON string is always matched by name) -- so a purely
        # numeric piece name still round-trips correctly (matched by id, same as typing
        # it in a raw request), while a name like "Front" is matched by name as usual.
        piece_param: str | int = int(piece) if piece.isdigit() else piece
        safe = _UNSAFE_FILENAME_CHARS.sub("_", piece)[:80] or "piece"
        filename = f"piece_view_{safe}.png"
        outcome = await agent.actiond.run_single(
            "render.snapshot", path=filename, target="piece", piece=piece_param,
            showPointNames=True, width=SNAPSHOT_WIDTH, height=SNAPSHOT_HEIGHT,
        )
        if outcome["status"] != "ok":
            raise ValueError(outcome.get("error") or "render.snapshot failed")
        resolved_piece = outcome["result"].get("piece", piece)
        return {"piece": resolved_piece, "url": f"/files/{session_id}/{filename}"}

    async def load_from_db(self) -> None:
        """Reconstructs every persisted session on backend startup -- metadata, the
        agent's own conversation (so a chat message can pick up reasoning right where
        it left off), and the event history (so the frontend timeline replays exactly
        like it does for a live session's late WebSocket subscriber). Does NOT spawn an
        actiond process for each one up front -- `agent.actiond` starts as a
        never-started placeholder (is_alive is False, same as a closed one) and
        transparently revives on first use via AgentSession._ensure_actiond_alive(),
        exactly like post-crash / chat recovery already does."""
        catalog = await self._get_tool_catalog()
        rows = await db.load_all_sessions()
        for row in rows:
            session_id = row["session_id"]
            output_dir = Path(row["output_dir"])
            measurements_path = Path(row["measurements_path"]) if row["measurements_path"] else None

            # Cold placeholder -- never started, so is_alive is False until something
            # actually needs it (Step/Run/chat), same trigger as a closed session.
            actiond = ActiondSession(
                output_dir=output_dir,
                pattern_path=None,
                measurements_path=measurements_path,
            )

            ref: dict[str, DesignSession] = {}
            agent = AgentSession(
                session_id=session_id,
                actiond=actiond,
                client=self._get_client(),
                tools=catalog["anthropic_tools"],
                op_metadata=catalog["op_metadata"],
                name_map=catalog["name_map"],
                output_dir=output_dir,
                goal=row["goal"],
                emit=self._make_emit(ref),
                step_limit=row["step_limit"],
                model=row["model"] or config.ANTHROPIC_MODEL,
                system_prompt=row["system_prompt"],
                current_piece=row.get("current_piece"),
            )
            agent.messages = db.deserialize_messages(row["messages_json"])
            agent.step = row["step"]
            agent.stop_reason = row["stop_reason"]
            agent.final_summary = row["final_summary"]
            agent.final_val_path = Path(row["val_path"]) if row["val_path"] else None
            # A session that was actively "running" when the backend went down has no
            # live run_task to resume it -- surface it as paused (revivable via
            # Step/Run/chat) rather than stuck showing "running" forever.
            agent.status = row["status"] if row["status"] in ("complete", "error") else "paused"

            design_session = DesignSession(session_id, agent, created_at=row["created_at"])
            ref["value"] = design_session
            design_session.history = await db.load_events(session_id)
            self._sessions[session_id] = design_session

        if rows:
            logger.info("Loaded %d session(s) from %s", len(rows), config.DB_PATH)

    async def _run_loop(self, design_session: DesignSession) -> None:
        agent = design_session.agent
        try:
            while True:
                should_continue = await agent.run_step()
                if not should_continue:
                    break
                if agent._stop_requested:
                    break
        except Exception:
            logger.exception("Agent loop crashed for session %s", design_session.session_id)
            agent.status = "error"
            agent.stop_reason = "internal_error"
            await design_session.broadcast(
                {"type": "error", "sessionId": design_session.session_id,
                 "message": "Internal orchestrator error -- see backend logs.", "fatal": True}
            )

    async def step_once(self, session_id: str) -> None:
        design_session = self.get(session_id)
        if design_session.run_task is not None and not design_session.run_task.done():
            raise RuntimeError("Session is already running continuously; stop it before single-stepping.")
        await design_session.agent.run_step()

    async def resume(self, session_id: str) -> None:
        design_session = self.get(session_id)
        if design_session.run_task is not None and not design_session.run_task.done():
            return
        design_session.agent._stop_requested = False
        design_session.run_task = asyncio.create_task(self._run_loop(design_session))

    async def send_message(self, session_id: str, text: str, auto_resume: bool = True) -> None:
        """The "chat to edit the pattern" entry point: injects a new user instruction
        into an existing (paused or already-complete) session and, by default,
        resumes the autonomous loop to act on it."""
        design_session = self.get(session_id)
        if design_session.run_task is not None and not design_session.run_task.done():
            raise RuntimeError("Session is actively running; stop it before sending a new instruction.")
        await design_session.agent.inject_user_message(text)
        if auto_resume:
            design_session.run_task = asyncio.create_task(self._run_loop(design_session))

    async def update_settings(
        self,
        session_id: str,
        model: str | None = None,
        api_key: str | None = None,
        system_prompt: str | None = None,
    ) -> None:
        """Live-edits an existing session's model / Anthropic credentials / system prompt --
        each takes effect on the *next* Claude call, no backend restart or session restart
        needed. Refuses while the loop is actively mid-turn (same guard as send_message):
        swapping the client or prompt out from under an in-flight streaming call is undefined.

        model and system_prompt are persisted (db_row() includes both, same as goal/step_limit)
        so they survive a backend restart, same as any other session setting. The API key is
        the one deliberate exception: it never touches sqlite or the logs (see _get_client()'s
        own masked-logging precedent for why that matters) -- a restarted backend falls back to
        config.ANTHROPIC_API_KEY / the SDK's own credential resolution for that session, same as
        it would for a brand-new one. Each argument left as None leaves that setting unchanged.
        """
        design_session = self.get(session_id)
        if design_session.run_task is not None and not design_session.run_task.done():
            raise RuntimeError("Session is actively running; stop it before changing settings.")
        agent = design_session.agent

        if model is not None:
            if model not in config.SELECTABLE_MODELS:
                raise ValueError(
                    f"Unknown model {model!r}; must be one of {sorted(config.SELECTABLE_MODELS)}"
                )
            agent.model = model

        if api_key is not None:
            key = api_key.strip()
            # Falls through to the SDK's own credential resolution if blanked out, same as
            # _get_client()'s own default-session behavior.
            agent.client = anthropic.AsyncAnthropic(api_key=key) if key else anthropic.AsyncAnthropic()

        if system_prompt is not None:
            agent.system_prompt = system_prompt.strip() or DEFAULT_SYSTEM_PROMPT

        await self._persist_session(design_session)

    async def stop_session(self, session_id: str) -> None:
        design_session = self.get(session_id)
        design_session.agent.request_stop()
        if design_session.run_task is not None:
            try:
                await asyncio.wait_for(design_session.run_task, timeout=config.ACTIOND_ACTION_TIMEOUT + 10)
            except asyncio.TimeoutError:
                design_session.run_task.cancel()
        elif design_session.agent.status not in ("complete", "error"):
            # Not auto-running (e.g. only ever single-stepped) -- finish it out here.
            await design_session.agent._finish("user_stopped", summary=None)

    async def shutdown(self) -> None:
        for design_session in self._sessions.values():
            try:
                if design_session.run_task is not None:
                    design_session.run_task.cancel()
                await design_session.agent.actiond.close()
            except Exception:
                pass


manager = SessionManager()
