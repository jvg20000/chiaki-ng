import { z, type ZodObject } from "zod";
import type { ChiakiResponse } from "./types.js";
import {
  PsListParams,
  PsPairParams,
  PsPairConfirmParams,
  PsConnectParams,
  PsPressParams,
  PsStickParams,
  PsTriggerParams,
  PsTouchpadParams,
  PsKeyboardParams,
  PsScreenshotParams,
} from "./types.js";
import type { ChiakiClient } from "./client.js";

// ── Tool definition type ──
// We use ZodObject<any> for schemas because the exact shape varies per tool
// (e.g., ZodDefault, ZodOptional wrappers make precise typing verbose).
// Runtime validation is handled by Zod itself.
export interface ToolDef {
  name: string;
  description: string;
  schema: ZodObject<any>;
  requiresState?: "idle" | "connected" | "streaming";
  handler: (client: ChiakiClient, params: Record<string, unknown>) => Promise<ChiakiResponse>;
}

// ── Helper: send command with params ──
async function send(
  client: ChiakiClient,
  cmd: string,
  params?: Record<string, unknown>,
): Promise<ChiakiResponse> {
  return client.send(cmd, params);
}

// ── Tool definitions ──

const psList: ToolDef = {
  name: "ps_list",
  description:
    "Lista las consolas PlayStation registradas en chiaki-ng. Devuelve nombre, tipo (PS4/PS5), IP y si están registradas.",
  schema: PsListParams,
  handler: (client) => send(client, "list"),
};

const psPair: ToolDef = {
  name: "ps_pair",
  description:
    "Inicia el proceso de registro con una consola PlayStation en la IP especificada. Tras llamar a esta tool, la consola mostrará un PIN de 8 dígitos que deberás introducir con ps_pair_confirm.",
  schema: PsPairParams,
  requiresState: "idle",
  handler: (client, params) => send(client, "pair", { host: params["host"] }),
};

const psPairConfirm: ToolDef = {
  name: "ps_pair_confirm",
  description:
    "Confirma el PIN de 8 dígitos mostrado por la consola durante el registro iniciado con ps_pair.",
  schema: PsPairConfirmParams,
  requiresState: "idle",
  handler: (client, params) => send(client, "pair_confirm", { pin: params["pin"] }),
};

const psConnect: ToolDef = {
  name: "ps_connect",
  description:
    "Conecta a una PlayStation ya registrada usando la configuración guardada (regist_key, morning). Usa ps_list para ver las consolas disponibles.",
  schema: PsConnectParams,
  requiresState: "idle",
  handler: (client, params) => send(client, "connect", { name: params["name"] }),
};

const psDisconnect: ToolDef = {
  name: "ps_disconnect",
  description: "Finaliza la sesión actual con la consola PlayStation. El estado vuelve a idle.",
  schema: z.object({}).strict() as ZodObject<any>,
  handler: (client) => send(client, "disconnect"),
};

const psStatus: ToolDef = {
  name: "ps_status",
  description:
    "Muestra el estado actual: sesión (idle/connected/streaming), host, consola, resolución, FPS, códec, bitrate, pérdida de paquetes, RTT, audio, rumble y micrófono.",
  schema: z.object({}).strict() as ZodObject<any>,
  handler: (client) => send(client, "status"),
};

const psPress: ToolDef = {
  name: "ps_press",
  description:
    "Pulsa uno o varios botones del mando PlayStation simultáneamente. Botones válidos: cross, circle, square, triangle, l1, r1, l2, r2, l3, r3, dpad_up, dpad_down, dpad_left, dpad_right, options, share, ps, touchpad.",
  schema: PsPressParams,
  handler: (client, params) => send(client, "press", params),
};

const psStick: ToolDef = {
  name: "ps_stick",
  description:
    "Mueve los joysticks analógicos. Los valores van de -1.0 a 1.0. lx/ly = stick izquierdo, rx/ry = stick derecho.",
  schema: PsStickParams,
  handler: (client, params) => send(client, "stick", params),
};

const psTrigger: ToolDef = {
  name: "ps_trigger",
  description:
    "Controla los gatillos L2 y R2 del mando PlayStation. Valores de 0 (sin pulsar) a 255 (pulsado a fondo).",
  schema: PsTriggerParams,
  handler: (client, params) => send(client, "trigger", params),
};

const psTouchpad: ToolDef = {
  name: "ps_touchpad",
  description:
    "Toca el panel táctil del mando DualSense/DualShock en las coordenadas especificadas (x: 0-1920, y: 0-1080).",
  schema: PsTouchpadParams,
  handler: (client, params) => send(client, "touchpad", params),
};

const psHome: ToolDef = {
  name: "ps_home",
  description: "Pulsa el botón PlayStation (PS). Equivale a ir al menú principal de la consola.",
  schema: z.object({}).strict() as ZodObject<any>,
  handler: (client) => send(client, "home"),
};

const psSleep: ToolDef = {
  name: "ps_sleep",
  description: "Pone la consola PlayStation en modo reposo (sleep). La sesión se cierra.",
  schema: z.object({}).strict() as ZodObject<any>,
  handler: (client) => send(client, "sleep"),
};

const psKeyboard: ToolDef = {
  name: "ps_keyboard",
  description:
    "Escribe texto usando el teclado virtual en la consola. Útil para rellenar campos de texto en juegos o aplicaciones.",
  schema: PsKeyboardParams,
  handler: (client, params) => send(client, "keyboard", { text: params["text"] }),
};

const psScreenshot: ToolDef = {
  name: "ps_screenshot",
  description:
    "Captura una imagen del stream de vídeo actual. Devuelve un JPEG en base64. Requiere streaming activo.",
  schema: PsScreenshotParams,
  handler: (client) => send(client, "screenshot"),
};

const psEvents: ToolDef = {
  name: "ps_events",
  description:
    "Obtiene los eventos pendientes de la cola: rumble (vibración), LED (cambio de color), etc.",
  schema: z.object({}).strict() as ZodObject<any>,
  handler: (client) => send(client, "events"),
};

// ── Registry ──
export const ALL_TOOLS: ToolDef[] = [
  psList,
  psPair,
  psPairConfirm,
  psConnect,
  psDisconnect,
  psStatus,
  psPress,
  psStick,
  psTrigger,
  psTouchpad,
  psHome,
  psSleep,
  psKeyboard,
  psScreenshot,
  psEvents,
];

export function getToolByName(name: string): ToolDef | undefined {
  return ALL_TOOLS.find((t) => t.name === name);
}
