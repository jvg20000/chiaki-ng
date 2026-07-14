import { describe, it, expect } from "vitest";
import { z } from "zod";

// ── Schema validation tests (no chiaki-ng required) ──
import {
  ChiakiResponseSchema,
  ChiakiPushSchema,
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
  VALID_BUTTONS,
} from "../src/types.js";

describe("ChiakiResponseSchema", () => {
  it("parses a valid response", () => {
    const result = ChiakiResponseSchema.safeParse({
      id: 1,
      ok: true,
      state: "idle",
    });
    expect(result.success).toBe(true);
  });

  it("parses a status response with full fields", () => {
    const result = ChiakiResponseSchema.safeParse({
      id: 2,
      ok: true,
      state: "streaming",
      host: "PS5-123",
      console: "PS5",
      resolution: "1920x1080",
      fps: 60,
      codec: "h265",
      bitrate_kbps: 30000,
      packet_loss: 0.5,
      rtt_ms: 12,
      decoder_fps: 59.9,
      audio: true,
      rumble: true,
      mic_muted: false,
    });
    expect(result.success).toBe(true);
    if (result.success) {
      expect(result.data.fps).toBe(60);
      expect(result.data.codec).toBe("h265");
    }
  });

  it("parses an error response", () => {
    const result = ChiakiResponseSchema.safeParse({
      id: 3,
      error: "not_connected",
      message: "No hay conexión con chiaki-ng",
    });
    expect(result.success).toBe(true);
    if (result.success) {
      expect(result.data.error).toBe("not_connected");
    }
  });

  it("rejects missing id", () => {
    const result = ChiakiResponseSchema.safeParse({ ok: true });
    expect(result.success).toBe(false);
  });

  it("rejects invalid console type", () => {
    const result = ChiakiResponseSchema.safeParse({
      id: 4,
      console: "XBOX",
    });
    expect(result.success).toBe(false);
  });
});

describe("ChiakiPushSchema", () => {
  it("parses state_change push", () => {
    const result = ChiakiPushSchema.safeParse({
      type: "state_change",
      state: "streaming",
    });
    expect(result.success).toBe(true);
  });

  it("parses pin_request push", () => {
    const result = ChiakiPushSchema.safeParse({
      type: "pin_request",
      message: "Introduce el PIN de 8 dígitos",
    });
    expect(result.success).toBe(true);
  });

  it("parses quit push", () => {
    const result = ChiakiPushSchema.safeParse({
      type: "quit",
      reason: "Connection lost",
    });
    expect(result.success).toBe(true);
  });

  it("rejects unknown push type", () => {
    const result = ChiakiPushSchema.safeParse({
      type: "unknown_event",
    });
    expect(result.success).toBe(false);
  });
});

describe("Param schemas", () => {
  it("PsListParams accepts empty object", () => {
    expect(PsListParams.safeParse({}).success).toBe(true);
  });

  it("PsListParams rejects extra fields", () => {
    expect(PsListParams.safeParse({ host: "x" }).success).toBe(false);
  });

  it("PsPairParams requires host string", () => {
    expect(PsPairParams.safeParse({ host: "192.168.1.100" }).success).toBe(
      true,
    );
    expect(PsPairParams.safeParse({ host: "" }).success).toBe(false);
    expect(PsPairParams.safeParse({}).success).toBe(false);
  });

  it("PsPairConfirmParams requires 8-digit pin", () => {
    expect(PsPairConfirmParams.safeParse({ pin: "12345678" }).success).toBe(
      true,
    );
    expect(PsPairConfirmParams.safeParse({ pin: "1234" }).success).toBe(false);
    expect(PsPairConfirmParams.safeParse({ pin: "ABCDEFGH" }).success).toBe(
      true,
    );
  });

  it("PsConnectParams requires name string", () => {
    expect(PsConnectParams.safeParse({ name: "My PS5" }).success).toBe(true);
    expect(PsConnectParams.safeParse({ name: "" }).success).toBe(false);
  });

  it("PsPressParams validates buttons and duration", () => {
    expect(
      PsPressParams.safeParse({ buttons: ["cross", "r1"] }).success,
    ).toBe(true);
    expect(
      PsPressParams.safeParse({
        buttons: ["cross"],
        duration_ms: 500,
      }).success,
    ).toBe(true);
    expect(PsPressParams.safeParse({ buttons: [] }).success).toBe(false);
    expect(
      PsPressParams.safeParse({ buttons: ["invalid_button"] }).success,
    ).toBe(false);
    expect(
      PsPressParams.safeParse({
        buttons: ["cross"],
        duration_ms: -1,
      }).success,
    ).toBe(false);
    expect(
      PsPressParams.safeParse({
        buttons: ["cross"],
        duration_ms: 6000,
      }).success,
    ).toBe(false);
  });

  it("PsStickParams validates stick ranges", () => {
    expect(
      PsStickParams.safeParse({ lx: 0.5, ly: -0.3, rx: 0, ry: 0 }).success,
    ).toBe(true);
    expect(
      PsStickParams.safeParse({ lx: 1.5, ly: 0, rx: 0, ry: 0 }).success,
    ).toBe(false);
    expect(PsStickParams.safeParse({}).success).toBe(true); // all optional with defaults
  });

  it("PsTriggerParams validates 0-255 range", () => {
    expect(PsTriggerParams.safeParse({ l2: 128, r2: 255 }).success).toBe(true);
    expect(PsTriggerParams.safeParse({ l2: 256, r2: 0 }).success).toBe(false);
    expect(PsTriggerParams.safeParse({}).success).toBe(true);
  });

  it("PsTouchpadParams validates coordinate ranges", () => {
    expect(
      PsTouchpadParams.safeParse({ x: 960, y: 540 }).success,
    ).toBe(true);
    expect(
      PsTouchpadParams.safeParse({ x: 2000, y: 500 }).success,
    ).toBe(false);
    expect(
      PsTouchpadParams.safeParse({ x: 500, y: 1100 }).success,
    ).toBe(false);
  });

  it("PsKeyboardParams validates text", () => {
    expect(
      PsKeyboardParams.safeParse({ text: "hello world" }).success,
    ).toBe(true);
    expect(PsKeyboardParams.safeParse({ text: "" }).success).toBe(false);
  });

  it("PsScreenshotParams accepts empty object", () => {
    expect(PsScreenshotParams.safeParse({}).success).toBe(true);
  });
});

describe("VALID_BUTTONS", () => {
  it("contains all expected PlayStation buttons", () => {
    expect(VALID_BUTTONS).toContain("cross");
    expect(VALID_BUTTONS).toContain("circle");
    expect(VALID_BUTTONS).toContain("square");
    expect(VALID_BUTTONS).toContain("triangle");
    expect(VALID_BUTTONS).toContain("l1");
    expect(VALID_BUTTONS).toContain("r1");
    expect(VALID_BUTTONS).toContain("l2");
    expect(VALID_BUTTONS).toContain("r2");
    expect(VALID_BUTTONS).toContain("l3");
    expect(VALID_BUTTONS).toContain("r3");
    expect(VALID_BUTTONS).toContain("dpad_up");
    expect(VALID_BUTTONS).toContain("dpad_down");
    expect(VALID_BUTTONS).toContain("dpad_left");
    expect(VALID_BUTTONS).toContain("dpad_right");
    expect(VALID_BUTTONS).toContain("options");
    expect(VALID_BUTTONS).toContain("share");
    expect(VALID_BUTTONS).toContain("ps");
    expect(VALID_BUTTONS).toContain("touchpad");
    expect(VALID_BUTTONS).toHaveLength(18);
  });
});
