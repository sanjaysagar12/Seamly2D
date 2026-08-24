import { randomUUID } from 'node:crypto';
import { mkdir } from 'node:fs/promises';
import path from 'node:path';
import { config, buildActiondInvocation } from './config.js';
import { ActiondProcess } from './actiondProcess.js';

export interface Session {
  id: string;
  proc: ActiondProcess;
  /** Host filesystem directory backing this session's --output-dir (bind-mounted in docker mode). */
  outputDirHost: string;
  createdAt: number;
  lastActivity: number;
  stepCounter: number;
  lastSnapshotFile: string | null;
}

export class SessionNotFoundError extends Error {
  constructor(id: string) {
    super(
      `No active pattern session for session_id "${id}". It may have been closed or reaped for inactivity. ` +
        `Call pattern_new_session to start a new one — session ids never carry over between conversations.`
    );
  }
}

export class SessionManager {
  private sessions = new Map<string, Session>();
  private reaperHandle: NodeJS.Timeout;

  constructor() {
    const interval = Math.max(30_000, Math.min(config.idleTimeoutMs, 5 * 60 * 1000));
    this.reaperHandle = setInterval(() => this.reapIdleSessions(), interval);
    this.reaperHandle.unref();
  }

  async createSession(): Promise<Session> {
    const id = randomUUID();
    const outputDirHost = path.join(config.sessionsDir, id, 'output');
    await mkdir(outputDirHost, { recursive: true });
    const { command, args, outputDirForFlag } = buildActiondInvocation(outputDirHost);
    const proc = new ActiondProcess(command, [...args, '--output-dir', outputDirForFlag]);

    // Smoke-test the daemon before handing the session_id back, so a broken ACTIOND_COMMAND
    // fails loudly here instead of silently hanging on the caller's first real action.
    let probe: any;
    try {
      probe = await proc.sendRequest([{ op: 'pattern.dump' }]);
    } catch (err) {
      proc.kill();
      throw new Error(`Failed to start actiond session: ${(err as Error).message}`);
    }
    if (probe?.status !== 'ok') {
      proc.kill();
      throw new Error(`New actiond session failed its startup smoke test: ${JSON.stringify(probe)}`);
    }

    const session: Session = {
      id,
      proc,
      outputDirHost,
      createdAt: Date.now(),
      lastActivity: Date.now(),
      stepCounter: 0,
      lastSnapshotFile: null,
    };
    this.sessions.set(id, session);
    return session;
  }

  get(id: string): Session {
    const session = this.sessions.get(id);
    if (!session || !session.proc.isAlive()) {
      if (session) this.sessions.delete(id);
      throw new SessionNotFoundError(id);
    }
    session.lastActivity = Date.now();
    return session;
  }

  async endSession(id: string): Promise<void> {
    const session = this.sessions.get(id);
    if (!session) throw new SessionNotFoundError(id);
    this.sessions.delete(id);
    await session.proc.close();
  }

  private reapIdleSessions(): void {
    const now = Date.now();
    for (const [id, session] of this.sessions) {
      if (now - session.lastActivity > config.idleTimeoutMs) {
        this.sessions.delete(id);
        session.proc.close().catch(() => session.proc.kill());
      }
    }
  }

  async shutdownAll(): Promise<void> {
    clearInterval(this.reaperHandle);
    const ids = [...this.sessions.keys()];
    await Promise.all(ids.map((id) => this.endSession(id).catch(() => undefined)));
  }
}
