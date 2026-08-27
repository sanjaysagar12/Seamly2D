import path from 'node:path';
import { stat } from 'node:fs/promises';
import { config } from './config.js';

/**
 * Resolves a pattern/measurements file argument to an absolute path and validates it before
 * it's ever handed to actiond. A bare filename is resolved against `config.uploadsDir` (the
 * local folder the person is expected to drop files into); an absolute path is used as-is.
 *
 * This server is a local, single-user stdio child process of Claude Desktop — whoever can talk
 * to it is, by construction, the same person running Desktop on this machine — so resolving an
 * absolute path the model provides is a deliberate design choice here, not an oversight.
 */
export async function resolveUploadPath(kindLabel: string, given: string, allowedExtensions: string[]): Promise<string> {
  const isAbsolute = path.isAbsolute(given);
  const resolved = isAbsolute ? given : path.join(config.uploadsDir, given);
  const ext = path.extname(resolved).toLowerCase();
  if (!allowedExtensions.includes(ext)) {
    throw new Error(`${kindLabel} "${given}" has extension "${ext}", expected one of: ${allowedExtensions.join(', ')}.`);
  }
  try {
    await stat(resolved);
  } catch {
    throw new Error(
      isAbsolute
        ? `${kindLabel} "${given}" was not found at that path. Check the path and try again.`
        : `${kindLabel} "${given}" was not found at ${resolved}. Place the file in the uploads folder ` +
          `(${config.uploadsDir}) and try again, or pass a full absolute path instead.`
    );
  }
  return resolved;
}
