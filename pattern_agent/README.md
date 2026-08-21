# Pattern Agent

A web app that drives `actiond` (Seamly2D's headless action-layer daemon) with an
autonomous Claude agent: you describe a garment piece in natural language, the agent
picks one construction action at a time, looks at the re-rendered pattern after every
action, and keeps going until it declares the piece done or a step limit is hit.

This is a standalone app in its own subtree (`pattern_agent/`) that drives `actiond.exe`
as a subprocess. It does not modify `actiond`, `src/libs/actionlayer`, or any other
Seamly2D/C++ code.

## Running it locally

### Backend

```
cd pattern_agent/backend
python -m venv .venv
.venv\Scripts\pip install -r requirements.txt
copy .env.example .env        # then edit .env and set ANTHROPIC_API_KEY
.venv\Scripts\python -m uvicorn app.main:app --port 8000
```

By default it looks for `actiond.exe` at
`out/src/app/actiond/bin/actiond.exe` relative to the Seamly2D repo root (i.e. it
assumes this checkout already has a built `actiond`). Override with the `ACTIOND_EXE`
env var if yours lives elsewhere. See `app/config.py` for every other env var
(`ANTHROPIC_MODEL`, `ANTHROPIC_EFFORT`, `PATTERN_AGENT_STEP_LIMIT`, timeouts, ...).

Run the backend's own test suite (spawns real `actiond` subprocesses; the agent-loop
tests use a scripted fake Claude so they don't need `ANTHROPIC_API_KEY`):

```
.venv\Scripts\python -m pytest tests/ -v
```

### Frontend

```
cd pattern_agent/frontend
npm install
npm run dev
```

Opens on `http://localhost:5173`. `vite.config.ts` proxies `/api`, `/files`, and `/ws`
to the backend on port 8000, so run both at once.

## Agent loop walkthrough (for tuning the system prompt)

Everything for the loop itself lives in `backend/app/agent_loop.py`, `AgentSession`:

- **`SYSTEM_PROMPT`** (top of the file) is the whole behavioral contract: one tool call
  per turn, look at the image before deciding, how formulas/measurements work, how to
  recover from an error, and that `pattern.complete` is the only clean way to end a
  session. This is the first place to edit if the agent is drafting badly, calling too
  many read-only tools, or not looking at images carefully enough.
- **`initialize()`** renders a step-0 snapshot (skipped if the pattern is truly empty --
  `render.snapshot` cleanly errors with "empty scene, nothing to render" until at least
  one object exists) and seeds `self.messages` with the goal text + that image.
- **`run_step()`** is one full turn: call Claude (`_call_claude`, streamed, thinking +
  exactly one forced tool call via `tool_choice: {"type": "any",
  "disable_parallel_tool_use": true}`) → if the tool is `pattern.complete`, finish →
  otherwise validate args locally against the tool's own JSON Schema
  (`validation.py`) → execute against the live `actiond` session
  (`ActiondSession.run_single`) → re-render a snapshot → checkpoint-save the `.val` →
  build a `tool_result` (text summary + the new snapshot image) → append it and loop.
  Every step, success or failure, emits events (`events.py`) that the WebSocket layer
  fans out live.
- **Crash recovery** (`_recover_from_crash`): if `actiond` itself dies mid-action (a
  segfault, in principle -- see the `piece.union` note below), the session respawns a
  fresh `actiond` process from the last checkpoint `.val` and tells the agent the action
  that caused it was *not* applied and not to repeat it verbatim. This is why every
  successful step also calls `session.save` to `checkpoint.val` -- it's the recovery
  point, not just a convenience.
- **`tool_schema.py`** converts `actiond --list-tools --format=ai` into the Anthropic
  `tools` array. It excludes `piece.union` (reproducibly segfaults `actiond` -- see
  Step 0 findings below) and `render.snapshot`/`session.save`/`session.close` (these are
  driven by the orchestrator every turn, not by the agent's own tool choice, so the
  agent's one-call-per-turn budget always goes toward real construction/introspection
  work). It appends one synthetic tool, `pattern.complete`, as the backend-only
  completion signal.
- **`session_manager.py`** owns the set of live sessions: one `AgentSession` per session
  id (which itself owns the current `ActiondSession` -- see the note below), an
  event-history buffer for late WebSocket subscribers (a client that connects
  mid-session, including the sessions list on the start screen, gets the full replay
  first), and the REST-facing start/stop/step/resume/message operations.
- **Chat / editing an existing pattern** (`inject_user_message` in `agent_loop.py`,
  `POST /api/sessions/{id}/message`): appends a new user instruction to the same
  conversation and resumes the loop. If the session had already finished (`actiond` was
  closed in `_finish()`), this revives a fresh `actiond` process from `final.val` (or
  `checkpoint.val`) first -- same mechanism as crash recovery, via the shared
  `_respawn_actiond` helper -- so the existing geometry isn't lost, and bumps
  `step_limit` by `config.EXTRA_STEPS_PER_MESSAGE` so it doesn't immediately re-hit the
  cap. Blocked while the loop is actively mid-turn (`status == "running"`); the frontend
  enforces this too. **Gotcha if you touch this path:** every `tool_use` block Claude
  emits must have a matching `tool_result` in the very next message or the API rejects
  the *next* request with a 400 -- including `pattern_complete`'s own tool_use, which
  used to get no tool_result at all because the turn just ended right after. That was
  invisible until a chat message resent that history to a live completed/errored
  session. `_handle_pattern_complete` and the unrecoverable-failure path in `run_step`
  both close out their tool_use now; `test_agent_loop.py`'s
  `assert_valid_message_history()` checks this invariant on scripted transcripts so a
  regression here doesn't need a live API call to catch.

**Model selection:** default is `claude-sonnet-5` (`config.ANTHROPIC_MODEL`, overridable
via `.env`); the start screen's Model dropdown offers Claude Haiku 4.5, Sonnet 5, and
Sonnet 4.6 (`config.SELECTABLE_MODELS`, served to the frontend via `GET /api/models` so
the two never drift apart), fixed at session start and stored per-session (`model`
column, included in `db_row()` so a chat-revived or backend-restart-reloaded session
keeps using the model it started with). **`claude-haiku-4-5` is the odd one out**: unlike
Opus 5/Sonnet 5/Sonnet 4.6, it doesn't support `thinking: {"type": "adaptive"}` or
`output_config.effort` -- sending either returns a 400. `AgentSession.
_thinking_and_effort_kwargs()` omits both for Haiku rather than reaching for its older
`budget_tokens`-style config; if you add a fourth selectable model, check the
`shared/model-migration.md` / thinking-support table in the `claude-api` skill before
assuming it takes the same kwargs as Sonnet 5.

**Single source of truth for `actiond`:** `AgentSession.actiond` is reassigned in place
whenever the subprocess is respawned (crash recovery, chat revival). `DesignSession`
deliberately does *not* keep its own copy -- an earlier version did, and it went stale
the moment a respawn happened, so `shutdown()` was closing an already-dead process and
leaking the real one.

**Persistence (`db.py`):** SQLite at `data/pattern_agent.db` (override with
`PATTERN_AGENT_DB_PATH`), two tables -- `sessions` (metadata + the agent's full
`messages_json`) and `events` (the same events the WebSocket streams, in order, for
replay). Written best-effort on every emitted event
(`SessionManager._persist_session`) -- a persistence failure never turns into a 500 for
the actual session operation, it just logs. Read once at backend startup
(`main.py`'s `lifespan` -> `db.init_db()` -> `manager.load_from_db()`): every persisted
session reappears in the sessions list exactly as it was, and `AgentSession.actiond`
starts as a *never-started* placeholder (`is_alive` is `False`, same as a closed one)
rather than spawning a live `actiond` process for every historical session up front --
it transparently revives on first real use (Step/Run/chat) via the same
`_ensure_actiond_alive()` → `_respawn_actiond()` path crash recovery already uses,
reading back from `final.val` or `checkpoint.val`. A session whose last known status was
`"running"` (backend went down mid-turn) reloads as `"paused"`, not stuck showing
`"running"` forever with nothing left to advance it.

## What changed from the original design brief, per Step 0's discovery

The task started with an explicit "discover the real interface before designing
anything" step. Everything held up except:

- **`piece.union` reproducibly segfaults `actiond`** (documented in
  `docs/action-layer-schema.md` and the test suite, and flagged by `--list-tools
  --format=ai` itself via `"status": "partial"`). A crash loses all in-memory pattern
  state, unlike every other failure mode (which comes back as a clean JSON error the
  agent can retry past) -- so it's excluded from the agent's tool list entirely rather
  than trusted to a warning string in the description.
- **`render.snapshot`/`session.save`/`session.close` are backend-managed, not
  agent-exposed.** The brief's spec already implied the backend re-renders after every
  action ("After each action executes, the pattern is re-rendered..."); making that
  literal meant the agent shouldn't also be able to spend its one-tool-per-turn budget
  calling `render.snapshot` itself.
- Everything else -- the persistent NDJSON daemon protocol, the
  `{id,status,appliedCount,results:[...]}` response envelope, `--list-tools
  --format=ai` already being ~shaped like an Anthropic tool definition, how
  `render.snapshot`/`measurements.load`/`session.save` behave -- matched the brief's
  assumptions once verified against the real binary.

## Known limitations / not yet built

- No auth/multi-user isolation -- fine for local use, not for exposing this publicly.
- No way to delete/archive a session from the UI or the database once started -- it
  just sits in the sessions list (and in SQLite) indefinitely.
- `messages_json` stores the full conversation, embedded snapshot images included, with
  no pruning -- fine for typical session lengths, but a very long-running chat-edited
  session will grow that column (and the in-memory `AgentSession.messages` it mirrors)
  without bound. Anthropic's own context-editing/compaction features would be the next
  step here if that becomes a real problem.
