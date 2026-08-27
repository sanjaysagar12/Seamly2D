import path from 'node:path';
import { readFile, writeFile } from 'node:fs/promises';
import { pathToFileURL } from 'node:url';
import { Server } from '@modelcontextprotocol/sdk/server/index.js';
import { CallToolRequestSchema, ListToolsRequestSchema, type Tool, type CallToolResult } from '@modelcontextprotocol/sdk/types.js';
import type { ToolCatalogEntry } from './catalog.js';
import { SessionManager, SessionNotFoundError, type Session } from './sessionManager.js';
import { buildSmisXml } from './measurements.js';
import { config } from './config.js';
import { resolveUploadPath } from './uploads.js';

const SERVER_INSTRUCTIONS = `This server drives Seamly2D's headless pattern-construction engine (actiond) so you can build
sewing patterns step by step and see a rendered snapshot after each change.

IMPORTANT session rule: at the start of a new conversation, before calling any other pattern tool,
call pattern_new_session and remember the returned patternSessionId. Pass that exact patternSessionId as the
"patternSessionId" argument on every other pattern/piece/point/curve/... tool call for the rest of THIS
conversation. Never reuse a patternSessionId you saw in a earlier conversation — each new conversation
must call pattern_new_session again to get its own fresh session. When you are done with a
pattern, call pattern_end_session to free the underlying process.

If the person wants to start from an existing pattern (.val/.sm2d) or measurements (.smis/.smms/.vst)
file, ask them for the filename (or full path) of the file(s) they want to use, then pass it to
pattern_new_session via patternFile/measurementsFile. A bare filename is resolved against this
server's local uploads folder — tell the person to place their file there first if they haven't
already. Do not attempt to read or transcribe the file's contents yourself — just pass the filename
or path through; do not attempt to manually re-derive the pattern's geometry through individual
point/line/curve tool calls either, that's slower, error-prone, and unnecessary now that direct
loading is supported.

Most mutating actions (creating points, lines, pieces, operations, ...) automatically return a
rendered PNG snapshot of the current draft alongside the action's JSON result, so you can visually
verify each step as you build. Read-only/introspection actions (pattern.dump, render.snapshot,
export.scene, pattern.listMeasurements, pattern.listTools, pattern.resolveName) do not trigger an
extra snapshot.

A pattern can contain more than one garment piece (e.g. a Front, a Back, a Sleeve) — what a person
often calls a "layer". pattern_new_session's response tells you how many pieces an uploaded pattern
already contains. Whenever a mutating call references an existing piece by name (piece.addAnchorPoint,
piece.internalPath, piece.insertNodes) or creates one (piece.addPatternPiece), you automatically get a
close-up target:"piece" snapshot of that specific piece alongside the whole-draft snapshot — use it to
judge that piece's own shape, since it can be hard to see in the combined draft view once there is more
than one piece. If the person asks to see "each piece"/"each layer"/"every piece" of a pattern — most
commonly right after loading one with more than one piece — call pattern_snapshot_pieces instead of a
plain render.snapshot: it renders every piece as its own separate, clearly-labeled image rather than
one combined view where pieces overlap. When you go on to edit or discuss one specific piece out of
several, keep working on that one piece at a time (call piece.list first if you're unsure which ones
already exist) rather than mixing changes to different pieces in the same step.`;

// Ops that don't mutate the live pattern (or, for session.close/session.save, can't usefully be
// followed by a render) skip the auto-render step. Everything else in the catalogue is treated as
// mutating. Preferring the catalogue's own `category` field over a hand-maintained op list keeps
// this correct as the action catalogue grows.
const NO_AUTORENDER_OPS = new Set(['session.close', 'session.save']);
const DXF_FORMATS = [
  'dxf-r10',
  'dxf-r12',
  'dxf-r13',
  'dxf-r14',
  'dxf-2000',
  'dxf-2004',
  'dxf-2007',
  'dxf-2010',
  'dxf-2013',
];
const PATTERN_FILE_EXTENSIONS = ['.val', '.sm2d'];
const MEASUREMENTS_FILE_EXTENSIONS = ['.smis', '.smms', '.vst'];

type Content = CallToolResult['content'][number];

function mcpToolName(op: string): string {
  return op.replace(/\./g, '_');
}

function isAutoRenderExcluded(entry: ToolCatalogEntry): boolean {
  return entry.category === 'introspection' || NO_AUTORENDER_OPS.has(entry.name);
}

const SESSION_ID_PROPERTY = {
  type: 'string',
  // Deliberately not named "session_id" on the wire: some MCP clients special-case and strip
  // any tool argument with that exact key (confirmed against Claude Desktop — every other
  // argument on a call passed through untouched while a literal "session_id" key vanished
  // every single time), presumably conflating it with the transport-level Mcp-Session-Id
  // concept even over stdio, where that concept doesn't apply. "patternSessionId" avoids it.
  description: 'Pattern session id returned by pattern_new_session. Required on every call.',
} as const;

export function buildServer(catalog: ToolCatalogEntry[]): Server {
  const sessions = new SessionManager();
  const opByToolName = new Map<string, ToolCatalogEntry>();
  for (const entry of catalog) {
    opByToolName.set(mcpToolName(entry.name), entry);
  }

  const staticTools: Tool[] = [
    {
      name: 'pattern_new_session',
      description:
        'Start a new Seamly2D pattern session and return its session_id. To start from an existing pattern ' +
        'and/or measurements file instead of a blank pattern, ask the person for the filename (or full path) of ' +
        'the file(s) they want to use, then pass it via patternFile and/or measurementsFile. A bare filename ' +
        '(e.g. "Aldrich.sm2d") is resolved against this server\'s local uploads folder — tell the person to ' +
        "place their file(s) there first if they haven't already. A full path (e.g. \"/Users/name/patterns/x.val\" " +
        'or "C:\\\\Users\\\\name\\\\x.val") is used as-is. Omit both to start from a blank pattern with no ' +
        'measurements, as before. Call this once at the start of every new conversation, before any other ' +
        'pattern tool call, and reuse the returned session_id for every subsequent call in that conversation.',
      inputSchema: {
        type: 'object',
        properties: {
          patternFile: {
            type: 'string',
            description:
              'Filename (resolved against the uploads folder) or full path of an existing .val or .sm2d ' +
              "pattern file to load as this session's starting pattern. Omit to start blank.",
          },
          measurementsFile: {
            type: 'string',
            description:
              'Filename (resolved against the uploads folder) or full path of an existing .smis/.smms/.vst ' +
              'measurements file to load alongside the pattern (or on its own, into a blank pattern). Omit to ' +
              'start with no measurements loaded.',
          },
        },
        required: [],
      },
    },
    {
      name: 'pattern_end_session',
      description: 'End a pattern session and release its underlying actiond process. Call when a conversation is done with its pattern.',
      inputSchema: { type: 'object', properties: { patternSessionId: SESSION_ID_PROPERTY }, required: ['patternSessionId'] },
    },
    {
      name: 'pattern_download_snapshot',
      description: 'Return a previously rendered draft snapshot PNG for this session (the most recent one by default).',
      inputSchema: {
        type: 'object',
        properties: {
          patternSessionId: SESSION_ID_PROPERTY,
          step: { type: 'number', description: 'Specific step number to fetch. Omit for the most recently rendered snapshot.' },
        },
        required: ['patternSessionId'],
      },
    },
    {
      name: 'pattern_download_val',
      description: "Save the session's live pattern to a .val file and return it.",
      inputSchema: { type: 'object', properties: { patternSessionId: SESSION_ID_PROPERTY }, required: ['patternSessionId'] },
    },
    {
      name: 'pattern_export_dxf',
      description: 'Export the current draft to DXF and return the file.',
      inputSchema: {
        type: 'object',
        properties: {
          patternSessionId: SESSION_ID_PROPERTY,
          dxfVersion: {
            type: 'string',
            description: `DXF version to export. Defaults to dxf-2013.`,
            enum: DXF_FORMATS,
          },
        },
        required: ['patternSessionId'],
      },
    },
    {
      name: 'pattern_set_measurements',
      description:
        'Upload individual (single-size) measurements as a {name: value} JSON object. Generates the .smis ' +
        'file actiond requires, then loads and recomputes the pattern against it in one step (measurements.sync).',
      inputSchema: {
        type: 'object',
        properties: {
          patternSessionId: SESSION_ID_PROPERTY,
          measurements: {
            type: 'object',
            description: 'Map of measurement name to numeric value, e.g. {"height": 173, "bust_circ": 102}.',
            additionalProperties: { type: 'number' },
          },
          unit: { type: 'string', description: 'Measurement unit. Defaults to "cm".' },
          pm_system: { type: 'string', description: 'Pattern-making system id. Defaults to "998".' },
        },
        required: ['patternSessionId', 'measurements'],
      },
    },
    {
      name: 'pattern_snapshot_pieces',
      description:
        'Render one close-up snapshot per pattern piece (garment piece — Front, Back, Sleeve, etc; what a person ' +
        'often calls a "layer") currently in the session, instead of a single combined whole-draft view. Use this ' +
        'whenever the person asks to see "each piece", "each layer", or "every piece" of a pattern that may ' +
        'contain more than one — a plain render.snapshot only shows the whole draft with every piece overlapping ' +
        'in the same view, which is hard to read once there is more than one piece. Internally calls piece.list ' +
        'to discover every piece, then render.snapshot(target: "piece") once per piece so each comes back as its ' +
        'own clearly-labeled image. Falls back to a single whole-draft snapshot (with a note) if the pattern has ' +
        'no pieces yet.',
      inputSchema: {
        type: 'object',
        properties: { patternSessionId: SESSION_ID_PROPERTY },
        required: ['patternSessionId'],
      },
    },
  ];

  const generatedTools: Tool[] = catalog.map((entry) => ({
    name: mcpToolName(entry.name),
    description:
      entry.description +
      (isAutoRenderExcluded(entry)
        ? ''
        : ' (On success, a rendered PNG snapshot of the draft is returned alongside the JSON result.)') +
      (entry.status === 'partial' && entry.statusReason ? ` [KNOWN GAP: ${entry.statusReason}]` : ''),
    inputSchema: {
      type: 'object',
      properties: {
        patternSessionId: SESSION_ID_PROPERTY,
        ...(entry.input_schema.properties ?? {}),
      },
      required: ['patternSessionId', ...(entry.input_schema.required ?? [])],
    },
  }));

  const server = new Server(
    { name: 'seamly2d-pattern', version: '0.1.0' },
    { capabilities: { tools: {} }, instructions: SERVER_INSTRUCTIONS }
  );

  server.setRequestHandler(ListToolsRequestSchema, async () => ({
    tools: [...staticTools, ...generatedTools],
  }));

  server.setRequestHandler(CallToolRequestSchema, async (request): Promise<CallToolResult> => {
    const { name, arguments: rawArgs } = request.params;
    const args = (rawArgs ?? {}) as Record<string, unknown>;
    try {
      switch (name) {
        case 'pattern_new_session':
          return await handleNewSession(sessions, args);
        case 'pattern_end_session':
          return await handleEndSession(sessions, args);
        case 'pattern_download_snapshot':
          return await handleDownloadSnapshot(sessions, args);
        case 'pattern_download_val':
          return await handleDownloadVal(sessions, args);
        case 'pattern_export_dxf':
          return await handleExportDxf(sessions, args);
        case 'pattern_set_measurements':
          return await handleSetMeasurements(sessions, args);
        case 'pattern_snapshot_pieces':
          return await handleSnapshotPieces(sessions, args);
        default: {
          const entry = opByToolName.get(name);
          if (!entry) return errorResult(`Unknown tool "${name}".`);
          return await handleGeneratedAction(sessions, entry, args);
        }
      }
    } catch (err) {
      if (err instanceof SessionNotFoundError) return errorResult(err.message);
      return errorResult(err instanceof Error ? err.message : String(err));
    }
  });

  return server;
}

function errorResult(message: string): CallToolResult {
  return { content: [{ type: 'text', text: message }], isError: true };
}

function requireSessionId(args: Record<string, unknown>): string {
  const id = args.patternSessionId;
  if (typeof id !== 'string' || id.length === 0) {
    throw new Error(
      '"patternSessionId" is required. Call pattern_new_session first and pass its patternSessionId. ' +
        `(received argument keys: ${JSON.stringify(Object.keys(args))}, patternSessionId value: ${JSON.stringify(id)})`
    );
  }
  return id;
}

async function handleNewSession(sessions: SessionManager, args: Record<string, unknown>): Promise<CallToolResult> {
  const patternFileArg = args.patternFile;
  const measurementsFileArg = args.measurementsFile;
  if (patternFileArg !== undefined && typeof patternFileArg !== 'string') {
    return errorResult('"patternFile" must be a string.');
  }
  if (measurementsFileArg !== undefined && typeof measurementsFileArg !== 'string') {
    return errorResult('"measurementsFile" must be a string.');
  }

  let patternFilePath: string | undefined;
  let measurementsFilePath: string | undefined;
  try {
    if (patternFileArg) {
      patternFilePath = await resolveUploadPath('Pattern file', patternFileArg, PATTERN_FILE_EXTENSIONS);
    }
    if (measurementsFileArg) {
      measurementsFilePath = await resolveUploadPath('Measurements file', measurementsFileArg, MEASUREMENTS_FILE_EXTENSIONS);
    }
  } catch (err) {
    return errorResult((err as Error).message);
  }

  const session = await sessions.createSession({ patternFilePath, measurementsFilePath });

  const loadedParts = [
    patternFilePath ? `pattern file "${patternFileArg}"` : null,
    measurementsFilePath ? `measurements file "${measurementsFileArg}"` : null,
  ].filter((p): p is string => p !== null);

  // Best-effort: tells the model right away whether an uploaded pattern already contains more
  // than one piece, so it knows to reach for pattern_snapshot_pieces / work one piece at a time
  // instead of treating the file as a single unit. A failure here shouldn't fail session creation.
  let pieceNames: string[] = [];
  if (patternFilePath) {
    try {
      pieceNames = await listPieceNames(session);
    } catch {
      // omit piece info from the response rather than failing session creation over it
    }
  }
  const pieceNote =
    pieceNames.length > 0
      ? ` This pattern already contains ${pieceNames.length} piece(s): ${pieceNames.join(', ')}. If asked to ` +
        'show each piece/layer, call pattern_snapshot_pieces rather than a single whole-draft render.snapshot.'
      : '';

  const message =
    loadedParts.length > 0
      ? `New pattern session created, loaded from ${loadedParts.join(' and ')}.${pieceNote} Use this exact ` +
        'patternSessionId for every subsequent pattern tool call in this conversation. Do not reuse it in a ' +
        'future conversation.'
      : 'New pattern session created with a blank pattern. Use this exact patternSessionId for every ' +
        'subsequent pattern tool call in this conversation. Do not reuse it in a future conversation.';

  return {
    content: [
      {
        type: 'text',
        text: JSON.stringify({ patternSessionId: session.id, message }, null, 2),
      },
    ],
  };
}

async function handleEndSession(sessions: SessionManager, args: Record<string, unknown>): Promise<CallToolResult> {
  const id = requireSessionId(args);
  await sessions.endSession(id);
  return { content: [{ type: 'text', text: `Session ${id} closed.` }] };
}

/** The piece a mutating action just touched, if any — either the piece it created
 *  (piece.addPatternPiece's "name") or the existing piece it referenced ("piece", used by
 *  piece.addAnchorPoint/internalPath/insertNodes). Undefined for anything else, including
 *  piece.union (piece1/piece2 — also excluded deliberately, since that op is a known crasher). */
function derivePieceNameForCloseup(entry: ToolCatalogEntry, opArgs: Record<string, unknown>): string | undefined {
  if (entry.name === 'piece.addPatternPiece' && typeof opArgs.name === 'string') return opArgs.name;
  if (typeof opArgs.piece === 'string') return opArgs.piece;
  return undefined;
}

async function handleGeneratedAction(
  sessions: SessionManager,
  entry: ToolCatalogEntry,
  args: Record<string, unknown>
): Promise<CallToolResult> {
  const id = requireSessionId(args);
  const session = sessions.get(id);
  const { patternSessionId: _drop, ...opArgs } = args;
  const action = { op: entry.name, ...opArgs };
  const needsRender = !isAutoRenderExcluded(entry);

  const actions: unknown[] = [action];
  let snapshotFile: string | null = null;
  if (needsRender) {
    session.stepCounter += 1;
    snapshotFile = `step_${session.stepCounter}.png`;
    actions.push({ op: 'render.snapshot', path: snapshotFile, width: config.snapshotWidth });
  }

  // Mirrors the reference pattern-agent's "current piece" behavior: whenever an action touches a
  // specific piece by name, also grab a close-up target:"piece" render of just that piece, since
  // it can be hard to judge one piece's own shape from the combined whole-draft view once there is
  // more than one piece in the pattern.
  const pieceNameForCloseup = needsRender ? derivePieceNameForCloseup(entry, opArgs) : undefined;
  let pieceSnapshotFile: string | null = null;
  if (pieceNameForCloseup) {
    pieceSnapshotFile = `step_${session.stepCounter}_piece.png`;
    actions.push({
      op: 'render.snapshot',
      path: pieceSnapshotFile,
      target: 'piece',
      piece: pieceNameForCloseup,
      width: config.snapshotWidth,
    });
  }

  const response = await session.proc.sendRequest(actions);
  const primary = response.results?.[0];
  const content: Content[] = [{ type: 'text', text: JSON.stringify(primary ?? response, null, 2) }];

  if (needsRender && snapshotFile) {
    const renderResult = response.results?.[1];
    if (renderResult?.status === 'ok') {
      appendSnapshot(content, await readFile(path.join(session.outputDirHost, snapshotFile)));
      session.lastSnapshotFile = snapshotFile;
    } else if (renderResult) {
      content.push({ type: 'text', text: `(render.snapshot failed: ${JSON.stringify(renderResult.error)})` });
    }
  }

  if (pieceSnapshotFile) {
    const pieceRenderResult = response.results?.[2];
    if (pieceRenderResult?.status === 'ok') {
      content.push({ type: 'text', text: `Close-up of piece "${pieceNameForCloseup}":` });
      appendSnapshot(content, await readFile(path.join(session.outputDirHost, pieceSnapshotFile)));
    } else if (pieceRenderResult) {
      content.push({ type: 'text', text: `(piece close-up snapshot failed: ${JSON.stringify(pieceRenderResult.error)})` });
    }
  }

  const isError = primary ? primary.status !== 'ok' : response.status !== 'ok';
  return { content, isError };
}

/** Best-effort piece.list call, returning just the piece names (empty array on any failure). */
async function listPieceNames(session: Session): Promise<string[]> {
  const response = await session.proc.sendRequest([{ op: 'piece.list' }]);
  const result = response.results?.[0];
  if (result?.status !== 'ok') return [];
  const pieces = result.result?.pieces;
  if (!Array.isArray(pieces)) return [];
  return pieces.map((p: any) => p.name).filter((n: unknown): n is string => typeof n === 'string');
}

async function handleSnapshotPieces(sessions: SessionManager, args: Record<string, unknown>): Promise<CallToolResult> {
  const id = requireSessionId(args);
  const session = sessions.get(id);

  const listResponse = await session.proc.sendRequest([{ op: 'piece.list' }]);
  const listResult = listResponse.results?.[0];
  if (listResult?.status !== 'ok') {
    return errorResult(`piece.list failed: ${JSON.stringify(listResult?.error ?? listResponse.error)}`);
  }
  const pieces: Array<{ name: string }> = Array.isArray(listResult.result?.pieces) ? listResult.result.pieces : [];

  if (pieces.length === 0) {
    session.stepCounter += 1;
    const snapshotFile = `step_${session.stepCounter}.png`;
    const response = await session.proc.sendRequest([
      { op: 'render.snapshot', path: snapshotFile, width: config.snapshotWidth },
    ]);
    const renderResult = response.results?.[0];
    const content: Content[] = [
      { type: 'text', text: 'This pattern has no pieces yet — showing the whole draft instead.' },
    ];
    if (renderResult?.status === 'ok') {
      appendSnapshot(content, await readFile(path.join(session.outputDirHost, snapshotFile)));
      session.lastSnapshotFile = snapshotFile;
      return { content };
    }
    return errorResult(`render.snapshot failed: ${JSON.stringify(renderResult?.error ?? response.error)}`);
  }

  const content: Content[] = [
    { type: 'text', text: `Found ${pieces.length} piece(s): ${pieces.map((p) => p.name).join(', ')}` },
  ];
  for (const piece of pieces) {
    session.stepCounter += 1;
    const snapshotFile = `step_${session.stepCounter}_piece.png`;
    const response = await session.proc.sendRequest([
      { op: 'render.snapshot', path: snapshotFile, target: 'piece', piece: piece.name, width: config.snapshotWidth },
    ]);
    const renderResult = response.results?.[0];
    if (renderResult?.status === 'ok') {
      content.push({ type: 'text', text: `Piece "${piece.name}":` });
      appendSnapshot(content, await readFile(path.join(session.outputDirHost, snapshotFile)));
    } else {
      content.push({ type: 'text', text: `Piece "${piece.name}" snapshot failed: ${JSON.stringify(renderResult?.error)}` });
    }
  }
  return { content };
}

function appendSnapshot(content: Content[], buf: Buffer): void {
  content.push({ type: 'image', data: buf.toString('base64'), mimeType: 'image/png' });
}

async function handleDownloadSnapshot(sessions: SessionManager, args: Record<string, unknown>): Promise<CallToolResult> {
  const id = requireSessionId(args);
  const session = sessions.get(id);
  const step = args.step;
  const file = typeof step === 'number' ? `step_${step}.png` : session.lastSnapshotFile;
  if (!file) return errorResult('No snapshot has been rendered in this session yet.');

  const filePath = path.join(session.outputDirHost, file);
  let buf: Buffer;
  try {
    buf = await readFile(filePath);
  } catch (err) {
    return errorResult(`Could not read snapshot "${file}": ${(err as Error).message}`);
  }
  const content: Content[] = [{ type: 'text', text: `Snapshot: ${filePath}` }];
  appendSnapshot(content, buf);
  return { content };
}

async function handleDownloadVal(sessions: SessionManager, args: Record<string, unknown>): Promise<CallToolResult> {
  const id = requireSessionId(args);
  const session = sessions.get(id);
  const response = await session.proc.sendRequest([{ op: 'session.save', path: 'pattern.val' }]);
  const result = response.results?.[0];
  if (result?.status !== 'ok') {
    return errorResult(`session.save failed: ${JSON.stringify(result?.error ?? response.error)}`);
  }
  const filePath = path.join(session.outputDirHost, 'pattern.val');
  const buf = await readFile(filePath);
  return {
    content: [
      { type: 'text', text: `Pattern saved to: ${filePath}` },
      {
        type: 'resource',
        resource: { uri: pathToFileURL(filePath).toString(), mimeType: 'application/xml', blob: buf.toString('base64') },
      },
    ],
  };
}

async function handleExportDxf(sessions: SessionManager, args: Record<string, unknown>): Promise<CallToolResult> {
  const id = requireSessionId(args);
  const session = sessions.get(id);
  const dxfVersion = (args.dxfVersion as string | undefined) ?? 'dxf-2013';
  if (!DXF_FORMATS.includes(dxfVersion)) {
    return errorResult(`Invalid dxfVersion "${dxfVersion}". Must be one of: ${DXF_FORMATS.join(', ')}.`);
  }
  const response = await session.proc.sendRequest([{ op: 'export.scene', path: 'pattern.dxf', format: dxfVersion }]);
  const result = response.results?.[0];
  if (result?.status !== 'ok') {
    return errorResult(`export.scene failed: ${JSON.stringify(result?.error ?? response.error)}`);
  }
  const filePath = path.join(session.outputDirHost, 'pattern.dxf');
  const buf = await readFile(filePath);
  return {
    content: [
      { type: 'text', text: `Pattern exported to: ${filePath}` },
      {
        type: 'resource',
        resource: { uri: pathToFileURL(filePath).toString(), mimeType: 'application/dxf', blob: buf.toString('base64') },
      },
    ],
  };
}

async function handleSetMeasurements(sessions: SessionManager, args: Record<string, unknown>): Promise<CallToolResult> {
  const id = requireSessionId(args);
  const session = sessions.get(id);
  const measurements = args.measurements;
  if (!measurements || typeof measurements !== 'object' || Array.isArray(measurements)) {
    return errorResult('"measurements" must be an object of {name: numeric value}.');
  }

  let xml: string;
  try {
    xml = buildSmisXml(measurements as Record<string, number>, {
      unit: args.unit as string | undefined,
      pm_system: args.pm_system as string | undefined,
    });
  } catch (err) {
    return errorResult((err as Error).message);
  }
  await writeFile(path.join(session.outputDirHost, 'measurements.smis'), xml, 'utf8');

  session.stepCounter += 1;
  const snapshotFile = `step_${session.stepCounter}.png`;
  const response = await session.proc.sendRequest([
    { op: 'measurements.sync', path: 'measurements.smis' },
    { op: 'render.snapshot', path: snapshotFile, width: config.snapshotWidth },
  ]);
  const syncResult = response.results?.[0];
  const content: Content[] = [{ type: 'text', text: JSON.stringify(syncResult ?? response, null, 2) }];

  const renderResult = response.results?.[1];
  if (renderResult?.status === 'ok') {
    appendSnapshot(content, await readFile(path.join(session.outputDirHost, snapshotFile)));
    session.lastSnapshotFile = snapshotFile;
  }

  const isError = syncResult ? syncResult.status !== 'ok' : response.status !== 'ok';
  return { content, isError };
}
