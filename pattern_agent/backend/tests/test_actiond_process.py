"""Milestone 1 verification: spawn one actiond daemon session, run a hardcoded
action, and get a snapshot back. Run with: pytest -s tests/test_actiond_process.py
"""
import asyncio
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent.parent))

import pytest

from app.actiond_process import ActiondSession


@pytest.mark.asyncio
async def test_basic_session_roundtrip(tmp_path):
    session = ActiondSession(output_dir=tmp_path)
    await session.start()
    try:
        result = await session.run_single(
            "basePoint", name="A1", x=0, y=0, draftBlock="front"
        )
        assert result["status"] == "ok", result
        assert result["result"]["name"] == "A1"

        result2 = await session.run_single(
            "endLine", name="A2", basePoint="A1", angle="90", length="50"
        )
        assert result2["status"] == "ok", result2

        snap = await session.run_single("render.snapshot", path="snap.png")
        assert snap["status"] == "ok", snap
        snap_path = Path(snap["result"]["path"])
        assert snap_path.exists(), f"snapshot not written to {snap_path}"
        assert snap_path.stat().st_size > 0

        dump = await session.run_single("pattern.dump")
        assert dump["status"] == "ok"
        assert len(dump["result"]["objects"]) == 2
    finally:
        await session.close()


@pytest.mark.asyncio
async def test_error_then_recovery(tmp_path):
    session = ActiondSession(output_dir=tmp_path)
    await session.start()
    try:
        bad = await session.run_single(
            "endLine", name="Bad", basePoint="NoSuchPoint", angle="0", length="1"
        )
        assert bad["status"] == "error"
        assert bad["error"] is not None

        # Process must still be alive and usable after a per-action failure.
        ok = await session.run_single("basePoint", name="A", x=0, y=0, draftBlock="front")
        assert ok["status"] == "ok"
    finally:
        await session.close()


if __name__ == "__main__":
    async def main():
        out = Path(__file__).resolve().parent / "_manual_output"
        out.mkdir(exist_ok=True)
        session = ActiondSession(output_dir=out)
        await session.start()
        try:
            r = await session.run_single("basePoint", name="A1", x=0, y=0, draftBlock="front")
            print("basePoint ->", r)
            snap = await session.run_single("render.snapshot", path="manual_snap.png")
            print("render.snapshot ->", snap)
        finally:
            await session.close()

    asyncio.run(main())
