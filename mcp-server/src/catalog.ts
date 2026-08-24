import { spawn } from 'node:child_process';
import { mkdtemp, rm } from 'node:fs/promises';
import { tmpdir } from 'node:os';
import path from 'node:path';
import { buildActiondInvocation } from './config.js';

export interface ToolCatalogEntry {
  name: string;
  description: string;
  category: string;
  input_schema: {
    type: 'object';
    properties?: Record<string, unknown>;
    required?: string[];
  };
  status?: string;
  statusReason?: string;
}

/**
 * Runs `actiond --list-tools --format=ai` once and returns the parsed catalogue. This is the
 * single source of truth for the per-action tool set — nothing here is hand-maintained, so the
 * MCP server automatically tracks the action layer's catalogue as it grows.
 */
export async function loadToolCatalog(): Promise<ToolCatalogEntry[]> {
  const probeDir = await mkdtemp(path.join(tmpdir(), 'actiond-catalog-'));
  try {
    const { command, args } = buildActiondInvocation(probeDir);
    const stdout = await runOneShot(command, [...args, '--list-tools', '--format=ai']);
    let parsed: unknown;
    try {
      parsed = JSON.parse(stdout);
    } catch (err) {
      throw new Error(
        `actiond --list-tools --format=ai did not print valid JSON (${(err as Error).message}). Raw output:\n${stdout}`
      );
    }
    if (!Array.isArray(parsed)) {
      throw new Error('actiond --list-tools --format=ai did not return a JSON array.');
    }
    return parsed as ToolCatalogEntry[];
  } finally {
    await rm(probeDir, { recursive: true, force: true }).catch(() => undefined);
  }
}

function runOneShot(command: string, args: string[]): Promise<string> {
  return new Promise((resolve, reject) => {
    const child = spawn(command, args, { stdio: ['ignore', 'pipe', 'pipe'] });
    let stdout = '';
    let stderr = '';
    child.stdout.on('data', (c: Buffer) => (stdout += c.toString()));
    child.stderr.on('data', (c: Buffer) => (stderr += c.toString()));
    child.on('error', (err) =>
      reject(new Error(`failed to run "${command}" ${args.join(' ')}: ${err.message}`))
    );
    child.on('exit', (code) => {
      if (code !== 0) {
        reject(new Error(`"${command} ${args.join(' ')}" exited with code ${code}: ${stderr}`));
      } else {
        resolve(stdout);
      }
    });
  });
}
