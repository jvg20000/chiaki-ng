# MCP para chiaki-ng — Documentación

Integración de [Model Context Protocol (MCP)](https://modelcontextprotocol.io/) para chiaki-ng que permite a modelos de IA controlar una PlayStation 4/5 a través de Remote Play.

## Arquitectura

```
┌──────────────────────────────────────┐       ┌──────────────────────────┐
│  MÁQUINA GAMING (chiaki-ng)          │       │  SERVIDOR AI             │
│                                      │  WS   │                          │
│  ┌────────────┐  ┌────────────────┐  │←─────→│  chiaki-mcp (TypeScript) │
│  │ StreamSess │←→│  McpServer     │  │ JSON  │  → MCP SDK (stdio)      │
│  │ → PS5      │  │  (QWebSocket)  │  │       │  → Hermes / LLM         │
│  └────────────┘  └────────────────┘  │       └──────────────────────────┘
└──────────────────────────────────────┘
```

- **McpServer**: clase C++ embebida en `StreamSession`, expone un servidor WebSocket en el puerto configurado
- **chiaki-mcp**: proyecto TypeScript que habla WebSocket/JSON con chiaki-ng y expone herramientas MCP via stdio
- **Protocolo**: JSON-RPC sobre WebSocket con comandos (`connect`, `press`, `stick`, `screenshot`, etc.)

## Documentos

| Documento | Contenido |
|---|---|
| [Arquitectura](arquitectura.md) | Diseño final, componentes, decisiones de arquitectura |
| [API de herramientas](api.md) | Referencia completa de las 15 tools MCP (`ps_list`, `ps_press`, etc.) |
| [Configuración](configuracion.md) | Settings en la UI, variables de entorno, puertos, autenticación |
| [Flujo de conexión](conexion.md) | State machine, handshake WebSocket, ciclo de vida de sesión |
| [Ejemplos con LLM](ejemplos.md) | Casos de uso prácticos: navegación, gameplay, screenshots, debugging |
| [Troubleshooting](troubleshooting.md) | Problemas comunes, errores, diagnóstico |

## Diagrama de arquitectura

[Abrir diagrama interactivo](diagrama-arquitectura.html) (SVG, compatible con cualquier navegador)

## Licencia

Todo el código MCP es parte de chiaki-ng y está bajo AGPL v3.
