# Arquitectura MCP — chiaki-ng

## Arquitectura final (julio 2026)

La arquitectura definitiva es **WebSocket server embebido en chiaki-ng + MCP TypeScript remoto**.
Corresponde al **Patrón C** del análisis original, refinado para minimizar dependencias en la máquina gaming.

```
┌─────────────────────────────────────────────────────────────────────────┐
│  chiaki-ng (C++ / Qt)                                                   │
│                                                                         │
│  ┌───────────────┐     ┌─────────────────────────────────────────────┐  │
│  │ StreamSession │     │  McpServer (ws://host:9090)                  │  │
│  │               │     │                                             │  │
│  │  ChiakiSession│◄───►│  ┌──────────┐  ┌──────────────┐             │  │
│  │  FFmpegDecoder│     │  │ Comandos  │  │  Notificaciones│          │  │
│  │  Controller   │     │  │ (JSON)   │  │  (push)       │           │  │
│  │  Events       │     │  └──────────┘  └──────────────┘             │  │
│  └───────────────┘     └──────────────────┬──────────────────────────┘  │
│                                           │                             │
│  ┌────────────────┐                       │ WebSocket                   │
│  │ Settings       │──── enable/port ──────┘                             │
│  │ (QSettings)    │                                                      │
│  └────────────────┘                                                      │
└─────────────────────────────────────────────────────────────────────────┘
                                           │
                                           │ ws://host:9090
                                           │ JSON-RPC
                                           ▼
┌─────────────────────────────────────────────────────────────────────────┐
│  chiaki-mcp (TypeScript / Node.js)                                       │
│                                                                         │
│  ┌──────────────┐    ┌──────────────┐    ┌────────────────────────────┐ │
│  │ ChiakiClient │◄──►│  tools.ts    │    │  MCP Server (stdio)        │ │
│  │ (WebSocket)  │    │  15 tools    │◄──►│  → Hermes / Claude / etc.  │ │
│  └──────────────┘    └──────────────┘    └────────────────────────────┘ │
└─────────────────────────────────────────────────────────────────────────┘
```

## Componentes

### 1. McpServer (C++ / Qt)

**Archivo**: `gui/include/mcpserver.h`, `gui/src/mcpserver.cpp`
**Líneas**: ~158 (header) + ~1475 (implementación)

Clase que vive dentro de `StreamSession`. Responsabilidades:

- **WebSocket server** (`QWebSocketServer`) en el puerto configurado
- **Autenticación**: sin token en localhost (`127.0.0.1`), token obligatorio al exponer a red (`0.0.0.0`)
- **State machine**: `Idle → Connected → Streaming`
- **Comandos JSON-RPC**: `list`, `pair`, `pair_confirm`, `connect`, `disconnect`, `login_pin`, `status`, `screenshot`, `press`, `stick`, `trigger`, `touchpad`, `home`, `sleep`, `keyboard`, `events`
- **Notificaciones push**: `state_change`, `pin_request`, `quit`
- **Screenshots**: captura frames del decoder FFmpeg → JPEG → base64 (binario WebSocket + metadatos JSON)

### 2. chiaki-mcp (TypeScript / Node.js)

**Directorio**: `mcp/`
**Stack**: `@modelcontextprotocol/sdk`, `ws`, `zod`, `vitest`, `typescript`

Servidor MCP que:
- Se conecta al WebSocket de chiaki-ng via `ChiakiClient`
- Expone 15 herramientas MCP con prefijo `ps_` via stdio
- Valida parámetros con Zod
- Verifica precondiciones de estado antes de cada comando
- Auto-reconexión con backoff exponencial
- Maneja frames binarios (JPEG screenshots) y notificaciones push

### 3. Settings MCP

Integrado en `gui/include/settings.h`, `gui/src/settings.cpp`:

| QSettings key | Default | Tipo | UI |
|---|---|---|---|
| `settings/mcp_enabled` | `false` | bool | Switch |
| `settings/mcp_port` | `9090` | int (1024-65535) | SpinBox |
| `settings/mcp_expose` | `false` | bool | Switch |
| `settings/mcp_token` | `""` | string | TextField + botón "New" |

Señales Qt: `mcpEnabledChanged()`, `mcpPortChanged()`, `mcpExposeChanged()`, `mcpTokenChanged()`

### 4. StreamSession Integration

En `gui/src/streamsession.cpp`:

```cpp
// Constructor: crea McpServer si GetMcpEnabled()
if(connect_info.settings->GetMcpEnabled())
{
    mcp_server = new McpServer(connect_info.settings, this);
    mcp_server->SetChiakiSession(&session);
    mcp_server->StartServer(port, expose, token);
}

// Hot reload cuando cambian settings
connect(connect_info.settings, &Settings::mcpEnabledChanged, ...);
// → HandleMcpSettingsChanged() reinicia el servidor si cambia enable/port/expose/token

// Estado sincronizado con StreamSession
mcp_server->SetState(McpState::Connected);   // al conectar
mcp_server->SetState(McpState::Idle);         // al desconectar
mcp_server->SetState(McpState::Streaming);    // al iniciar stream
mcp_server->SetFfmpegDecoder(ffmpeg_decoder); // para screenshots
```

## Protocolo WebSocket

### Handshake

```
Cliente → Servidor:  {"type": "handshake", "version": 1, "token": "abc123..."}
Servidor → Cliente:  {"type": "handshake_ok", "version": 1}
```

El token solo se requiere cuando `expose=true` (bind `0.0.0.0`). En localhost el token es opcional.

### Comandos (JSON-RPC)

```
→ {"id": 1, "cmd": "press", "params": {"buttons": ["cross"], "duration_ms": 100}}
← {"id": 1, "ok": true}

→ {"id": 2, "cmd": "status"}
← {"id": 2, "ok": true, "state": "streaming", "host": "PS5-123", "console": "PS5", ...}

→ {"id": 3, "cmd": "screenshot"}
← [frame binario JPEG]
← {"id": 3, "ok": true, "screenshot": "base64..."}
```

### Notificaciones push

```
← {"type": "state_change", "state": "streaming"}
← {"type": "pin_request", "message": "PS5 solicita PIN"}
← {"type": "quit", "reason": "stopped"}
```

## State machine

```
IDLE ──connect──► CONNECTED ──stream starts──► STREAMING
  ▲                    │                          │
  └──disconnect────────┘                          │
  ▲                                               │
  └───────────── disconnect ──────────────────────┘
```

Cada comando verifica el estado antes de ejecutar:

| Estado requerido | Comandos |
|---|---|
| `idle` | `pair`, `pair_confirm`, `connect` |
| `connected` o `streaming` | `disconnect`, `press`, `stick`, `trigger`, `touchpad`, `home`, `sleep`, `keyboard`, `events` |
| `streaming` | `screenshot` |
| cualquiera | `list`, `status` |

## Decisiones de diseño

| Decisión | Razón |
|---|---|
| **prefijo `ps_`** (no `ps5_`) | Compatible con PS4 y PS5 |
| **WebSocket, no TCP crudo** | Soporte nativo en Qt y Node; full-duplex; binario para screenshots |
| **MCP en TypeScript, no Python** | Mejor DX con MCP SDK oficial; ecosistema Hermes (Node) |
| **Autenticación condicional** | Seguridad sin fricción: sin token en localhost, token obligatorio al exponer |
| **Token autogenerado** | Evita tokens débiles; botón "New" en la UI para regenerar |
| **Misma sesión que la GUI** | El MCP comparte ChiakiSession con la GUI; no hay sesiones paralelas |
| **Estado sincronizado** | StreamSession notifica cambios de estado al McpServer automáticamente |

## Fuera de scope

- ❌ PSN (token, account-id, duid) — si el host lo tiene configurado se usa, pero el MCP no lo expone ni modifica
- ❌ Configuración de vídeo (resolución, bitrate, codec) — se usa la del host
- ❌ Wakeup / `auto_regist` — no se expone via MCP
- ❌ Sesiones independientes de la GUI — el MCP comparte la sesión activa
