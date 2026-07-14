# Configuración MCP

## Configuración en chiaki-ng (UI)

Accesible desde la pestaña **"MCP"** en Settings (`SettingsDialog.qml`):

| Setting | QSettings key | Default | Rango | Widget |
|---|---|---|---|---|
| **Enable** | `settings/mcp_enabled` | `false` | — | Switch ON/OFF |
| **Port** | `settings/mcp_port` | `9090` | 1024–65535 | SpinBox |
| **Expose to network** | `settings/mcp_expose` | `false` | — | Switch |
| **Auth token** | `settings/mcp_token` | `""` | 128 chars hex | TextField (read-only) + botón **New** |

### Reglas de autenticación

- **Localhost** (`expose=false`): bind a `127.0.0.1`. **No requiere token** — el handshake WebSocket se acepta sin `token`.
- **Red** (`expose=true`): bind a `0.0.0.0`. **Requiere token** — el cliente debe incluir `"token": "<hex>"` en el handshake. Si está vacío al activar, se autogenera uno de 128 caracteres hexadecimales.

El botón **"New"** regenera el token inmediatamente (desconecta clientes activos).

### Cambios en caliente

Cualquier cambio en enable/port/expose/token reinicia el `McpServer` automáticamente via `HandleMcpSettingsChanged()` en `StreamSession`. No es necesario reiniciar chiaki-ng.

---

## Configuración de chiaki-mcp (TypeScript)

El servidor MCP TypeScript se configura con variables de entorno:

| Variable | Default | Descripción |
|---|---|---|
| `CHIAKI_HOST` | `127.0.0.1` | IP/hostname donde corre chiaki-ng |
| `CHIAKI_PORT` | `9090` | Puerto MCP WebSocket |
| `CHIAKI_TOKEN` | — | Token de autenticación (requerido si `expose=true`) |
| `CHIAKI_TIMEOUT` | `10000` | Timeout de comandos en ms |
| `CHIAKI_MAX_RETRIES` | `0` | Reintentos máximos por comando fallido |
| `LOG_LEVEL` | `info` | Nivel de log: `debug`, `info`, `warn`, `error` |

### Ejemplos de configuración

**Localhost (sin token)**:
```bash
CHIAKI_HOST=127.0.0.1 CHIAKI_PORT=9090 node dist/index.js
```

**Remoto (con token)**:
```bash
CHIAKI_HOST=192.168.1.100 CHIAKI_PORT=9090 \
  CHIAKI_TOKEN=3f8a9b2c1d4e5f6a7b8c9d0e1f2a3b4c5d6e7f8a \
  node dist/index.js
```

**Debug**:
```bash
LOG_LEVEL=debug CHIAKI_HOST=127.0.0.1 node dist/index.js
```

---

## Integración con Hermes

Configurar en `config.yaml` (el comando varía según instalación):

**Con npm global:**
```yaml
mcp:
  servers:
    - name: chiaki-ng
      type: stdio
      command: chiaki-mcp
      env:
        CHIAKI_HOST: "127.0.0.1"
        CHIAKI_PORT: "9090"
```

**Desde el repositorio:**
```yaml
mcp:
  servers:
    - name: chiaki-ng
      type: stdio
      command: node
      args:
        - /ruta/a/chiaki-ng/mcp/dist/index.js
      env:
        CHIAKI_HOST: "127.0.0.1"
        CHIAKI_PORT: "9090"
```

**Conexión remota (con token):**
```yaml
mcp:
  servers:
    - name: chiaki-ng
      type: stdio
      command: chiaki-mcp
      env:
        CHIAKI_HOST: "192.168.1.100"
        CHIAKI_PORT: "9090"
        CHIAKI_TOKEN: "3f8a9b2c..."
```

---

## Seguridad

### Mejores prácticas

1. **No exponer a internet directamente** — usar VPN o SSH tunneling si necesitas acceso remoto
2. **Rotar el token periódicamente** — botón "New" en la UI
3. **No compartir el token en logs ni commits** — el token se guarda en QSettings (cifrado a nivel de sistema operativo)
4. **Firewall local**: si `expose=true`, asegurar que solo IPs de confianza alcancen el puerto

### SSH tunneling (alternativa a expose)

En lugar de `expose=true`, tuneliza el puerto:

```bash
# En la máquina AI
ssh -L 9090:127.0.0.1:9090 usuario@maquina-gaming

# Luego conectas a localhost sin token
CHIAKI_HOST=127.0.0.1 CHIAKI_PORT=9090 node dist/index.js
```

---

## Instalación

### Desde npm (recomendado)

```bash
npm install -g chiaki-mcp
```

El paquete se publica automáticamente al mergear a `main` (tag `latest`) o `develop` (tag `dev`):
- **Estable**: `npm install -g chiaki-mcp` (latest, desde main)
- **Desarrollo**: `npm install -g chiaki-mcp@dev` (desde develop)

También disponible como [GitHub Release](https://github.com/jvg20000/chiaki-ng/releases) con tarball adjunto (tag `mcp-vX.Y.Z`).

### Desde el repositorio (desarrollo)

```bash
cd mcp/
npm install
npm run build        # tsc → dist/
npm test             # vitest (21 tests)
```

Producción:
```bash
npm ci --omit=dev    # solo deps de producción
node dist/index.js
```
