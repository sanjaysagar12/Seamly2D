"""Milestone 7-ish smoke test for the REST/WebSocket surface: measurement upload,
starting a session, and the WebSocket event stream. Runs against the real FastAPI app
and a real actiond subprocess, but does NOT require a valid ANTHROPIC_API_KEY -- the
loop is expected to reach a clean "error" status (not hang) once it tries its first
real Claude call, which is exactly the failure-surfacing behavior this test checks.
"""
import asyncio
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent.parent))

import httpx
import pytest

from app.main import app
from app.session_manager import manager


@pytest.fixture(autouse=True)
def _isolate_manager_sessions():
    yield
    manager._sessions.clear()


@pytest.mark.asyncio
async def test_measurement_upload_and_list(tmp_path, monkeypatch):
    from app import config

    monkeypatch.setattr(config, "MEASUREMENTS_DIR", tmp_path)
    import app.main as main_module

    monkeypatch.setattr(main_module.config, "MEASUREMENTS_DIR", tmp_path)

    transport = httpx.ASGITransport(app=app)
    async with httpx.AsyncClient(transport=transport, base_url="http://test") as client:
        content = b"<?xml version='1.0'?><measurements individual=\"true\"></measurements>"
        resp = await client.post(
            "/api/measurements",
            files={"file": ("sample.smis", content, "application/xml")},
        )
        assert resp.status_code == 200, resp.text
        assert resp.json() == {"filename": "sample.smis"}

        resp = await client.get("/api/measurements")
        assert resp.status_code == 200
        assert "sample.smis" in resp.json()["files"]


@pytest.mark.asyncio
async def test_measurement_list_ignores_non_measurement_files(tmp_path, monkeypatch):
    # data/measurements/ keeps a .gitkeep placeholder so the empty dir survives git --
    # it (and anything else without a real measurement extension) must never show up
    # as a selectable option in the UI.
    from app import config

    monkeypatch.setattr(config, "MEASUREMENTS_DIR", tmp_path)
    import app.main as main_module

    monkeypatch.setattr(main_module.config, "MEASUREMENTS_DIR", tmp_path)

    (tmp_path / ".gitkeep").write_text("")
    (tmp_path / "notes.txt").write_text("not a measurement file")
    (tmp_path / "real.smis").write_text("<measurements individual=\"true\"></measurements>")

    transport = httpx.ASGITransport(app=app)
    async with httpx.AsyncClient(transport=transport, base_url="http://test") as client:
        resp = await client.get("/api/measurements")
        assert resp.status_code == 200
        assert resp.json()["files"] == ["real.smis"]


@pytest.mark.asyncio
async def test_start_session_surfaces_missing_credentials_cleanly(monkeypatch):
    # config.ANTHROPIC_API_KEY is read from .env once at import time (see
    # config.py's load_dotenv() call), so patching the env var here would only affect
    # a fresh import -- not the already-bound module constant session_manager reads.
    # Patch that constant directly so this test is deterministic regardless of
    # whatever real key is sitting in this developer's .env file.
    from app import config

    monkeypatch.setattr(config, "ANTHROPIC_API_KEY", "sk-ant-invalid-test-key")
    manager._client = None  # drop any cached client from an earlier test

    transport = httpx.ASGITransport(app=app)
    async with httpx.AsyncClient(transport=transport, base_url="http://test") as client:
        resp = await client.post(
            "/api/sessions",
            json={"goal": "Draw a single line between two points", "stepLimit": 3},
        )
        assert resp.status_code == 200, resp.text
        session_id = resp.json()["sessionId"]

        # Poll status until the loop reaches a terminal state (should be fast: the
        # very first Claude call fails auth).
        for _ in range(50):
            resp = await client.get(f"/api/sessions/{session_id}")
            status = resp.json()["status"]
            if status in ("complete", "error"):
                break
            await asyncio.sleep(0.2)
        else:
            pytest.fail(f"session never reached a terminal status; last={status}")

        assert status == "error"

        design_session = manager.get(session_id)
        assert design_session.agent.stop_reason == "api_error"

        error_events = [e for e in design_session.history if e["type"] == "error"]
        assert error_events, "expected at least one error event in the session history"
        assert error_events[0]["fatal"] is True

        # A brand-new session starts from a truly empty pattern -- render.snapshot
        # cleanly fails with "empty scene, nothing to render" (a plain actiond error,
        # not a crash) until at least one object exists, so there's no step-0 image yet.
        # initialize() must tolerate that and still reach the first Claude call.
        snapshot_events = [e for e in design_session.history if e["type"] == "snapshot_ready"]
        assert len(snapshot_events) == 0


@pytest.mark.asyncio
async def test_list_models():
    from app import config

    transport = httpx.ASGITransport(app=app)
    async with httpx.AsyncClient(transport=transport, base_url="http://test") as client:
        resp = await client.get("/api/models")
        assert resp.status_code == 200
        body = resp.json()
        ids = {m["id"] for m in body["models"]}
        assert ids == {"claude-haiku-4-5", "claude-sonnet-5", "claude-sonnet-4-6"}
        assert body["default"] == config.ANTHROPIC_MODEL


@pytest.mark.asyncio
async def test_start_session_rejects_unknown_model(monkeypatch):
    from app import config

    monkeypatch.setattr(config, "ANTHROPIC_API_KEY", "sk-ant-invalid-test-key")
    manager._client = None

    transport = httpx.ASGITransport(app=app)
    async with httpx.AsyncClient(transport=transport, base_url="http://test") as client:
        resp = await client.post(
            "/api/sessions",
            json={"goal": "Draw a line", "model": "gpt-5", "autorun": False},
        )
        assert resp.status_code == 400, resp.text
        assert "gpt-5" in resp.text


@pytest.mark.asyncio
async def test_start_session_honors_requested_model(monkeypatch):
    from app import config

    monkeypatch.setattr(config, "ANTHROPIC_API_KEY", "sk-ant-invalid-test-key")
    manager._client = None

    transport = httpx.ASGITransport(app=app)
    async with httpx.AsyncClient(transport=transport, base_url="http://test") as client:
        resp = await client.post(
            "/api/sessions",
            json={"goal": "Draw a line", "model": "claude-haiku-4-5", "autorun": False},
        )
        assert resp.status_code == 200, resp.text
        session_id = resp.json()["sessionId"]

        resp = await client.get(f"/api/sessions/{session_id}")
        assert resp.json()["model"] == "claude-haiku-4-5"
