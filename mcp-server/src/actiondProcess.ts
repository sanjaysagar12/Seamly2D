import { spawn, type ChildProcessWithoutNullStreams } from 'node:child_process';
import readline from 'node:readline';
import { randomUUID } from 'node:crypto';
import { config } from './config.js';

export class ActiondError extends Error {}
export class ActiondCrashedError extends ActiondError {}
export class ActiondTimeoutError extends ActiondError {}

interface PendingRequest {
  resolve: (value: unknown) => void;
  reject: (err: Error) => void;
}

/**
 * Wraps one `actiond` daemon subprocess speaking the NDJSON session protocol: one JSON request
 * per stdin line, one JSON response per stdout line. The daemon processes requests strictly one
 * at a time, so callers here are serialized through a single-file queue rather than matched by id.
 */
export class ActiondProcess {
  private child: ChildProcessWithoutNullStreams;
  private rl: readline.Interface;
  private queueTail: Promise<unknown> = Promise.resolve();
  private pending: PendingRequest | null = null;
  private alive = true;
  private stderrTail = '';

  constructor(command: string, args: string[]) {
    this.child = spawn(command, args, { stdio: ['pipe', 'pipe', 'pipe'] });
    this.rl = readline.createInterface({ input: this.child.stdout });
    this.rl.on('line', (line) => this.handleLine(line));
    this.child.stderr.on('data', (chunk: Buffer) => {
      this.stderrTail = (this.stderrTail + chunk.toString()).slice(-4000);
    });
    this.child.on('exit', () => {
      this.alive = false;
      this.failPending(new ActiondCrashedError(`actiond process exited unexpectedly.${this.stderrTail ? ` stderr: ${this.stderrTail}` : ''}`));
    });
    this.child.on('error', (err) => {
      this.alive = false;
      this.failPending(new ActiondCrashedError(`failed to spawn/run actiond ("${command}"): ${err.message}`));
    });
  }

  private failPending(err: Error) {
    if (this.pending) {
      this.pending.reject(err);
      this.pending = null;
    }
  }

  private handleLine(line: string) {
    if (!this.pending) return; // unsolicited output (shouldn't happen); ignore rather than crash
    let parsed: unknown;
    try {
      parsed = JSON.parse(line);
    } catch {
      return; // ignore a malformed/partial line rather than resolve garbage
    }
    const p = this.pending;
    this.pending = null;
    p.resolve(parsed);
  }

  isAlive(): boolean {
    return this.alive;
  }

  /** Sends one NDJSON request line ({"actions": [...]}) and resolves with the parsed response object. */
  sendRequest(actions: unknown[], onError: 'abort' | 'continue' = 'abort'): Promise<any> {
    const run = async (): Promise<any> => {
      if (!this.alive) {
        throw new ActiondCrashedError(
          'actiond process for this session is no longer running; start a new session with pattern_new_session.'
        );
      }
      const id = randomUUID();
      const payload = JSON.stringify({ id, onError, actions }) + '\n';
      const responsePromise = new Promise<unknown>((resolve, reject) => {
        this.pending = { resolve, reject };
      });
      this.child.stdin.write(payload);
      let timer: NodeJS.Timeout;
      const timeout = new Promise<never>((_, reject) => {
        timer = setTimeout(
          () => reject(new ActiondTimeoutError(`actiond did not respond within ${config.requestTimeoutMs}ms`)),
          config.requestTimeoutMs
        );
      });
      try {
        return await Promise.race([responsePromise, timeout]);
      } finally {
        clearTimeout(timer!);
      }
    };
    // Chain onto the tail regardless of the previous outcome so one failed request doesn't wedge the queue.
    const result = this.queueTail.then(run, run);
    this.queueTail = result.then(
      () => undefined,
      () => undefined
    );
    return result;
  }

  /** Asks the daemon to close cleanly (session.close), falling back to SIGKILL after a grace period. */
  async close(graceMs = config.processExitGraceMs): Promise<void> {
    if (!this.alive) return;
    try {
      await this.sendRequest([{ op: 'session.close' }]);
    } catch {
      // fall through to hard kill below
    }
    await new Promise<void>((resolve) => {
      if (!this.alive) {
        resolve();
        return;
      }
      const timer = setTimeout(() => {
        if (this.alive) this.child.kill('SIGKILL');
        resolve();
      }, graceMs);
      this.child.once('exit', () => {
        clearTimeout(timer);
        resolve();
      });
    });
  }

  kill(): void {
    if (this.alive) this.child.kill('SIGKILL');
  }
}
