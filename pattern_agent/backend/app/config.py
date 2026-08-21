"""Central configuration, resolved from environment variables."""
from __future__ import annotations

import os
from pathlib import Path

from dotenv import load_dotenv

BACKEND_DIR = Path(__file__).resolve().parent.parent
REPO_ROOT = BACKEND_DIR.parent.parent

load_dotenv(BACKEND_DIR / ".env", override=True)

ACTIOND_EXE = Path(
    os.environ.get(
        "ACTIOND_EXE",
        str(REPO_ROOT / "out" / "src" / "app" / "actiond" / "bin" / "actiond.exe"),
    )
)

DATA_DIR = Path(os.environ.get("PATTERN_AGENT_DATA_DIR", str(BACKEND_DIR / "data")))
MEASUREMENTS_DIR = DATA_DIR / "measurements"
SESSIONS_DIR = DATA_DIR / "sessions"
DB_PATH = Path(os.environ.get("PATTERN_AGENT_DB_PATH", str(DATA_DIR / "pattern_agent.db")))

ANTHROPIC_API_KEY = (os.environ.get("ANTHROPIC_API_KEY") or "").strip() or None
ANTHROPIC_MODEL = os.environ.get("ANTHROPIC_MODEL", "claude-opus-5")
ANTHROPIC_EFFORT = os.environ.get("ANTHROPIC_EFFORT", "medium")
ANTHROPIC_MAX_TOKENS = int(os.environ.get("ANTHROPIC_MAX_TOKENS", "8000"))

DEFAULT_STEP_LIMIT = int(os.environ.get("PATTERN_AGENT_STEP_LIMIT", "60"))
EXTRA_STEPS_PER_MESSAGE = int(os.environ.get("PATTERN_AGENT_EXTRA_STEPS_PER_MESSAGE", "20"))
ACTIOND_STARTUP_TIMEOUT = float(os.environ.get("ACTIOND_STARTUP_TIMEOUT", "15"))
ACTIOND_ACTION_TIMEOUT = float(os.environ.get("ACTIOND_ACTION_TIMEOUT", "30"))

MEASUREMENTS_DIR.mkdir(parents=True, exist_ok=True)
SESSIONS_DIR.mkdir(parents=True, exist_ok=True)
