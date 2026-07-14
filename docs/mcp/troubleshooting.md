# Troubleshooting MCP

Guía de diagnóstico y resolución de problemas comunes.

---

## Índice

1. [chiaki-mcp no arranca](#chiaki-mcp-no-arranca)
2. [Error "not_connected" en todas las tools](#error-not_connected-en-todas-las-tools)
3. [Error "no_session"](#error-no_session)
4. [Error "no_stream" en ps_screenshot](#error-no_stream-en-ps_screenshot)
5. [Error "not_registered" en ps_connect](#error-not_registered-en-ps_connect)
6. [Screenshots en negro o corruptos](#screenshots-en-negro-o-corruptos)
7. [Desconexiones frecuentes](#desconexiones-frecuentes)
8. [Problemas de autenticación (token)](#problemas-de-autenticacion-token)
9. [MCP tab no aparece en Settings](#mcp-tab-no-aparece-en-settings)
10. [Cambios de settings no tienen efecto](#cambios-de-settings-no-tienen-efecto)
11. [Diagnóstico con logs](#diagnostico-con-logs)

---

## chiaki-mcp no arranca

**Síntomas**: `node dist/index.js` falla con error.

**Causas y soluciones**:

1. **Falta `npm install`**:
   ```bash
   cd mcp/ && npm ci
   ```

2. **Falta `npm run build`**:
   ```bash
   npm run build
   ```
   Verifica que `dist/` contiene `index.js`, `client.js`, `tools.js`, `types.js`.

3. **Node.js muy antiguo**:
   ```bash
   node --version  # requiere >= 20.0.0
   ```

4. **Error de dependencias nativas**: `ws` es puro JavaScript, no debería haber problemas. Si ves errores de compilación nativa:
   ```bash
   rm -rf node_modules package-lock.json
   npm install
   ```

---

## Error "not_connected" en todas las tools

**Síntomas**: `ps_list`, `ps_status` y cualquier tool responde `"not_connected"`.

**Causa**: chiaki-mcp no puede establecer la conexión WebSocket con chiaki-ng.

**Diagnóstico**:

```bash
# 1. Verifica que chiaki-ng está corriendo con MCP enabled
#    Abre chiaki-ng → Settings → pestaña MCP → Enable = ON

# 2. Verifica el puerto
curl -i http://127.0.0.1:9090 2>&1 | head -5
# Deberías ver "Upgrade: websocket" o conexión rechazada (no timeout)

# 3. Verifica el log de chiaki-ng
#    El McpServer imprime en stdout/stderr:
#    "McpServer: listening on 127.0.0.1:9090"
```

**Soluciones**:

1. Activar MCP en la UI: Settings → pestaña MCP → Enable
2. Verificar que el puerto en `CHIAKI_PORT` coincide con el configurado en la UI
3. Si chiaki-ng está en otra máquina, verificar conectividad de red:
   ```bash
   ping 192.168.1.100
   nc -zv 192.168.1.100 9090
   ```

---

## Error "no_session"

**Síntomas**: `ps_press`, `ps_home`, etc. responden `{error: "no_session"}`.

**Causa**: No hay una sesión activa con la PlayStation.

**Solución**:
```bash
# 1. Ver qué consolas hay
ps_list

# 2. Conectar
ps_connect({name: "PS5-Salon"})

# 3. Verificar estado
ps_status  # debe mostrar "connected" o "streaming"
```

---

## Error "no_stream" en ps_screenshot

**Síntomas**: `ps_screenshot` responde `{error: "no_stream"}`.

**Causa**: El stream de vídeo aún no ha comenzado. La transición `connected → streaming` puede tardar 2-10 segundos.

**Solución**:

```bash
# Espera activa:
ps_status  # ver state
# si state == "connected" → esperar 2-3 segundos
ps_status  # verificar de nuevo
# cuando state == "streaming" → ps_screenshot ya funciona
```

El MCP TypeScript emite notificaciones `state_change` — el LLM puede reaccionar a ellas en lugar de hacer polling.

---

## Error "not_registered" en ps_connect

**Síntomas**: `ps_connect` responde `{error: "not_registered"}`.

**Causa**: La consola no tiene `regist_key` guardado en QSettings.

**Solución**: Registrar la consola primero:

```bash
# 1. Iniciar pairing
ps_pair({host: "192.168.1.50"})

# 2. La PS5 muestra un PIN de 8 dígitos
# 3. Confirmar PIN
ps_pair_confirm({pin: "12345678"})

# 4. Verificar que aparece en ps_list como registered: true
ps_list
```

---

## Screenshots en negro o corruptos

**Síntomas**: `ps_screenshot` devuelve un JPEG completamente negro o con artefactos.

**Causas**:

1. **Primer frame tras conectar**: el decoder aún no tiene un keyframe (IDR).
   - Solución: esperar 2-3 segundos tras `state == "streaming"` antes del primer screenshot.

2. **Decoder no configurado**: si `SetFfmpegDecoder` no se llamó.
   - Solución: verificar que la sesión se inició desde la GUI (StreamSession). El MCP no puede crear sesiones standalone sin GUI.

3. **Codec no soportado**: PS5 usando HDR o resolución no estándar.
   - Solución: en la UI de chiaki-ng, Settings → Video → Codec: H264 (más compatible).

---

## Desconexiones frecuentes

**Síntomas**: La conexión WebSocket se cae cada pocos minutos.

**Causas y soluciones**:

1. **Red inestable**: verificar packet loss con `ps_status`:
   ```json
   {"packet_loss": 0.15}  // 15% → red inestable
   ```
   Solución: acercar el router, usar cable Ethernet, o bajar bitrate.

2. **chiaki-ng pierde la sesión con PS5**: es un problema de la sesión Remote Play subyacente, no del MCP.
   - Síntoma: `quit` notification con `reason: "connection_lost"`
   - Solución: reconectar con `ps_connect`. Verificar que la PS5 está encendida y en la misma red.

3. **Firewall / NAT**: puertos efímeros bloqueados.
   - Solución: asegurar que los puertos 9295-9308 UDP (Remote Play) no están bloqueados.

---

## Problemas de autenticación (token)

**Síntomas**: `chiaki-mcp` falla el handshake con error de autenticación.

**Causas**:

1. **Expose=true pero sin token en el cliente**:
   ```
   McpServer: auth required for non-localhost connection
   ```
   Solución: pasar `CHIAKI_TOKEN` al arrancar chiaki-mcp.

2. **Token incorrecto**: el token del cliente no coincide con el de QSettings.
   Solución: copiar el token exacto de Settings → MCP → Auth token.

3. **Token con caracteres especiales**: el token es hexadecimal puro (128 chars), pero asegúrate de no tener espacios o saltos de línea:
   ```bash
   CHIAKI_TOKEN="3f8a9b2c1d4e5f6a..."  # correcto
   CHIAKI_TOKEN="3f8a9b2c1d 4e5f6a..."  # incorrecto (espacio)
   ```

---

## MCP tab no aparece en Settings

**Síntomas**: La pestaña "MCP" no está visible en SettingsDialog.

**Causa**: La build no incluye los cambios de MCP (pestaña QML, settings MCP).

**Solución**:
```bash
# Verificar que la build es de feat/mcp-server o incluye los commits MCP
cd ~/projects/chiaki-ng
git log --oneline | grep -i mcp

# Si no hay commits MCP, construir desde feat/mcp-server:
git checkout feat/mcp-server
make configure && make build
```

---

## Cambios de settings no tienen efecto

**Síntomas**: Cambias enable/port/token en la UI pero el MCP sigue usando los valores antiguos.

**Causas**:

1. **StreamSession no está activa**: los cambios solo se aplican en caliente si hay una sesión activa. Sin sesión, los cambios se aplican la próxima vez que se inicie una.

2. **Señal Qt no conectada**: verificar que `HandleMcpSettingsChanged` está conectado:
   ```cpp
   // gui/src/streamsession.cpp
   connect(connect_info.settings, &Settings::mcpEnabledChanged, ...);
   ```

**Solución**: reiniciar la sesión de chiaki-ng (desconectar y reconectar).

---

## Diagnóstico con logs

### Activar logs en chiaki-ng

chiaki-ng escribe logs del McpServer en stdout/stderr. Ejecutar desde terminal para verlos:

```bash
./build/gui/chiaki-ng 2>&1 | grep -i mcp
```

Mensajes esperados:
```
McpServer: listening on 127.0.0.1:9090 (auth: disabled)
McpServer: new connection from 127.0.0.1
McpServer: handshake OK
McpServer: command 'list' from client
McpServer: state changed to 'connected'
McpServer: state changed to 'streaming'
```

### Activar logs en chiaki-mcp

```bash
LOG_LEVEL=debug node dist/index.js
```

Mensajes esperados:
```
[2026-07-14T...][chiaki-mcp][INFO] Conectando a chiaki-ng en 127.0.0.1:9090...
[2026-07-14T...][chiaki-mcp][INFO] Conectado a chiaki-ng McpServer ✓
[2026-07-14T...][chiaki-mcp][DEBUG] → list {}
[2026-07-14T...][chiaki-mcp][DEBUG] ← list ok {...}
```

### Verificar conectividad WebSocket

```bash
# Instalar websocat si no está disponible
brew install websocat  # o apt install websocat

# Conectar y hacer handshake
echo '{"type":"handshake","version":1}' | websocat ws://127.0.0.1:9090
# Respuesta esperada: {"type":"handshake_ok","version":1}

# Enviar un comando
echo '{"id":1,"cmd":"list"}' | websocat ws://127.0.0.1:9090
# Respuesta esperada: {"id":1,"ok":true,"hosts":[...]}
```
