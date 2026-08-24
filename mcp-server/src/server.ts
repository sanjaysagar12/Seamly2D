import path from 'node:path';
import { readFile, writeFile } from 'node:fs/promises';
import { pathToFileURL } from 'node:url';
import { Server } from '@modelcontextprotocol/sdk/server/index.js';
import { CallToolRequestSchema, ListToolsRequestSchema, type Tool, type CallToolResult } from '@modelcontextprotocol/sdk/types.js';
import type { ToolCatalogEntry } from './catalog.js';
import { SessionManager, SessionNotFoundError, type Session } from './sessionManager.js';
import { buildSmisXml } from './measurements.js';
import { config } from './config.js';

const SERVER_INSTRUCTIONS = `This server drives Seamly2D's headless pattern-construction engine (actiond) so you can build
sewing patterns step by step and see a rendered snapshot after each change.

IMPORTANT session rule: at the start of a new conversation, before calling any other pattern tool,
call pattern_new_session and remember the returned session_id. Pass that exact session_id as the
"session_id" argument on every other pattern/piece/point/curve/... tool call for the rest of THIS
conversation. Never reuse a session_id you saw in a earlier conversation — each new conversation
must call pattern_new_session again to get its own fresh, blank pattern. When you are done with a
pattern, call pattern_end_session to free the underlying process.

Most mutating actions (creating points, lines, pieces, operations, ...) automatically return a
rendered PNG snapshot of the current draft alongside the action's JSON result, so you can visually
verify each step as you build. Read-only/introspection actions (pattern.dump, render.snapshot,
export.scene, pattern.listMeasurements, pattern.listTools, pattern.resolveName) do not trigger an
extra snapshot.`;

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

type Content = CallToolResult['content'][number];

function mcpToolName(op: string): string {
  return op.replace(/\./g, '_');
}

function isAutoRenderExcluded(entry: ToolCatalogEntry): boolean {
  return entry.category === 'introspection' || NO_AUTORENDER_OPS.has(entry.name);
}

const SESSION_ID_PROPERTY = {
  type: 'string',
  description: 'Session id returned by pattern_new_session. Required on every call.',
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
        'Start a brand-new, isolated Seamly2D pattern session (blank pattern) and return its session_id. ' +
        'Call this once at the start of every new conversation, before any other pattern tool call, and reuse ' +
        'the returned session_id for every subsequent call in that conversation. Never reuse a session_id from ' +
        'an earlier conversation — always start a fresh session for a fresh conversation.',
      inputSchema: { type: 'object', properties: {}, required: [] },
    },
    {
      name: 'pattern_end_session',
      description: 'End a pattern session and release its underlying actiond process. Call when a conversation is done with its pattern.',
      inputSchema: { type: 'object', properties: { session_id: SESSION_ID_PROPERTY }, required: ['session_id'] },
    },
    {
      name: 'pattern_download_snapshot',
      description: 'Return a previously rendered draft snapshot PNG for this session (the most recent one by default).',
      inputSchema: {
        type: 'object',
        properties: {
          session_id: SESSION_ID_PROPERTY,
          step: { type: 'number', description: 'Specific step number to fetch. Omit for the most recently rendered snapshot.' },
        },
        required: ['session_id'],
      },
    },
    {
      name: 'pattern_download_val',
      description: "Save the session's live pattern to a .val file and return it.",
      inputSchema: { type: 'object', properties: { session_id: SESSION_ID_PROPERTY }, required: ['session_id'] },
    },
    {
      name: 'pattern_export_dxf',
      description: 'Export the current draft to DXF and return the file.',
      inputSchema: {
        type: 'object',
        properties: {
          session_id: SESSION_ID_PROPERTY,
          dxfVersion: {
            type: 'string',
            description: `DXF version to export. Defaults to dxf-2013.`,
            enum: DXF_FORMATS,
          },
        },
        required: ['session_id'],
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
          session_id: SESSION_ID_PROPERTY,
          measurements: {
            type: 'object',
            description: 'Map of measurement name to numeric value, e.g. {"height": 173, "bust_circ": 102}.',
            additionalProperties: { type: 'number' },
          },
          unit: { type: 'string', description: 'Measurement unit. Defaults to "cm".' },
          pm_system: { type: 'string', description: 'Pattern-making system id. Defaults to "998".' },
        },
        required: ['session_id', 'measurements'],
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
        session_id: SESSION_ID_PROPERTY,
        ...(entry.input_schema.properties ?? {}),
      },
      required: ['session_id', ...(entry.input_schema.required ?? [])],
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
          return await handleNewSession(sessions);
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
  const id = args.session_id;
  if (typeof id !== 'string' || id.length === 0) {
    throw new Error('"session_id" is required. Call pattern_new_session first and pass its session_id.');
  }
  return id;
}

async function handleNewSession(sessions: SessionManager): Promise<CallToolResult> {
  const session = await sessions.createSession();
  return {
    content: [
      {
        type: 'text',
        text: JSON.stringify(
          {
            session_id: session.id,
            message:
              'New pattern session created with a blank pattern. Use this exact session_id for every ' +
              'subsequent pattern tool call in this conversation. Do not reuse it in a future conversation.',
          },
          null,
          2
        ),
      },
    ],
  };
}

async function handleEndSession(sessions: SessionManager, args: Record<string, unknown>): Promise<CallToolResult> {
  const id = requireSessionId(args);
  await sessions.endSession(id);
  return { content: [{ type: 'text', text: `Session ${id} closed.` }] };
}

async function handleGeneratedAction(
  sessions: SessionManager,
  entry: ToolCatalogEntry,
  args: Record<string, unknown>
): Promise<CallToolResult> {
  const id = requireSessionId(args);
  const session = sessions.get(id);
  const { session_id: _drop, ...opArgs } = args;
  const action = { op: entry.name, ...opArgs };
  const needsRender = !isAutoRenderExcluded(entry);

  const actions: unknown[] = [action];
  let snapshotFile: string | null = null;
  if (needsRender) {
    session.stepCounter += 1;
    snapshotFile = `step_${session.stepCounter}.png`;
    actions.push({ op: 'render.snapshot', path: snapshotFile, width: config.snapshotWidth });
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

  const isError = primary ? primary.status !== 'ok' : response.status !== 'ok';
  return { content, isError };
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
