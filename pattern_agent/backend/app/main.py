from __future__ import annotations

import logging
import shutil
from contextlib import asynccontextmanager
from pathlib import Path
from typing import Optional

from fastapi import FastAPI, File, HTTPException, UploadFile, WebSocket, WebSocketDisconnect
from fastapi.middleware.cors import CORSMiddleware
from fastapi.responses import FileResponse
from fastapi.staticfiles import StaticFiles
from pydantic import BaseModel

from . import config, db
from .agent_loop import SYSTEM_PROMPT as DEFAULT_SYSTEM_PROMPT
from .session_manager import SessionNotFoundError, manager

logging.basicConfig(level=logging.INFO)
logger = logging.getLogger("pattern_agent.main")


@asynccontextmanager
async def lifespan(_app: FastAPI):
    await db.init_db()
    await manager.load_from_db()
    yield
    await manager.shutdown()
    await db.close_db()


app = FastAPI(title="Pattern Agent", lifespan=lifespan)

app.add_middleware(
    CORSMiddleware,
    allow_origins=["*"],
    allow_methods=["*"],
    allow_headers=["*"],
)

app.mount("/files", StaticFiles(directory=str(config.SESSIONS_DIR)), name="files")


# ---------------------------------------------------------------------------
# Shared helpers for the uploaded-file managers below (measurements, patterns) --
# both are "a directory of files matching one of a few extensions", so renaming and
# deleting are identical operations parameterized only by directory + allowed extensions.
# ---------------------------------------------------------------------------

class RenameFileRequest(BaseModel):
    newFilename: str


def _resolve_uploaded_file(directory: Path, filename: str, allowed_exts: set[str]) -> Path:
    candidate = directory / Path(filename).name
    if not candidate.is_file() or candidate.suffix.lower() not in allowed_exts:
        raise HTTPException(404, f"File not found: {filename}")
    return candidate


def _delete_uploaded_file(directory: Path, filename: str, allowed_exts: set[str]) -> None:
    _resolve_uploaded_file(directory, filename, allowed_exts).unlink()


def _rename_uploaded_file(directory: Path, filename: str, new_filename: str, allowed_exts: set[str]) -> str:
    candidate = _resolve_uploaded_file(directory, filename, allowed_exts)
    new_name = Path(new_filename).name
    if not new_name or Path(new_name).suffix.lower() not in allowed_exts:
        raise HTTPException(400, f"Unsupported or missing file name/type: {new_filename!r}")
    dest = directory / new_name
    if dest.exists():
        raise HTTPException(409, f"A file named {new_name!r} already exists")
    candidate.rename(dest)
    return dest.name


# ---------------------------------------------------------------------------
# Measurements
# ---------------------------------------------------------------------------

ALLOWED_MEASUREMENT_EXTS = {".smis", ".smms", ".vst"}


@app.get("/api/measurements")
async def list_measurements():
    files = sorted(
        p.name
        for p in config.MEASUREMENTS_DIR.iterdir()
        if p.is_file() and p.suffix.lower() in ALLOWED_MEASUREMENT_EXTS
    )
    return {"files": files}


@app.post("/api/measurements")
async def upload_measurement(file: UploadFile = File(...)):
    ext = Path(file.filename or "").suffix.lower()
    if ext not in ALLOWED_MEASUREMENT_EXTS:
        raise HTTPException(400, f"Unsupported measurement file type {ext!r}")
    dest = config.MEASUREMENTS_DIR / Path(file.filename).name
    with dest.open("wb") as f:
        shutil.copyfileobj(file.file, f)
    return {"filename": dest.name}


@app.patch("/api/measurements/{filename}")
async def rename_measurement(filename: str, req: RenameFileRequest):
    new_name = _rename_uploaded_file(config.MEASUREMENTS_DIR, filename, req.newFilename, ALLOWED_MEASUREMENT_EXTS)
    return {"filename": new_name}


@app.delete("/api/measurements/{filename}")
async def delete_measurement(filename: str):
    _delete_uploaded_file(config.MEASUREMENTS_DIR, filename, ALLOWED_MEASUREMENT_EXTS)
    return {"ok": True}


# ---------------------------------------------------------------------------
# Base pattern files (optional starting point for a session, instead of an
# empty pattern -- actiond's --pattern loads either extension the same way,
# see pattern_session.cpp/PatternSession::loadFromFile, which just parses the
# file's XML content regardless of suffix).
# ---------------------------------------------------------------------------

ALLOWED_PATTERN_EXTS = {".val", ".sm2d"}


def _is_actiond_autobackup(name: str) -> bool:
    """actiond (like the Seamly2D GUI itself) writes its own "<name>_<timestamp>
    (backup)<ext>" copy next to a pattern file the moment it opens it -- this fires
    for every base-pattern session start *and* every piece-preview call below, right
    inside PATTERNS_DIR. These are actiond's own internal safety copies, not files a
    user uploaded, so they must never show up as selectable "base pattern" options."""
    return "(backup)" in name


@app.get("/api/patterns")
async def list_patterns():
    files = sorted(
        p.name
        for p in config.PATTERNS_DIR.iterdir()
        if p.is_file() and p.suffix.lower() in ALLOWED_PATTERN_EXTS and not _is_actiond_autobackup(p.name)
    )
    return {"files": files}


@app.post("/api/patterns")
async def upload_pattern(file: UploadFile = File(...)):
    ext = Path(file.filename or "").suffix.lower()
    if ext not in ALLOWED_PATTERN_EXTS:
        raise HTTPException(400, f"Unsupported pattern file type {ext!r}")
    dest = config.PATTERNS_DIR / Path(file.filename).name
    with dest.open("wb") as f:
        shutil.copyfileobj(file.file, f)
    return {"filename": dest.name}


@app.patch("/api/patterns/{filename}")
async def rename_pattern(filename: str, req: RenameFileRequest):
    new_name = _rename_uploaded_file(config.PATTERNS_DIR, filename, req.newFilename, ALLOWED_PATTERN_EXTS)
    return {"filename": new_name}


@app.delete("/api/patterns/{filename}")
async def delete_pattern(filename: str):
    _delete_uploaded_file(config.PATTERNS_DIR, filename, ALLOWED_PATTERN_EXTS)
    return {"ok": True}


@app.get("/api/patterns/{filename}/pieces")
async def list_pattern_file_pieces(filename: str, measurementsFilename: Optional[str] = None):
    """Which pieces (Front/Back/Sleeve/...) an uploaded base pattern file already
    contains -- lets the home page show/select them before a session is even started.

    measurementsFilename is optional but frequently required: many real, multi-size
    pattern files (e.g. an Aldrich block-style master pattern) reference a measurements
    file by a path from wherever they were originally authored, which never resolves
    once uploaded here -- actiond then fails to load the pattern at all ("Measurements
    file not found"). Passing the measurement file selected alongside this pattern (see
    GET/POST /api/measurements) overrides that stale reference, exactly like starting a
    real session with both files does.
    """
    candidate = config.PATTERNS_DIR / Path(filename).name
    if not candidate.exists():
        raise HTTPException(404, f"Pattern file not found: {filename}")

    measurements_path = None
    if measurementsFilename:
        measurements_path = config.MEASUREMENTS_DIR / Path(measurementsFilename).name
        if not measurements_path.exists():
            raise HTTPException(404, f"Measurement file not found: {measurementsFilename}")

    try:
        pieces = await manager.list_pattern_pieces(candidate, measurements_path=measurements_path)
    except Exception as exc:
        raise HTTPException(400, f"Could not read pieces from {filename}: {exc}") from exc
    return {"pieces": pieces}


# ---------------------------------------------------------------------------
# Models
# ---------------------------------------------------------------------------

@app.get("/api/models")
async def list_models():
    return {
        "models": [{"id": model_id, "label": label} for model_id, label in config.SELECTABLE_MODELS.items()],
        "default": config.ANTHROPIC_MODEL,
        # Lets the home page prefill an editable system-prompt field with the real default
        # instead of either hardcoding a stale copy of it in the frontend or adding a second
        # round trip just for one string -- StartScreen already fetches this endpoint on mount.
        "defaultSystemPrompt": DEFAULT_SYSTEM_PROMPT,
    }


# ---------------------------------------------------------------------------
# Sessions
# ---------------------------------------------------------------------------

class StartSessionRequest(BaseModel):
    # Optional: a session can start with no goal at all, letting the user describe what
    # to draft afterward via the session page's chat box instead (session_manager.
    # start_session forces autorun off in that case -- see below -- so the agent never
    # burns a turn improvising against an empty goal before that first message arrives).
    goal: Optional[str] = None
    measurementsFilename: Optional[str] = None
    patternFilename: Optional[str] = None
    stepLimit: Optional[int] = None
    autorun: bool = True
    model: Optional[str] = None
    systemPrompt: Optional[str] = None
    # Name of a piece already present in patternFilename (see GET .../pieces above) to
    # start the agent "focused" on -- it gets an automatic close-up snapshot from turn
    # one, same as any piece the agent later references itself. Optional and independent
    # of patternFilename: a session can also start with no base pattern and no focus.
    focusPiece: Optional[str] = None


@app.post("/api/sessions")
async def start_session(req: StartSessionRequest):
    measurements_path = None
    if req.measurementsFilename:
        candidate = config.MEASUREMENTS_DIR / req.measurementsFilename
        if not candidate.exists():
            raise HTTPException(404, f"Measurement file not found: {req.measurementsFilename}")
        measurements_path = candidate

    pattern_path = None
    if req.patternFilename:
        candidate = config.PATTERNS_DIR / req.patternFilename
        if not candidate.exists():
            raise HTTPException(404, f"Pattern file not found: {req.patternFilename}")
        pattern_path = candidate

    goal = (req.goal or "").strip()
    # With no goal, autorun is forced off regardless of what the caller asked for: the
    # very first agent turn is built from goal_text (see agent_loop.initialize(), which
    # already renders a blank goal as "wait for the user's first instruction"), so
    # running immediately would force the model to invent an action from nothing. The
    # session starts "paused" instead, waiting for the user's first chat message --
    # send_message()'s own auto_resume then starts the loop for real.
    try:
        design_session = await manager.start_session(
            goal=goal,
            measurements_path=measurements_path,
            pattern_path=pattern_path,
            step_limit=req.stepLimit or config.DEFAULT_STEP_LIMIT,
            autorun=req.autorun and bool(goal),
            model=req.model,
            system_prompt=req.systemPrompt,
            focus_piece=req.focusPiece,
        )
    except ValueError as exc:
        raise HTTPException(400, str(exc)) from exc
    except Exception as exc:
        logger.exception("Failed to start session")
        raise HTTPException(500, f"Failed to start session: {exc}") from exc

    return {"sessionId": design_session.session_id}


@app.get("/api/sessions")
async def list_sessions():
    return {"sessions": manager.list_sessions()}


@app.get("/api/sessions/{session_id}")
async def get_session(session_id: str):
    try:
        design_session = manager.get(session_id)
    except SessionNotFoundError:
        raise HTTPException(404, "Session not found")
    agent = design_session.agent
    return {
        "sessionId": session_id,
        "status": agent.status,
        "step": agent.step,
        "stepLimit": agent.step_limit,
        "goal": agent.goal,
        "model": agent.model,
        # Never the API key -- see update_session_settings below. The system prompt itself
        # isn't a secret; sent back so the settings panel can prefill its textarea with
        # whatever is actually in effect right now, not just the built-in default.
        "systemPrompt": agent.system_prompt,
        "stopReason": agent.stop_reason,
        "finalSummary": agent.final_summary,
        "valUrl": f"/files/{session_id}/final.val" if agent.final_val_path else None,
        "currentPiece": agent.current_piece,
    }


@app.delete("/api/sessions/{session_id}")
async def delete_session(session_id: str):
    """Permanently deletes a session -- stops it first if still running. Irreversible:
    the conversation, checkpoints, snapshots, and saved .val are all removed from disk."""
    try:
        await manager.delete_session(session_id)
    except SessionNotFoundError:
        raise HTTPException(404, "Session not found")
    return {"ok": True}


@app.get("/api/sessions/{session_id}/pieces")
async def list_session_pieces(session_id: str):
    try:
        pieces = await manager.list_session_pieces(session_id)
    except SessionNotFoundError:
        raise HTTPException(404, "Session not found")
    except Exception as exc:
        raise HTTPException(400, f"Could not list pieces: {exc}") from exc
    return {"pieces": pieces}


@app.get("/api/sessions/{session_id}/pieces/{piece}/snapshot")
async def get_piece_snapshot(session_id: str, piece: str):
    """Renders (or re-renders) a close-up of one piece on demand -- the session page
    calls this whenever the user switches which piece they're looking at. Also
    redirects the agent's own attention to this piece (see
    SessionManager.render_piece_snapshot), so switching pieces here is how a user
    working across several pieces in one session tells the agent which one to act on
    next before sending a chat instruction."""
    try:
        result = await manager.render_piece_snapshot(session_id, piece)
    except SessionNotFoundError:
        raise HTTPException(404, "Session not found")
    except Exception as exc:
        raise HTTPException(400, f"Could not render piece {piece!r}: {exc}") from exc
    return result


class UpdateSessionSettingsRequest(BaseModel):
    model: Optional[str] = None
    apiKey: Optional[str] = None
    systemPrompt: Optional[str] = None
    goal: Optional[str] = None


@app.post("/api/sessions/{session_id}/settings")
async def update_session_settings(session_id: str, req: UpdateSessionSettingsRequest):
    """Live-edits model / Anthropic API key / system prompt / goal for an existing
    session -- see SessionManager.update_settings()'s docstring for exactly what does
    and doesn't persist. Every field is optional and independent: omit whichever ones
    you don't want to change. The API key is write-only by design -- it is never echoed
    back by this or any other endpoint (see get_session above)."""
    try:
        await manager.update_settings(
            session_id, model=req.model, api_key=req.apiKey, system_prompt=req.systemPrompt, goal=req.goal
        )
    except SessionNotFoundError:
        raise HTTPException(404, "Session not found")
    except RuntimeError as exc:
        raise HTTPException(409, str(exc))
    except ValueError as exc:
        raise HTTPException(400, str(exc))
    return {"ok": True}


@app.post("/api/sessions/{session_id}/stop")
async def stop_session(session_id: str):
    try:
        await manager.stop_session(session_id)
    except SessionNotFoundError:
        raise HTTPException(404, "Session not found")
    return {"ok": True}


@app.post("/api/sessions/{session_id}/step")
async def step_session(session_id: str):
    try:
        await manager.step_once(session_id)
    except SessionNotFoundError:
        raise HTTPException(404, "Session not found")
    except RuntimeError as exc:
        raise HTTPException(409, str(exc))
    return {"ok": True}


@app.post("/api/sessions/{session_id}/resume")
async def resume_session(session_id: str):
    try:
        await manager.resume(session_id)
    except SessionNotFoundError:
        raise HTTPException(404, "Session not found")
    return {"ok": True}


class SendMessageRequest(BaseModel):
    text: str


@app.post("/api/sessions/{session_id}/message")
async def send_message(session_id: str, req: SendMessageRequest):
    if not req.text.strip():
        raise HTTPException(400, "Message text cannot be empty")
    try:
        await manager.send_message(session_id, req.text.strip())
    except SessionNotFoundError:
        raise HTTPException(404, "Session not found")
    except RuntimeError as exc:
        raise HTTPException(409, str(exc))
    except ValueError as exc:
        raise HTTPException(409, str(exc))
    return {"ok": True}


@app.get("/api/sessions/{session_id}/download")
async def download_val(session_id: str):
    try:
        design_session = manager.get(session_id)
    except SessionNotFoundError:
        raise HTTPException(404, "Session not found")
    val_path = design_session.agent.final_val_path
    if val_path is None or not val_path.exists():
        raise HTTPException(404, "Final .val not available yet")
    return FileResponse(val_path, filename=f"{session_id}.val", media_type="application/xml")


# ---------------------------------------------------------------------------
# WebSocket event stream
# ---------------------------------------------------------------------------

@app.websocket("/ws/{session_id}")
async def session_events(websocket: WebSocket, session_id: str):
    await websocket.accept()
    try:
        design_session = manager.get(session_id)
    except SessionNotFoundError:
        await websocket.send_json({"type": "error", "message": "Session not found", "fatal": True})
        await websocket.close()
        return

    for event in design_session.history:
        await websocket.send_json(event)

    queue = design_session.subscribe()
    try:
        while True:
            event = await queue.get()
            await websocket.send_json(event)
    except WebSocketDisconnect:
        pass
    finally:
        design_session.unsubscribe(queue)


# ---------------------------------------------------------------------------
# Built frontend (frontend/`npm run build`'s dist/), if present
# ---------------------------------------------------------------------------
# Mounted last and only at "/" (a catch-all) so every /api, /files, /ws route above -- all
# registered earlier -- is matched first; Starlette tries routes in registration order, so this
# mount only ever serves paths nothing above claimed. html=True serves dist/index.html for "/"
# and any other path that isn't a real file, which is what a single-page app needs (this app has
# no client-side router today, but that's what would fall through to on a hard refresh of a
# deep link if one were added later). Absent locally (README's `npm run dev` + vite proxy is the
# local-dev path instead) -- guarded so uvicorn still starts fine without a built dist/.
if config.FRONTEND_DIST_DIR.is_dir():
    app.mount("/", StaticFiles(directory=str(config.FRONTEND_DIST_DIR), html=True), name="frontend")
