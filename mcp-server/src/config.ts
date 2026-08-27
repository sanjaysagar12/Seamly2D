import path from 'node:path';
import { mkdir } from 'node:fs/promises';
import { fileURLToPath } from 'node:url';

const __dirname = path.dirname(fileURLToPath(import.meta.url));
const packageRoot = path.resolve(__dirname, '..');

const SESSION_DIR_PLACEHOLDER = '<session-dir>';

function parseActiondCommand(): string[] {
  const raw = process.env.ACTIOND_COMMAND;
  if (!raw) return ['actiond'];
  let parsed: unknown;
  try {
    parsed = JSON.parse(raw);
  } catch (err) {
    throw new Error(`ACTIOND_COMMAND must be a JSON array of strings (e.g. ["actiond"]): ${(err as Error).message}`);
  }
  if (!Array.isArray(parsed) || parsed.length === 0 || parsed.some((p) => typeof p !== 'string')) {
    throw new Error('ACTIOND_COMMAND must be a non-empty JSON array of strings.');
  }
  return parsed as string[];
}

export const config = {
  /** Command+args used to invoke actiond. May contain the literal token "<session-dir>", which is
   *  replaced per-session with that session's host output directory (used for a `docker run -v` mount). */
  actiondCommandTemplate: parseActiondCommand(),
  /** Where actiond itself sees its output directory when run in a container (i.e. the mount target). */
  containerOutputDir: process.env.ACTIOND_CONTAINER_OUTPUT_DIR ?? '/data/output',
  /** Host directory under which each session gets its own subdirectory. */
  sessionsDir: process.env.MCP_SESSIONS_DIR ?? path.join(packageRoot, 'sessions'),
  /** Where the person manually places pattern/measurement files to be loaded into a new session. */
  uploadsDir: process.env.MCP_UPLOADS_DIR ?? path.join(packageRoot, 'uploads'),
  /** A session with no tool call against it for this long is auto-closed. */
  idleTimeoutMs: Number(process.env.MCP_SESSION_IDLE_TIMEOUT_MS ?? 30 * 60 * 1000),
  /** How long to wait for a single actiond request/response round trip before treating it as hung. */
  requestTimeoutMs: Number(process.env.MCP_ACTIOND_REQUEST_TIMEOUT_MS ?? 30_000),
  /** Grace period after sending session.close before we SIGKILL a still-running actiond process. */
  processExitGraceMs: Number(process.env.MCP_ACTIOND_EXIT_GRACE_MS ?? 5_000),
  /** Default pixel width for auto-rendered snapshots after a mutating action. */
  snapshotWidth: Number(process.env.MCP_SNAPSHOT_WIDTH ?? 800),
};

/** Creates the uploads directory on startup if it doesn't already exist. */
export async function ensureUploadsDir(): Promise<void> {
  await mkdir(config.uploadsDir, { recursive: true });
}

export interface ActiondInvocation {
  command: string;
  args: string[];
  /** The --output-dir value to pass to actiond itself: the host path in local mode, or the
   *  container mount path in docker mode (where hostOutputDir was substituted into a `-v` flag). */
  outputDirForFlag: string;
}

/** Resolves ACTIOND_COMMAND into a concrete argv for one session, given that session's host output dir. */
export function buildActiondInvocation(hostOutputDir: string): ActiondInvocation {
  const template = config.actiondCommandTemplate;
  const isContainerized = template.some((part) => part.includes(SESSION_DIR_PLACEHOLDER));
  // The placeholder may appear embedded in a larger arg, e.g. "<session-dir>:/data/output" for `-v`,
  // so this is a substring replace within each arg, not a whole-element swap.
  const substituted = template.map((part) => part.split(SESSION_DIR_PLACEHOLDER).join(hostOutputDir));
  const [command, ...args] = substituted;
  return {
    command,
    args,
    outputDirForFlag: isContainerized ? config.containerOutputDir : hostOutputDir,
  };
}
