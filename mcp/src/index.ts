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
import { CallToolRequestSchema } from "@modelcontextprotocol/sdk/types.js";
import type { CallToolResult } from "@modelcontextprotocol/sdk/types.js";

import { ChiakiClient } from "./client.js";
import { ALL_TOOLS, getToolByName, validateState } from "./tools.js";
import type { ToolDef } from "./tools.js";
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
    const ts = new Date().toISOString();
    console.error(`[${ts}][chiaki-mcp][${level.toUpperCase()}]`, ...args);
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

// ── Session state tracking ──
// Updated via push notifications from chiaki-ng McpServer's state_change events.
let currentState = "idle";

// ── Event wiring ──
client.on("connected", () => {
  log("info", "Conectado a chiaki-ng McpServer ✓");
});

client.on("disconnected", () => {
  currentState = "idle";
  log("warn", "Desconectado de chiaki-ng");
});

client.on("reconnecting", ({ attempt, delay }: { attempt: number; delay: number }) => {
  log(
    "info",
    `Reconectando a chiaki-ng (intento ${attempt}, en ${delay}ms)...`,
  );
});

client.on("push", (push: ChiakiPush) => {
  if (push.type === "state_change") {
    const prevState = currentState;
    currentState = push.state;
    log("debug", `Estado: ${prevState} → ${currentState}`);
  }
  log("debug", "Push notification:", push.type, push);
});

client.on("error", (err: Error) => {
  log("error", "Error:", err.message);
});

// ── Tool registration ──
// Phase 1: register tools with the high-level SDK so they appear in listTools.
// The provided callback is a no-op — our custom handler (Phase 2) intercepts
// CallToolRequest before the SDK's dispatch runs.
// We cast schema to `any` to bypass deep type instantiation in the SDK's
// Zod→JSONSchema conversion. Real validation happens in our handler below.
for (const toolDef of ALL_TOOLS) {
  const schema = toolDef.schema as z.ZodType<Record<string, unknown>>;
  server.registerTool(toolDef.name, {
    description: toolDef.description,
    inputSchema: schema,
  }, async (_args: Record<string, unknown>) => {
    // No-op: our custom setRequestHandler below intercepts all tool calls.
    return { content: [{ type: "text" as const, text: "internal dispatcher" }] };
  });
}
log("info", `Registradas ${ALL_TOOLS.length} tools MCP`);

// ── Custom CallToolRequest handler ──
// Phase 2: replace the SDK's internal CallToolRequest handler with our own
// that adds Zod validation (preserving .strict(), .default(), etc.),
// session state checking, screenshot handling, and unified error formatting.
//
// The SDK internally sets its handler on the first registerTool() call.
// We remove it and replace it with our own.
server.server.removeRequestHandler("tools/call");
server.server.setRequestHandler(CallToolRequestSchema, async (request): Promise<CallToolResult> => {
  const { name, arguments: rawArgs } = request.params;
  const toolDef: ToolDef | undefined = getToolByName(name);

  // ── Unknown tool ──
  if (!toolDef) {
    log("warn", `Tool desconocida: ${name}`);
    return {
      content: [{ type: "text", text: `Tool desconocida: ${name}` }],
      isError: true,
    };
  }

  log("debug", `→ ${name}`, rawArgs);

  // ── Zod param validation ──
  const parsed = toolDef.schema.safeParse(rawArgs ?? {});
  if (!parsed.success) {
    const issues = parsed.error.issues
      .map((i) => `  - ${i.path.join(".") || "(root)"}: ${i.message}`)
      .join("\n");
    log("warn", `← ${name} params inválidos:`, parsed.error.issues);
    return {
      content: [
        {
          type: "text",
          text: `Parámetros inválidos para '${name}':\n${issues}`,
        },
      ],
      isError: true,
    };
  }

  // ── State validation ──
  const stateError = validateState(toolDef.requiresState, currentState);
  if (stateError) {
    log("warn", `← ${name} state error:`, stateError);
    return {
      content: [{ type: "text", text: `Error: ${stateError}` }],
      isError: true,
    };
  }

  // ── Execute handler ──
  try {
    const response = await toolDef.handler(client, parsed.data);

    // Server-side error (not_connected, timeout, etc.)
    if (response.error) {
      log("warn", `← ${name} error:`, response.error, response.message);
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
    if (name === "ps_screenshot" && response.screenshot) {
      log(
        "debug",
        `← ${name} screenshot (${response.screenshot.length} chars base64)`,
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
    log("debug", `← ${name} ok`, response);
    return {
      content: [
        { type: "text", text: JSON.stringify(response, null, 2) },
      ],
    };
  } catch (err) {
    log("error", `← ${name} exception:`, err);
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
});

log("info", `Handler CallToolRequest personalizado instalado vía setRequestHandler`);

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
