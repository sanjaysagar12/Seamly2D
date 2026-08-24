#!/usr/bin/env node
import { StdioServerTransport } from '@modelcontextprotocol/sdk/server/stdio.js';
import { loadToolCatalog } from './catalog.js';
import { buildServer } from './server.js';

async function main(): Promise<void> {
  const catalog = await loadToolCatalog();
  process.stderr.write(`[seamly2d-mcp] loaded ${catalog.length} actiond tools\n`);

  const server = buildServer(catalog);
  const transport = new StdioServerTransport();
  await server.connect(transport);
  process.stderr.write('[seamly2d-mcp] connected on stdio\n');
}

main().catch((err) => {
  const message = err instanceof Error ? (err.stack ?? err.message) : String(err);
  process.stderr.write(`[seamly2d-mcp] fatal startup error: ${message}\n`);
  process.exit(1);
});
