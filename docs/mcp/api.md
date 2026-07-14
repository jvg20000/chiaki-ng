# API de herramientas MCP

15 herramientas disponibles vía `chiaki-mcp`. Todas usan el prefijo `ps_`.

## Resumen

| Tool | Estado requerido | Descripción |
|---|---|---|
| `ps_list` | cualquiera | Lista consolas registradas |
| `ps_pair` | idle | Inicia registro con una PS |
| `ps_pair_confirm` | idle | Confirma PIN de registro |
| `ps_connect` | idle | Conecta a una PS registrada |
| `ps_disconnect` | connected+ | Finaliza la sesión |
| `ps_status` | cualquiera | Estado actual de la sesión |
| `ps_press` | connected+ | Pulsa botones del mando |
| `ps_stick` | connected+ | Mueve joysticks analógicos |
| `ps_trigger` | connected+ | Controla gatillos L2/R2 |
| `ps_touchpad` | connected+ | Toca el panel táctil |
| `ps_home` | connected+ | Botón PlayStation |
| `ps_sleep` | connected+ | Modo reposo |
| `ps_keyboard` | connected+ | Teclado virtual |
| `ps_screenshot` | streaming | Captura JPEG del stream |
| `ps_events` | connected+ | Eventos pendientes (rumble, LED) |

---

## `ps_list`

Lista las consolas PlayStation registradas en chiaki-ng.

```
Parámetros: ninguno
Retorno:    [{name, console: "PS4"|"PS5", host, registered}]
```

Ejemplo:
```json
[
  {"name": "PS5-Salon", "console": "PS5", "host": "192.168.1.50", "registered": true},
  {"name": "PS4-Habitacion", "console": "PS4", "host": "192.168.1.51", "registered": false}
]
```

---

## `ps_pair`

Inicia el proceso de registro con una consola PlayStation. La consola mostrará un PIN de 8 dígitos.

```
Parámetros: {host: string}
Retorno:    {ok: true, message: "introduce PIN de 8 dígitos"}
Estado req: idle
```

Ejemplo:
```json
// Request
{"host": "192.168.1.50"}

// Response
{"ok": true, "message": "PIN solicitado. Usa ps_pair_confirm para introducirlo."}
```

---

## `ps_pair_confirm`

Confirma el PIN de 8 dígitos durante el registro.

```
Parámetros: {pin: string (8 dígitos)}
Retorno:    {ok: true} | {error: "regist_failed", message: "..."}
Estado req: idle
```

Ejemplo:
```json
// Request
{"pin": "12345678"}

// Response OK
{"ok": true, "message": "Registro completado. Consola guardada como 'PS5-12345678'"}

// Response Error
{"error": "regist_failed", "message": "PIN incorrecto o timeout"}
```

---

## `ps_connect`

Conecta a una PlayStation ya registrada usando la configuración guardada (regist_key, morning).

```
Parámetros: {name: string}
Retorno:    {ok: true, state: "connected"}
Estado req: idle
```

Ejemplo:
```json
// Request
{"name": "PS5-Salon"}

// Response
{"ok": true, "state": "connected"}

// Error: no registrada
{"error": "not_registered", "message": "PS5-Salon no está registrada. Usa ps_pair y ps_pair_confirm primero."}

// Error: ya conectado
{"error": "already_connected", "message": "Ya hay una sesión activa. Usa ps_disconnect() primero."}
```

---

## `ps_disconnect`

Finaliza la sesión actual. El estado vuelve a `idle`.

```
Parámetros: ninguno
Retorno:    {ok: true, state: "idle"}
Estado req: connected o streaming
```

---

## `ps_status`

Muestra el estado detallado de la sesión actual.

```
Parámetros: ninguno
Retorno:    {state, host, console, resolution?, fps?, codec?, decoder_fps?,
             bitrate_kbps?, packet_loss?, rtt_ms?, audio?, rumble?, mic_muted?}
```

Ejemplo:
```json
{
  "state": "streaming",
  "host": "PS5-Salon",
  "console": "PS5",
  "resolution": "1920x1080",
  "fps": 60,
  "codec": "H265",
  "decoder_fps": 60,
  "bitrate_kbps": 15000,
  "packet_loss": 0.01,
  "rtt_ms": 5,
  "audio": true,
  "rumble": false,
  "mic_muted": true
}
```

Durante `idle`, solo `state` está presente:
```json
{"state": "idle"}
```

---

## `ps_press`

Pulsa uno o varios botones del mando PlayStation simultáneamente.

```
Parámetros: {buttons: string[], duration_ms?: number (default: 100, max: 5000)}
Retorno:    {ok: true}
Estado req: connected o streaming
```

Botones válidos:
```
cross, circle, square, triangle,
l1, r1, l2, r2, l3, r3,
dpad_up, dpad_down, dpad_left, dpad_right,
options, share, ps, touchpad
```

Ejemplos:
```json
// Pulsar Cross
{"buttons": ["cross"]}

// Pulsar Cross + R1 simultáneamente durante 200ms
{"buttons": ["cross", "r1"], "duration_ms": 200}

// Mantener R2 pulsado 500ms
{"buttons": ["r2"], "duration_ms": 500}
```

---

## `ps_stick`

Mueve los joysticks analógicos.

```
Parámetros: {lx?: float (-1.0..1.0, default: 0),
             ly?: float (-1.0..1.0, default: 0),
             rx?: float (-1.0..1.0, default: 0),
             ry?: float (-1.0..1.0, default: 0)}
Retorno:    {ok: true}
Estado req: connected o streaming
```

Ejemplos:
```json
// Stick izquierdo todo arriba (avanzar)
{"lx": 0, "ly": -1.0}

// Stick izquierdo a la derecha (girar)
{"lx": 1.0, "ly": 0}

// Ambos sticks en diagonal
{"lx": 0.7, "ly": -0.7, "rx": 0.5, "ry": 0.5}

// Centrar todo
{}
```

---

## `ps_trigger`

Controla los gatillos L2 y R2.

```
Parámetros: {l2?: int (0-255, default: 0),
             r2?: int (0-255, default: 0)}
Retorno:    {ok: true}
Estado req: connected o streaming
```

Ejemplos:
```json
// Gatillo derecho a fondo
{"r2": 255}

// Ambos gatillos a media presión
{"l2": 128, "r2": 128}

// Soltar todo
{"l2": 0, "r2": 0}
```

---

## `ps_touchpad`

Toca el panel táctil del mando DualSense/DualShock.

```
Parámetros: {x: float (0..1920), y: float (0..1080)}
Retorno:    {ok: true}
Estado req: connected o streaming
```

Ejemplo:
```json
// Centro del touchpad
{"x": 960, "y": 540}

// Esquina superior izquierda
{"x": 0, "y": 0}
```

---

## `ps_home`

Pulsa el botón PlayStation (PS). Equivale a ir al menú principal.

```
Parámetros: ninguno
Retorno:    {ok: true}
Estado req: connected o streaming
```

---

## `ps_sleep`

Pone la consola en modo reposo. La sesión se cierra automáticamente.

```
Parámetros: ninguno
Retorno:    {ok: true, state: "idle"}
Estado req: connected o streaming
```

---

## `ps_keyboard`

Escribe texto usando el teclado virtual en la consola. Útil para campos de texto en juegos o aplicaciones.

```
Parámetros: {text: string}
Retorno:    {ok: true}
Estado req: connected o streaming
```

Ejemplo:
```json
{"text": "Buscar jugador..."}
```

---

## `ps_screenshot`

Captura una imagen del stream de vídeo actual como JPEG en base64.

```
Parámetros: ninguno
Retorno:    {ok: true, screenshot: "base64 JPEG"}
Estado req: streaming
```

**Importante**: El MCP TypeScript convierte automáticamente la respuesta a `ImageContent` nativo de MCP, para que el LLM pueda "ver" la imagen. La respuesta en crudo es:

```json
{
  "ok": true,
  "screenshot": "/9j/4AAQSkZJRgABAQEASABIAAD/2wBD..."
}
```

Y el MCP lo transforma a:
```json
{
  "content": [{
    "type": "image",
    "data": "/9j/4AAQSkZJRgABAQEASABIAAD/2wBD...",
    "mimeType": "image/jpeg"
  }]
}
```

---

## `ps_events`

Obtiene los eventos pendientes de la cola (máximo 100 eventos almacenados). La cola se vacía después de cada lectura.

```
Parámetros: ninguno
Retorno:    [{type, ...}]
Estado req: connected o streaming
```

Tipos de eventos:

| type | payload | Significado |
|---|---|---|
| `rumble` | `{left: 0-255, right: 0-255}` | Vibración de mandos |
| `led` | `{r, g, b}` | Cambio de color LED del mando |
| `quit` | `{reason: "stopped"|"connection_lost"|"..."}` | Sesión terminada |
| `login_pin` | `{message: "PS5 solicita PIN"}` | PIN requerido durante conexión |

Ejemplo:
```json
[
  {"type": "rumble", "left": 128, "right": 64},
  {"type": "led", "r": 0, "g": 255, "b": 0}
]
```

---

## Errores comunes

| Error | Significado |
|---|---|
| `no_session` | No hay sesión activa. Conecta primero con `ps_connect`. |
| `no_stream` | El stream de vídeo no está activo. Espera a que `state` sea `streaming`. |
| `already_connected` | Ya hay una sesión. Usa `ps_disconnect` primero. |
| `not_registered` | La consola no está registrada. Usa `ps_pair` y `ps_pair_confirm`. |
| `invalid_button` | Nombre de botón no válido. Verifica la lista de botones permitidos. |
| `not_connected` | chiaki-mcp no está conectado al WebSocket de chiaki-ng. Verifica que chiaki-ng esté ejecutándose con MCP enabled. |
