import { z } from "zod";

// ── Estados de la sesión ──
export const McpState = {
  Idle: "idle",
  Connected: "connected",
  Streaming: "streaming",
} as const;
export type McpState = (typeof McpState)[keyof typeof McpState];

// ── Tipos de consola ──
export const ConsoleType = {
  PS4: "PS4",
  PS5: "PS5",
} as const;
export type ConsoleType = (typeof ConsoleType)[keyof typeof ConsoleType];

// ── Handshake ──
export const HandshakeSchema = z.object({
  type: z.literal("handshake"),
  version: z.literal(1),
  token: z.string().optional(),
});
export type Handshake = z.infer<typeof HandshakeSchema>;

export const HandshakeOkSchema = z.object({
  type: z.literal("handshake_ok"),
  version: z.literal(1),
});
export type HandshakeOk = z.infer<typeof HandshakeOkSchema>;

// ── Mensaje WebSocket genérico (del chiaki-ng McpServer) ──
export const ChiakiResponseSchema = z.object({
  id: z.number(),
  ok: z.boolean().optional(),
  error: z.string().optional(),
  message: z.string().optional(),
  state: z.enum(["idle", "connected", "streaming"]).optional(),
  // Status payload
  host: z.string().optional(),
  console: z.enum(["PS4", "PS5"]).optional(),
  resolution: z.string().optional(),
  fps: z.number().optional(),
  codec: z.string().optional(),
  bitrate_kbps: z.number().optional(),
  packet_loss: z.number().optional(),
  rtt_ms: z.number().optional(),
  audio: z.boolean().optional(),
  rumble: z.boolean().optional(),
  mic_muted: z.boolean().optional(),
  decoder_fps: z.number().optional(),
  // screenshot
  screenshot: z.string().optional(),
});
export type ChiakiResponse = z.infer<typeof ChiakiResponseSchema>;

// ── Notificaciones push ──
export const StateChangeSchema = z.object({
  type: z.literal("state_change"),
  state: z.enum(["idle", "connected", "streaming"]),
});
export type StateChange = z.infer<typeof StateChangeSchema>;

export const PinRequestSchema = z.object({
  type: z.literal("pin_request"),
  message: z.string(),
});
export type PinRequest = z.infer<typeof PinRequestSchema>;

export const QuitSchema = z.object({
  type: z.literal("quit"),
  reason: z.string(),
});
export type Quit = z.infer<typeof QuitSchema>;

export const ChiakiPushSchema = z.discriminatedUnion("type", [
  StateChangeSchema,
  PinRequestSchema,
  QuitSchema,
]);
export type ChiakiPush = z.infer<typeof ChiakiPushSchema>;

// ── Comandos enviados al McpServer (JSON-RPC over WS) ──
export interface ChiakiCommand {
  id: number;
  cmd: string;
  params?: Record<string, unknown>;
}

// ── Botones válidos ──
export const VALID_BUTTONS = [
  "cross",
  "circle",
  "square",
  "triangle",
  "l1",
  "r1",
  "l2",
  "r2",
  "l3",
  "r3",
  "dpad_up",
  "dpad_down",
  "dpad_left",
  "dpad_right",
  "options",
  "share",
  "ps",
  "touchpad",
] as const;

export type Button = (typeof VALID_BUTTONS)[number];

// ── Schemas de parámetros por comando ──
export const PsListParams = z.object({}).strict();
export const PsPairParams = z.object({ host: z.string().min(1) }).strict();
export const PsPairConfirmParams = z.object({ pin: z.string().length(8) }).strict();
export const PsConnectParams = z.object({ name: z.string().min(1) }).strict();
export const PsPressParams = z.object({
  buttons: z.array(z.enum(VALID_BUTTONS)).min(1),
  duration_ms: z.number().int().min(0).max(5000).default(100).optional(),
}).strict();
export const PsStickParams = z.object({
  lx: z.number().min(-1).max(1).default(0),
  ly: z.number().min(-1).max(1).default(0),
  rx: z.number().min(-1).max(1).default(0),
  ry: z.number().min(-1).max(1).default(0),
}).strict();
export const PsTriggerParams = z.object({
  l2: z.number().int().min(0).max(255).default(0),
  r2: z.number().int().min(0).max(255).default(0),
}).strict();
export const PsTouchpadParams = z.object({
  x: z.number().min(0).max(1920),
  y: z.number().min(0).max(1080),
}).strict();
export const PsKeyboardParams = z.object({ text: z.string().min(1) }).strict();
export const PsScreenshotParams = z.object({}).strict();

// ── Info de host registrado ──
export interface RegisteredHost {
  name: string;
  console: ConsoleType;
  host: string;
  registered: boolean;
}
