import { EventEmitter } from "events";
import WebSocket, { RawData } from "ws";
import type { ChiakiCommand, ChiakiResponse, ChiakiPush } from "./types.js";
import { ChiakiResponseSchema } from "./types.js";

/**
 * Connection state.
 */
export type ConnectionState = "disconnected" | "connecting" | "connected";

/**
 * Configuration options for ChiakiClient.
 */
export interface ChiakiClientOptions {
  /** Automatically reconnect on disconnect (default: true). */
  reconnect?: boolean;
  /** Initial reconnect delay in ms (default: 1000). */
  reconnectDelay?: number;
  /** Maximum reconnect delay in ms (default: 30000). */
  maxReconnectDelay?: number;
  /** Reconnect backoff multiplier (default: 2). */
  reconnectMultiplier?: number;
  /** Command timeout in ms (default: 10000). */
  timeout?: number;
  /** Max retries for failed commands (default: 0). */
  maxRetries?: number;
  /** Handshake timeout in ms (default: 5000). */
  handshakeTimeout?: number;
}

/**
 * Events emitted by ChiakiClient:
 *
 * - `connected`        — Handshake completed, ready to send commands.
 * - `disconnected`      — Connection closed (intentionally or unexpectedly).
 * - `push`              — Push notification from chiaki-ng (state_change, pin_request, quit).
 * - `error`             — Non-fatal error (e.g., send timeout).
 * - `reconnecting`      — About to attempt reconnect. Payload: { attempt: number, delay: number }.
 */
export declare interface ChiakiClient {
  on(event: "connected", listener: () => void): this;
  on(event: "disconnected", listener: () => void): this;
  on(event: "push", listener: (push: ChiakiPush) => void): this;
  on(event: "error", listener: (err: Error) => void): this;
  on(event: "reconnecting", listener: (info: { attempt: number; delay: number }) => void): this;
  emit(event: "connected"): boolean;
  emit(event: "disconnected"): boolean;
  emit(event: "push", push: ChiakiPush): boolean;
  emit(event: "error", err: Error): boolean;
  emit(event: "reconnecting", info: { attempt: number; delay: number }): boolean;
}

export class ChiakiClient extends EventEmitter {
  private ws: WebSocket | null = null;
  private url: string;
  private token?: string;
  private nextId = 1;
  private pending = new Map<number, PendingEntry>();
  private state: ConnectionState = "disconnected";

  // Options
  private reconnect: boolean;
  private reconnectDelay: number;
  private maxReconnectDelay: number;
  private reconnectMultiplier: number;
  private timeout: number;
  private maxRetries: number;
  private handshakeTimeout: number;

  // Reconnect state
  private reconnectAttempts = 0;
  private reconnectTimer: ReturnType<typeof setTimeout> | null = null;
  private shuttingDown = false;
  private connectResolve: ((() => void) | null) = null;
  private connectReject: ((err: Error) => void) | null = null;

  constructor(
    host: string,
    port: number,
    token?: string,
    opts: ChiakiClientOptions = {},
  ) {
    super();
    this.url = `ws://${host}:${port}`;
    this.token = token;

    this.reconnect = opts.reconnect ?? true;
    this.reconnectDelay = opts.reconnectDelay ?? 1000;
    this.maxReconnectDelay = opts.maxReconnectDelay ?? 30000;
    this.reconnectMultiplier = opts.reconnectMultiplier ?? 2;
    this.timeout = opts.timeout ?? 10000;
    this.maxRetries = opts.maxRetries ?? 0;
    this.handshakeTimeout = opts.handshakeTimeout ?? 5000;
  }

  // ── Public API ──

  /**
   * Connect to chiaki-ng McpServer. Resolves when handshake completes.
   * If reconnect is enabled and connection drops, automatically reconnects.
   */
  connect(): Promise<void> {
    if (this.state === "connected") {
      return Promise.resolve();
    }

    // If a connection attempt is already in progress, return its promise
    // We track this via state + connect promise
    return new Promise<void>((resolve, reject) => {
      // If already connecting, chain onto the ongoing attempt
      if (this.state === "connecting") {
        // Wait for the current attempt: we'll resolve/reject when it finishes
        const check = (): void => {
          if (this.state === "connected") {
            resolve();
          } else if (this.state === "disconnected") {
            reject(new Error("Connection failed"));
          } else {
            // Still connecting — poll after a short delay
            setTimeout(check, 50);
          }
        };
        check();
        return;
      }

      this.connectResolve = resolve;
      this.connectReject = reject;
      this.shuttingDown = false;
      this.doConnect();
    });
  }

  /**
   * Send a command to chiaki-ng and wait for the response.
   * Returns immediately with error if not connected.
   * Respects timeout and maxRetries options.
   */
  async send(
    cmd: string,
    params?: Record<string, unknown>,
  ): Promise<ChiakiResponse> {
    if (!this.ws || this.state !== "connected") {
      return {
        id: 0,
        error: "not_connected",
        message:
          "No hay conexión con chiaki-ng. Inicia chiaki-ng con MCP habilitado.",
      };
    }

    const id = this.nextId++;
    const command: ChiakiCommand = { id, cmd, params };
    let lastError: ChiakiResponse | null = null;

    // Retry loop
    for (let attempt = 0; attempt <= this.maxRetries; attempt++) {
      try {
        return await this.sendOne(id, command);
      } catch (err) {
        if (err instanceof PendingTimeout) {
          lastError = {
            id,
            error: "timeout",
            message: `Comando '${cmd}' excedió el timeout de ${this.timeout}ms`,
          };
        } else if (err instanceof Error) {
          lastError = { id, error: "send_error", message: err.message };
        }
        // On last attempt, return the error
        if (attempt === this.maxRetries) {
          return lastError ?? { id, error: "unknown", message: "Send failed" };
        }
        // Otherwise retry — use a fresh id
        // (only meaningful if the first send failed to reach the server)
      }
    }

    return lastError ?? { id, error: "unknown", message: "Send failed" };
  }

  /**
   * Disconnect from chiaki-ng. Disables auto-reconnect.
   */
  disconnect(): void {
    this.shuttingDown = true;
    this.clearReconnectTimer();
    if (this.ws) {
      this.ws.close(1000, "Client disconnect");
    }
  }

  /**
   * Whether the client is connected and ready.
   */
  isConnected(): boolean {
    return this.state === "connected";
  }

  /**
   * Current connection state.
   */
  getState(): ConnectionState {
    return this.state;
  }

  // ── Internal: connection lifecycle ──

  private doConnect(): void {
    this.state = "connecting";

    let handshakeResolved = false;
    let handshakeTimer: ReturnType<typeof setTimeout> | undefined;

    try {
      this.ws = new WebSocket(this.url);

      this.ws.on("open", () => {
        // Send handshake
        const handshake: Record<string, unknown> = {
          type: "handshake",
          version: 1,
        };
        if (this.token) handshake["token"] = this.token;
        this.ws!.send(JSON.stringify(handshake));

        // Start handshake timeout
        handshakeTimer = setTimeout(() => {
          if (!handshakeResolved) {
            handshakeResolved = true;
            const err = new Error(
              "Handshake timeout: no response from chiaki-ng",
            );
            this.onConnectError(err);
          }
        }, this.handshakeTimeout);
      });

      this.ws.on("message", (data: RawData) => {
        try {
          const msg = JSON.parse(data.toString());

          // Handshake response
          if (msg.type === "handshake_ok" && !handshakeResolved) {
            handshakeResolved = true;
            if (handshakeTimer) clearTimeout(handshakeTimer);
            this.onConnected();
            return;
          }

          // Handshake error (auth failed, version mismatch, etc.)
          if (msg.type === "handshake_error" && !handshakeResolved) {
            handshakeResolved = true;
            if (handshakeTimer) clearTimeout(handshakeTimer);
            const errMsg = msg.message ?? "Handshake rejected";
            this.onConnectError(new Error(errMsg));
            return;
          }

          // Push notification
          if (
            msg.type &&
            ["state_change", "pin_request", "quit"].includes(msg.type)
          ) {
            this.emit("push", msg as ChiakiPush);
            return;
          }

          // Command response
          const parsed = ChiakiResponseSchema.safeParse(msg);
          if (!parsed.success) {
            return;
          }
          const resp = parsed.data;
          const entry = this.pending.get(resp.id);
          if (entry) {
            this.pending.delete(resp.id);
            if (entry.timer) clearTimeout(entry.timer);
            entry.resolve(resp);
          }
        } catch {
          // Ignore parse errors on raw messages
        }
      });

      this.ws.on("close", (code: number, reason: Buffer) => {
        this.ws = null;

        // Reject pending promises
        for (const [id, entry] of this.pending) {
          if (entry.timer) clearTimeout(entry.timer);
          entry.resolve({
            id,
            error: "connection_closed",
            message: "Conexión cerrada",
          });
        }
        this.pending.clear();

        if (!handshakeResolved) {
          handshakeResolved = true;
          if (handshakeTimer) clearTimeout(handshakeTimer);
          const reasonStr = reason.toString();
          const err = new Error(
            `Connection closed before handshake: ${code} ${reasonStr}`,
          );
          this.onConnectError(err);
        } else {
          this.onDisconnected();
        }
      });

      this.ws.on("error", (err: Error) => {
        if (!handshakeResolved) {
          handshakeResolved = true;
          if (handshakeTimer) clearTimeout(handshakeTimer);
          this.onConnectError(err);
        } else {
          this.emit("error", err);
        }
      });
    } catch (err) {
      this.onConnectError(
        err instanceof Error ? err : new Error(String(err)),
      );
    }
  }

  private onConnected(): void {
    this.state = "connected";
    this.reconnectAttempts = 0;
    this.emit("connected");
    if (this.connectResolve) {
      this.connectResolve();
      this.connectResolve = null;
      this.connectReject = null;
    }
  }

  private onConnectError(err: Error): void {
    this.state = "disconnected";
    this.ws = null;
    if (this.connectReject) {
      this.connectReject(err);
      this.connectResolve = null;
      this.connectReject = null;
    }
    this.emit("error", err);
    this.scheduleReconnect();
  }

  private onDisconnected(): void {
    const wasConnected = this.state === "connected";
    this.state = "disconnected";
    this.ws = null;
    if (wasConnected) {
      this.emit("disconnected");
    }
    this.scheduleReconnect();
  }

  // ── Reconnect with exponential backoff + jitter ──

  private scheduleReconnect(): void {
    if (this.shuttingDown || !this.reconnect) {
      return;
    }

    this.reconnectAttempts++;
    const base = this.reconnectDelay *
      Math.pow(this.reconnectMultiplier, this.reconnectAttempts - 1);
    const jitter = Math.random() * 0.3 * base; // ±15% jitter
    const delay = Math.min(base + jitter, this.maxReconnectDelay);

    this.emit("reconnecting", {
      attempt: this.reconnectAttempts,
      delay: Math.round(delay),
    });

    this.reconnectTimer = setTimeout(() => {
      this.doConnect();
    }, delay);
  }

  private clearReconnectTimer(): void {
    if (this.reconnectTimer) {
      clearTimeout(this.reconnectTimer);
      this.reconnectTimer = null;
    }
  }

  // ── Internal: send with timeout ──

  private sendOne(
    id: number,
    command: ChiakiCommand,
  ): Promise<ChiakiResponse> {
    return new Promise<ChiakiResponse>((resolve, reject) => {
      const timer = setTimeout(() => {
        this.pending.delete(id);
        reject(new PendingTimeout(`Command ${command.cmd} timed out`));
      }, this.timeout);

      this.pending.set(id, { resolve, timer });
      this.ws!.send(JSON.stringify(command));
    });
  }
}

// ── Internal types ──

interface PendingEntry {
  resolve: (resp: ChiakiResponse) => void;
  timer: ReturnType<typeof setTimeout>;
}

class PendingTimeout extends Error {
  constructor(message: string) {
    super(message);
    this.name = "PendingTimeout";
  }
}
