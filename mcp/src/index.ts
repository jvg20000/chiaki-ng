#!/usr/bin/env node

/**
 * chiaki-mcp: MCP server for chiaki-ng PS Remote Play.
 *
 * Connects to chiaki-ng's built-in WebSocket McpServer and exposes
 * PlayStation control tools to AI models via the Model Context Protocol.
 *
 * Usage:
 *   npx tsx src/index.ts                    # Development (tsx)
 *   npm run build && npm start              # Production
 *   node dist/index.js                      # Direct
 *
 * Configuration via environment variables:
 *   CHIAKI_HOST       — chiaki-ng host (default: 127.0.0.1)
 *   CHIAKI_PORT       — MCP WebSocket port (default: 9090)
 *   CHIAKI_TOKEN      — auth token (required when chiaki-ng has expose=true)
 *   CHIAKI_TIMEOUT    — command timeout in ms (default: 10000)
 *   CHIAKI_MAX_RETRIES — max retries per command (default: 0)
 *   LOG_LEVEL         — debug, info, warn, error (default: info)
 */

import { McpServer } from "@modelcontextprotocol/sdk/server/mcp.js";
import { StdioServerTransport } from "@modelcontextprotocol/sdk/server/stdio.js";
import type { CallToolResult } from "@modelcontextprotocol/sdk/types.js";

import { ChiakiClient } from "./client.js";
import { ALL_TOOLS, getToolByName } from "./tools.js";
import type { ChiakiPush } from "./types.js";

// ── Configuration ──
const CHIAKI_HOST = process.env["CHIAKI_HOST"] ?? "127.0.0.1";
const CHIAKI_PORT = parseInt(process.env["CHIAKI_PORT"] ?? "9090", 10);
const CHIAKI_TOKEN = process.env["CHIAKI_TOKEN"] || undefined;
const CHIAKI_TIMEOUT = parseInt(process.env["CHIAKI_TIMEOUT"] ?? "10000", 10);
const CHIAKI_MAX_RETRIES = parseInt(
  process.env["CHIAKI_MAX_RETRIES"] ?? "0",
  10,
);
const LOG_LEVEL = process.env["LOG_LEVEL"] ?? "info";

const LOG_LEVELS: Record<string, number> = {
  debug: 0,
  info: 1,
  warn: 2,
  error: 3,
};
const currentLogLevel = LOG_LEVELS[LOG_LEVEL] ?? 1;

function log(level: string, ...args: unknown[]): void {
  const lvl = LOG_LEVELS[level] ?? 1;
  if (lvl >= currentLogLevel) {
    console.error(`[chiaki-mcp][${level.toUpperCase()}]`, ...args);
  }
}

// ── MCP Server ──
const server = new McpServer({
  name: "chiaki-mcp",
  version: "1.0.0",
  description:
    "Controla una PlayStation (PS4/PS5) a través de chiaki-ng Remote Play desde un modelo de IA.",
});

// ── Chiaki WebSocket client ──
const client = new ChiakiClient(CHIAKI_HOST, CHIAKI_PORT, CHIAKI_TOKEN, {
  timeout: CHIAKI_TIMEOUT,
  maxRetries: CHIAKI_MAX_RETRIES,
  reconnectDelay: 1000,
  maxReconnectDelay: 30000,
  handshakeTimeout: 5000,
});

// ── Event wiring ──
client.on("connected", () => {
  log("info", "Conectado a chiaki-ng McpServer ✓");
});

client.on("disconnected", () => {
  log("warn", "Desconectado de chiaki-ng");
});

client.on("reconnecting", ({ attempt, delay }: { attempt: number; delay: number }) => {
  log(
    "info",
    `Reconectando a chiaki-ng (intento ${attempt}, en ${delay}ms)...`,
  );
});

client.on("push", (push: ChiakiPush) => {
  log("debug", "Push notification:", push.type, push);
});

client.on("error", (err: Error) => {
  log("error", "Error:", err.message);
});

// ── Register tools ──
for (const toolDef of ALL_TOOLS) {
  server.tool(
    toolDef.name,
    toolDef.description,
    toolDef.schema.shape,
    async (params: Record<string, unknown>): Promise<CallToolResult> => {
      log("debug", `→ ${toolDef.name}`, params);

      try {
        const response = await toolDef.handler(client, params);

        if (response.error) {
          log("warn", `← ${toolDef.name} error:`, response.error);
          return {
            content: [
              {
                type: "text",
                text: `Error: ${response.error}${response.message ? ` — ${response.message}` : ""}`,
              },
            ],
            isError: true,
          };
        }

        // Handle screenshot specially — return as image content
        if (toolDef.name === "ps_screenshot" && response.screenshot) {
          log(
            "debug",
            `← ${toolDef.name} screenshot (${response.screenshot.length} chars base64)`,
          );
          return {
            content: [
              {
                type: "image",
                data: response.screenshot,
                mimeType: "image/jpeg",
              },
              {
                type: "text",
                text: `Screenshot capturado. Resolución: ${response.resolution ?? "desconocida"}`,
              },
            ],
          };
        }

        // Default text response
        log("debug", `← ${toolDef.name} ok`, response);
        return {
          content: [
            { type: "text", text: JSON.stringify(response, null, 2) },
          ],
        };
      } catch (err) {
        log("error", `← ${toolDef.name} exception:`, err);
        return {
          content: [
            {
              type: "text",
              text: `Error interno: ${err instanceof Error ? err.message : String(err)}`,
            },
          ],
          isError: true,
        };
      }
    },
  );

  log("info", `Tool registrada: ${toolDef.name}`);
}

// ── Start ──
async function main(): Promise<void> {
  log("info", `Conectando a chiaki-ng en ${CHIAKI_HOST}:${CHIAKI_PORT}...`);

  try {
    await client.connect();
    log("info", "Conectado a chiaki-ng McpServer ✓");
  } catch (err) {
    log(
      "warn",
      "No se pudo conectar a chiaki-ng. Las tools funcionarán pero reportarán 'not_connected' hasta que se restablezca la conexión.",
    );
    log("debug", "Error de conexión:", err);
  }

  const transport = new StdioServerTransport();
  await server.connect(transport);
  log("info", "chiaki-mcp listo (stdio)");
}

main().catch((err) => {
  log("error", "Fatal:", err);
  process.exit(1);
});

// Handle graceful shutdown
process.on("SIGINT", () => {
  log("info", "Apagando...");
  client.disconnect();
  process.exit(0);
});

process.on("SIGTERM", () => {
  log("info", "Apagando...");
  client.disconnect();
  process.exit(0);
});
