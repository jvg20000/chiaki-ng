# Flujo de conexión MCP

## State machine completa

```
                         ps_pair(host)
                            │
  ┌─────────────────┐       ▼
  │                 │  ┌──────────┐    ps_pair_confirm(pin)
  │      IDLE       │  │ PAIRING  │──────────────────────► vuelve a IDLE
  │                 │  └──────────┘   (éxito o fallo)
  └────────┬────────┘
           │ ps_connect(name)
           │ (usa regist_key + morning del host)
           ▼
  ┌─────────────────┐
  │                 │     stream inicia automáticamente
  │   CONNECTED     │──────────────────────────────────►
  │                 │
  └────────┬────────┘
           │                          ┌─────────────────┐
           │                          │                 │
           └─────────────────────────►│   STREAMING     │
                                      │                 │
                                      └────────┬────────┘
                                               │
                    ps_disconnect()            │
                    ps_sleep()                 │
                    quit (evento)              │
                                               ▼
                                        ┌─────────────────┐
                                        │                 │
                                        │      IDLE       │
                                        │                 │
                                        └─────────────────┘
```

## Secuencia de handshake WebSocket

```
CLIENTE (chiaki-mcp)                     SERVIDOR (chiaki-ng McpServer)
       │                                          │
       │  TCP connect                             │
       │─────────────────────────────────────────►│
       │                                          │
       │  {"type":"handshake","version":1}         │
       │  (+ "token" si expose=true)              │
       │─────────────────────────────────────────►│
       │                                          │── verifica versión
       │                                          │── verifica token (si expose)
       │          {"type":"handshake_ok",          │
       │           "version":1}                    │
       │◄─────────────────────────────────────────│
       │                                          │
       │  ──── listo para comandos ────           │
```

## Ciclo de vida de una sesión

### 1. Discovery

```
LLM → ps_list()
MCP → {"id":1, "cmd":"list"}  ────WS────►  McpServer
                                        lee QSettings::GetRegisteredHosts()
MCP ◄──── {"id":1, "ok":true, "hosts":[...]}  ◄─
LLM ← [{name, console, host, registered}, ...]
```

### 2. Pairing (si la consola no está registrada)

```
LLM → ps_pair("192.168.1.50")
MCP → {"id":2, "cmd":"pair", "params":{"host":"192.168.1.50"}}  ──►  McpServer
                                                                    └─→ chiaki_regist_start()
                                                                    └─→ PS5 muestra PIN

      ◄── {"id":2, "ok":true, "message":"Introduce PIN de 8 dígitos"}
LLM ← "PIN solicitado"

LLM → ps_pair_confirm("12345678")
MCP → {"id":3, "cmd":"pair_confirm", "params":{"pin":"12345678"}}  ──►  McpServer
                                                                      └─→ chiaki_regist_finish()
                                                                      └─→ guarda regist_key en QSettings

      ◄── {"id":3, "ok":true}
LLM ← "Registro completado: PS5-Salon"
```

### 3. Conexión

```
LLM → ps_connect("PS5-Salon")
MCP → {"id":4, "cmd":"connect", "params":{"name":"PS5-Salon"}}  ──►  McpServer
                                                                   └─→ busca host en QSettings
                                                                   └─→ chiaki_session_init()
                                                                   └─→ chiaki_session_start()
                                                                   └─→ estado → Connected
                                                                   └─→ StreamSession inicia codecs

      ◄── {"id":4, "ok":true, "state":"connected"}
MCP ◄── {"type":"state_change", "state":"connected"}  (push)
LLM ← "Conectado a PS5-Salon"

... stream de vídeo/audio comienza ...

MCP ◄── {"type":"state_change", "state":"streaming"}  (push)
```

### 4. Control

```
LLM → ps_press(["cross"], 100)
MCP → {"id":5, "cmd":"press", "params":{"buttons":["cross"],"duration_ms":100}}  ──►
                                                                   └─→ chiaki_session_set_controller_state()
                                                                   └─→ sleep(100ms)
                                                                   └─→ suelta botones

      ◄── {"id":5, "ok":true}
LLM ← "Botón cross pulsado"
```

### 5. Screenshot

```
LLM → ps_screenshot()
MCP → {"id":6, "cmd":"screenshot"}  ──►  McpServer
                                       └─→ captura frame actual del FFmpegDecoder
                                       └─→ convierte AVFrame → JPEG (libswscale)
                                       └─→ codifica JPEG → base64

      ◄── [frame binario WebSocket: JPEG raw]
      ◄── {"id":6, "ok":true, "screenshot":"/9j/4AAQ..."}

MCP: extrae base64, construye ImageContent nativo MCP
LLM ← [ve la imagen JPEG]
```

### 6. Desconexión

```
LLM → ps_sleep()
MCP → {"id":7, "cmd":"sleep"}  ──►  McpServer
                                  └─→ chiaki_session_goto_bed()
                                  └─→ estado → Idle

      ◄── {"id":7, "ok":true, "state":"idle"}
MCP ◄── {"type":"quit", "reason":"stopped"}  (push)
MCP ◄── {"type":"state_change", "state":"idle"}  (push)
LLM ← "Consola en reposo"
```

## Verificación de estado en cada comando

Cada comando en `McpServer::HandleCommand()` verifica el estado antes de ejecutar:

```cpp
// En CmdConnect():
if(state != McpState::Idle)
    return ErrorResponse("already_connected");

// En CmdPress():
if(!StateAllowsControl())
    return ErrorResponse("no_session");

// En CmdScreenshot():
if(!StateAllowsStream())
    return ErrorResponse("no_stream");
```

El MCP TypeScript también verifica precondiciones en `validateState()` antes de enviar el comando, proporcionando mensajes de error en español.

## Reconexión automática

El `ChiakiClient` TypeScript implementa reconexión con backoff exponencial:

```
desconexión detectada
  → intento 1: esperar 1s
  → intento 2: esperar 2s
  → intento 3: esperar 4s
  → intento 4: esperar 8s
  → ...
  → máximo: 30s entre intentos
  → handshake nuevamente al reconectar
```

Estado durante la reconexión:
- Las tools MCP responden con `"not_connected"` hasta que se restablece la conexión
- Las notificaciones push se pierden durante la desconexión
- El `currentState` se resetea a `"idle"` al desconectar

## Timeouts

| Timeout | Default | Descripción |
|---|---|---|
| Comando WebSocket | 10s (`CHIAKI_TIMEOUT`) | Si chiaki-ng no responde en este tiempo, el comando falla |
| Handshake | 5s | Timeout para completar el handshake inicial |
| Reconexión | 30s máximo | Backoff máximo entre intentos de reconexión |
