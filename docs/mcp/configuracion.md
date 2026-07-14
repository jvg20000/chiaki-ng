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

- **Localhost** (`expose=false`): bind a `127.0.0.1`. **No requiere token**.
- **Red** (`expose=true`): bind a `0.0.0.0`. **Requiere token**. Si está vacío al activar, se autogenera uno de 128 caracteres hexadecimales.

El botón **"New"** regenera el token inmediatamente (desconecta clientes activos).

### Cambios en caliente

Cualquier cambio en enable/port/expose/token reinicia el `McpServer` automáticamente. No es necesario reiniciar chiaki-ng.

---

## Instalación de chiaki-mcp

### Opción A: GitHub Packages (recomendado, sin configurar secrets)

El CD publica automáticamente en GitHub Packages al mergear a `main`/`develop`. Usa `GITHUB_TOKEN` (siempre disponible).

```bash
# Configurar registro (una sola vez, necesita un PAT con read:packages)
npm config set @jvg20000:registry https://npm.pkg.github.com
npm config set //npm.pkg.github.com/:_authToken TU_GITHUB_TOKEN

# Instalar
npm install -g @jvg20000/chiaki-mcp
```

- **Estable**: `npm install -g @jvg20000/chiaki-mcp` (latest, desde main)
- **Desarrollo**: `npm install -g @jvg20000/chiaki-mcp@dev` (desde develop)

El binario queda como `chiaki-mcp` en el PATH.

### Opción B: Desde el repositorio (sin registros, sin token)

```bash
cd ~/projects/chiaki-ng/mcp/
npm install
npm run build
npm test
```

O instalar global desde local:
```bash
npm install -g ~/projects/chiaki-ng/mcp/
```

### Opción C: GitHub Release (tarball)

Descargar de [Releases](https://github.com/jvg20000/chiaki-ng/releases) (tag `mcp-vX.Y.Z`):
```bash
npm install -g https://github.com/jvg20000/chiaki-ng/releases/download/mcp-v1.0.0/chiaki-mcp-1.0.0.tgz
```

---

## Configuración de chiaki-mcp

Variables de entorno:

| Variable | Default | Descripción |
|---|---|---|
| `CHIAKI_HOST` | `127.0.0.1` | IP/hostname donde corre chiaki-ng |
| `CHIAKI_PORT` | `9090` | Puerto MCP WebSocket |
| `CHIAKI_TOKEN` | — | Token de autenticación (requerido si `expose=true`) |
| `CHIAKI_TIMEOUT` | `10000` | Timeout de comandos en ms |
| `CHIAKI_MAX_RETRIES` | `0` | Reintentos máximos por comando fallido |
| `LOG_LEVEL` | `info` | Nivel de log: `debug`, `info`, `warn`, `error` |

### Ejemplos

**Localhost (sin token)**:
```bash
CHIAKI_HOST=127.0.0.1 CHIAKI_PORT=9090 chiaki-mcp
```

**Remoto (con token)**:
```bash
CHIAKI_HOST=192.168.1.100 CHIAKI_PORT=9090 \
  CHIAKI_TOKEN=3f8a9b2c... \
  chiaki-mcp
```

**Debug**:
```bash
LOG_LEVEL=debug CHIAKI_HOST=127.0.0.1 chiaki-mcp
```

---

## Integración con Hermes

En `~/.hermes/profiles/<perfil>/config.yaml`:

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

Conexión remota (con token):
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

## Publicación (CD)

Al mergear a `main` o `develop` con cambios en `mcp/`:

| | main | develop |
|---|---|---|
| **GitHub Packages** | `@jvg20000/chiaki-mcp@latest` | `@jvg20000/chiaki-mcp@dev` |
| **GitHub Release** | Release estable | Prerelease |
| **Tag** | `mcp-vX.Y.Z` | `mcp-vX.Y.Z-dev-{sha}` |

Solo publica si `mcp/package.json` cambió de versión. Usa `GITHUB_TOKEN` (sin secrets manuales).

---

## Seguridad

1. **No exponer a internet** — usar VPN o SSH tunneling
2. **Rotar el token** — botón "New" en la UI
3. **No commitear tokens** — QSettings los guarda cifrados a nivel SO
4. **Firewall local** si `expose=true`

### SSH tunneling (alternativa a expose)

```bash
ssh -L 9090:127.0.0.1:9090 usuario@maquina-gaming
CHIAKI_HOST=127.0.0.1 CHIAKI_PORT=9090 chiaki-mcp
```
