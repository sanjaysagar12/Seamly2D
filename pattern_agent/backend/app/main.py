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


# ---------------------------------------------------------------------------
# Sessions
# ---------------------------------------------------------------------------

class StartSessionRequest(BaseModel):
    goal: str
    measurementsFilename: Optional[str] = None
    stepLimit: Optional[int] = None
    autorun: bool = True


@app.post("/api/sessions")
async def start_session(req: StartSessionRequest):
    measurements_path = None
    if req.measurementsFilename:
        candidate = config.MEASUREMENTS_DIR / req.measurementsFilename
        if not candidate.exists():
            raise HTTPException(404, f"Measurement file not found: {req.measurementsFilename}")
        measurements_path = candidate

    try:
        design_session = await manager.start_session(
            goal=req.goal,
            measurements_path=measurements_path,
            step_limit=req.stepLimit or config.DEFAULT_STEP_LIMIT,
            autorun=req.autorun,
        )
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
        "stopReason": agent.stop_reason,
        "finalSummary": agent.final_summary,
        "valUrl": f"/files/{session_id}/final.val" if agent.final_val_path else None,
    }


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
