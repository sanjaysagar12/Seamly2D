"""Subprocess wrapper around one persistent `actiond` NDJSON daemon session.

Protocol (verified by hand against the real binary — see docs/action-layer-schema.md
in the Seamly2D repo and the project README for how this was discovered):

  Request line:  {"id": <any>, "onError": "abort"|"continue", "actions": [{"op": ..., ...}, ...]}
  Response line: {"id": <echoed>, "status": "ok"|"partial"|"error", "appliedCount": N,
                  "results": [{"op","index","status":"ok"|"error","result","error"}, ...],
                  "error": <top-level failure or null>}

One request line in, one response line out, in order -- the daemon never interleaves,
so a single asyncio.Lock per process is enough to serialize callers safely.
"""
from __future__ import annotations

import asyncio
import json
import logging
import uuid
from pathlib import Path
from typing import Any

from . import config

logger = logging.getLogger("pattern_agent.actiond")


class ActiondError(Exception):
    """Base class for actiond wrapper failures."""


class ActiondCrashError(ActiondError):
    """The actiond subprocess exited (crashed or was killed) while a caller was waiting on it."""

    def __init__(self, returncode: int | None, stderr_tail: str):
        self.returncode = returncode
        self.stderr_tail = stderr_tail
        super().__init__(
            f"actiond process exited unexpectedly (code={returncode}). "
            f"Last stderr:\n{stderr_tail}"
        )


class ActiondTimeoutError(ActiondError):
    """actiond did not respond within the configured timeout."""


class ActiondSession:
    """One live actiond daemon subprocess, driving one in-memory pattern."""

    def __init__(
        self,
        output_dir: Path,
        pattern_path: Path | None = None,
        measurements_path: Path | None = None,
    ):
        self.output_dir = output_dir
        self.pattern_path = pattern_path
        self.measurements_path = measurements_path
        self._process: asyncio.subprocess.Process | None = None
        self._lock = asyncio.Lock()
        self._stderr_tail: list[str] = []
        self._stderr_task: asyncio.Task | None = None
        self._closed = False

    @property
    def is_alive(self) -> bool:
        return self._process is not None and self._process.returncode is None

    async def start(self) -> None:
        if not config.ACTIOND_EXE.exists():
            raise ActiondError(f"actiond executable not found at {config.ACTIOND_EXE}")

        self.output_dir.mkdir(parents=True, exist_ok=True)

        args = ["--output-dir", str(self.output_dir)]
        if self.pattern_path is not None:
            args += ["--pattern", str(self.pattern_path)]
        if self.measurements_path is not None:
            args += ["--measurements", str(self.measurements_path)]

        logger.info("Starting actiond: %s %s", config.ACTIOND_EXE, " ".join(args))
        self._process = await asyncio.create_subprocess_exec(
            str(config.ACTIOND_EXE),
            *args,
            stdin=asyncio.subprocess.PIPE,
            stdout=asyncio.subprocess.PIPE,
            stderr=asyncio.subprocess.PIPE,
        )
        self._stderr_task = asyncio.create_task(self._drain_stderr())

    async def _drain_stderr(self) -> None:
        assert self._process is not None and self._process.stderr is not None
        try:
            async for raw_line in self._process.stderr:
                line = raw_line.decode("utf-8", errors="replace").rstrip("\r\n")
                if not line:
                    continue
                self._stderr_tail.append(line)
                if len(self._stderr_tail) > 200:
                    self._stderr_tail.pop(0)
                logger.debug("actiond stderr: %s", line)
        except asyncio.CancelledError:
            pass

    async def send_batch(
        self,
        actions: list[dict[str, Any]],
        on_error: str = "abort",
        request_id: str | None = None,
        timeout: float = config.ACTIOND_ACTION_TIMEOUT,
    ) -> dict[str, Any]:
        """Send one request line (one or more actions) and return the parsed daemon response."""
        if not self.is_alive:
            raise ActiondCrashError(
                self._process.returncode if self._process else None,
                "\n".join(self._stderr_tail[-40:]),
            )

        request_id = request_id or uuid.uuid4().hex
        payload = {"id": request_id, "onError": on_error, "actions": actions}
        line = json.dumps(payload, ensure_ascii=False) + "\n"

        async with self._lock:
            assert self._process is not None
            assert self._process.stdin is not None
            assert self._process.stdout is not None

            try:
                self._process.stdin.write(line.encode("utf-8"))
                await self._process.stdin.drain()
            except (BrokenPipeError, ConnectionResetError) as exc:
                raise ActiondCrashError(
                    self._process.returncode, "\n".join(self._stderr_tail[-40:])
                ) from exc

            try:
                raw_response = await asyncio.wait_for(
                    self._process.stdout.readline(), timeout=timeout
                )
            except asyncio.TimeoutError as exc:
                raise ActiondTimeoutError(
                    f"actiond did not respond within {timeout}s to request {request_id}"
                ) from exc

            if not raw_response:
                # EOF on stdout -- the process died.
                raise ActiondCrashError(
                    self._process.returncode, "\n".join(self._stderr_tail[-40:])
                )

            try:
                return json.loads(raw_response.decode("utf-8"))
            except json.JSONDecodeError as exc:
                raise ActiondError(
                    f"Could not parse actiond response line: {raw_response!r}"
                ) from exc

    async def run_single(
        self, op: str, timeout: float = config.ACTIOND_ACTION_TIMEOUT, **params: Any
    ) -> dict[str, Any]:
        """Run exactly one action; return its `results[0]` entry
        ({"op","index","status","result","error"})."""
        action = {"op": op, **params}
        response = await self.send_batch([action], on_error="abort", timeout=timeout)
        if response.get("error"):
            # Malformed request itself (not a per-action failure).
            raise ActiondError(f"actiond rejected the request: {response['error']}")
        results = response.get("results") or []
        if not results:
            raise ActiondError(f"actiond returned no results for op={op!r}: {response!r}")
        return results[0]

    async def close(self, graceful_timeout: float = 5.0) -> None:
        if self._closed:
            return
        self._closed = True

        if self.is_alive:
            try:
                await self.send_batch([{"op": "session.close"}], timeout=graceful_timeout)
            except ActiondError:
                pass  # fall through to a hard terminate below

        if self._process is not None and self._process.returncode is None:
            try:
                self._process.stdin.close()  # type: ignore[union-attr]
            except Exception:
                pass
            try:
                await asyncio.wait_for(self._process.wait(), timeout=graceful_timeout)
            except asyncio.TimeoutError:
                logger.warning("actiond did not exit gracefully; terminating")
                self._process.terminate()
                try:
                    await asyncio.wait_for(self._process.wait(), timeout=5.0)
                except asyncio.TimeoutError:
                    self._process.kill()

        if self._stderr_task is not None:
            self._stderr_task.cancel()
