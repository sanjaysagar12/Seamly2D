# seamly2d-mcp-server

An MCP server that wraps `seamly2d-actiond` (Seamly2D's headless pattern-construction engine,
`src/app/actiond/`) so an MCP client — Claude Desktop, Claude Code, etc. — can build sewing
patterns action by action, seeing a rendered PNG snapshot after each step.

It does not hand-maintain a list of tools. At startup it runs `actiond --list-tools --format=ai`
once and generates one MCP tool per action op from that output, so the tool set automatically
tracks whatever `actiond`'s own action registry supports.

## Prerequisites

- Node.js 18+.
- A working `actiond` — either built locally (see the repo root's `build_actiond.bat` /
  `Makefile`) or available as the `seamly2d-actiond:latest` Docker image (`Dockerfile.actiond`
  at the repo root).

## Install & build

```
npm install
npm run build
```

This produces `dist/index.js`, a stdio MCP server entrypoint.

## Configuring how `actiond` is invoked

Set `ACTIOND_COMMAND` to a JSON array giving the command and any fixed leading args used to
invoke `actiond`. Two shapes are supported:

- **Local binary** (default if unset: `["actiond"]`, i.e. assumes it's on `PATH`):
  ```
  ACTIOND_COMMAND=["C:/yoko/Seamly2D/build/src/app/actiond/bin/actiond.exe"]
  ```
- **Docker image** — include the literal token `<session-dir>` inside the `-v` arg; it is
  substituted per-session with that session's own host output directory, and the server then
  appends `--output-dir <container path>` itself (default `/data/output`, override with
  `ACTIOND_CONTAINER_OUTPUT_DIR`) instead of a host path:
  ```
  ACTIOND_COMMAND=["docker","run","-i","--rm","-v","<session-dir>:/data/output","seamly2d-actiond:latest"]
  ```

In both cases the server appends `--output-dir <path>` itself for every session daemon it
spawns — don't include `--output-dir` in `ACTIOND_COMMAND` yourself.

Other environment variables (all optional):

| Variable | Default | Meaning |
|---|---|---|
| `MCP_SESSIONS_DIR` | `<this package>/sessions` | Host directory under which each session gets its own subdirectory. |
| `MCP_SESSION_IDLE_TIMEOUT_MS` | `1800000` (30 min) | A session with no tool call against it for this long is auto-closed. |
| `MCP_ACTIOND_REQUEST_TIMEOUT_MS` | `30000` | How long to wait for one actiond request/response round trip before treating it as hung. |
| `MCP_ACTIOND_EXIT_GRACE_MS` | `5000` | Grace period after `session.close` before a still-running actiond process is SIGKILLed. |
| `MCP_SNAPSHOT_WIDTH` | `800` | Pixel width used for the automatic post-action snapshot render. |
| `ACTIOND_CONTAINER_OUTPUT_DIR` | `/data/output` | Container-side output path used when `ACTIOND_COMMAND` is docker-style (see above). |

## Claude Desktop setup

Copy an entry from [`claude_desktop_config.example.json`](claude_desktop_config.example.json)
into your `claude_desktop_config.json`'s `"mcpServers"` object, adjusting the paths for your
machine, then restart Claude Desktop.

## Session model

MCP's stdio transport has no "new chat started" signal that reaches a long-lived server
process, so session boundaries are explicit tool calls, not something the protocol infers:

1. At the start of a conversation, the model calls `pattern_new_session` (no arguments), which
   spawns a fresh `actiond` daemon subprocess against a blank pattern in its own isolated
   directory and returns a `patternSessionId`.
2. Every other tool call takes that `patternSessionId` as a required argument.
3. `pattern_end_session(patternSessionId)` closes the daemon when the conversation is done with its
   pattern. Sessions idle for longer than `MCP_SESSION_IDLE_TIMEOUT_MS` are auto-closed by a
   background reaper, so an abandoned/crashed chat doesn't leak `actiond` processes forever.

This is a model-instruction-following contract (spelled out in the server's `instructions` and
repeated in `pattern_new_session`'s own description), not something MCP enforces — the model
is expected to call `pattern_new_session` once per conversation and never reuse an id across
conversations.

**Why the parameter is called `patternSessionId`, not `session_id`:** an earlier version used
`session_id`, and every tool call from Claude Desktop silently lost that one argument while every
other argument on the same call passed through untouched — confirmed by inspecting the raw
arguments the server actually received. Desktop appears to special-case and strip any tool
argument with that exact key, apparently conflating it with the transport-level `Mcp-Session-Id`
concept from the Streamable HTTP transport, even over stdio where that concept doesn't apply.
`patternSessionId` avoids the collision.

## Tools

- **Auto-generated, one per `actiond` action op** (e.g. `basePoint`, `endLine`, `piece_union`,
  `pattern_undo`, ...): same name (`.` replaced with `_`) and parameters as `actiond --list-tools
  --format=ai` reports, plus a required `patternSessionId`. On success, any op outside the catalogue's
  `introspection` category (plus `session.close`/`session.save`) also returns a rendered PNG
  snapshot of the draft alongside the JSON result, batched into the same request as the action
  itself.
- `pattern_new_session` / `pattern_end_session` — see above.
- `pattern_download_snapshot(patternSessionId, step?)` — re-fetch a snapshot already rendered this
  session (most recent by default).
- `pattern_download_val(patternSessionId)` — saves and returns the live pattern as a `.val` file.
- `pattern_export_dxf(patternSessionId, dxfVersion?)` — exports the draft to DXF (default
  `dxf-2013`; validated against the real `dxf-r10`..`dxf-2013` set before it ever reaches
  `actiond`).
- `pattern_set_measurements(patternSessionId, measurements, unit?, pm_system?)` — takes a flat
  `{name: value}` JSON object, generates the `.smis` measurement file `actiond` requires, and
  calls `measurements.sync` (load + recompute in one step). The raw `measurements_load` /
  `measurements_recompute` / `measurements_sync` tools (auto-generated, taking a file `path`)
  are also available for a person supplying an actual measurement file.

Binary files (`.val`, `.dxf`) come back as an embedded `resource` content block (base64 `blob`)
plus the absolute host filesystem path as text, so they're usable even in a client that doesn't
render embedded resources inline.

## Known limitation: measurements on a truly blank pattern

A session started fresh (no `.val` loaded) has its measurement type internally set to
`"unknown"` by `actiond` itself. `measurements.load`/`measurements.sync` (and so
`pattern_set_measurements`) currently reject any real measurement file against that pattern with
a `measurementTypeMismatch` error (`expected: "unknown"`) — this is `actiond`'s own behavior,
verified directly against the daemon outside this wrapper, not something introduced by this MCP
server. Loading measurements successfully today requires starting from a pattern file that
already establishes a measurement type. Worth revisiting once/if the action layer adds a way to
set a blank pattern's measurement type explicitly.

## Worked example conversation

> **User:** Start a new pattern and draw a 10cm square starting at the origin.
>
> **Claude:** *(calls `pattern_new_session` → gets `patternSessionId`)*
> *(calls `basePoint` with `name: "A", x: 0, y: 0, draftBlock: "Front"`, patternSessionId)* → sees the
> JSON result plus a snapshot PNG showing point A.
> *(calls `endLine` three more times to walk `A → B → C → D`, each call showing the growing
> square)*
> *(calls `piece_addPatternPiece` with `nodes: ["A","B","C","D"]`)* → snapshot shows the closed
> piece.
>
> **User:** Looks good — give me the DXF.
>
> **Claude:** *(calls `pattern_export_dxf` with the session's `patternSessionId`)* → returns the
> `.dxf` file.

## Development

```
npm run dev     # run src/index.ts directly via tsx, no build step
npm run build   # tsc -> dist/
npm start        # run the built dist/index.js
```
