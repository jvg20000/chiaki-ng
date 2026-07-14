# Ejemplos de uso con LLM

Casos prácticos de cómo un modelo de IA (Claude, GPT, Hermes) interactúa con una PlayStation a través del MCP.

---

## Ejemplo 1: Navegación básica

El LLM quiere ir al menú principal y abrir un juego.

```
Usuario: "Abre el menú principal y dime qué ves"

LLM → ps_home()
LLM ← {ok: true}

LLM → ps_screenshot()
LLM ← [imagen JPEG del menú principal]

LLM: "Veo el menú principal con varios juegos. Destiny 2 está seleccionado."

Usuario: "Ábrelo"

LLM → ps_press({buttons: ["cross"], duration_ms: 100})
LLM ← {ok: true}

... espera 5 segundos a que cargue ...

LLM → ps_screenshot()
LLM ← [imagen JPEG]

LLM: "El juego está cargando. Veo la pantalla de título de Destiny 2."
```

---

## Ejemplo 2: Navegación por menús con dpad

```
Usuario: "Ve a Configuración → Sonido y baja el volumen"

LLM → ps_press({buttons: ["dpad_up"]})    # subir en el menú
LLM → ps_press({buttons: ["dpad_right"]}) # entrar a Configuración
LLM → ps_press({buttons: ["dpad_down"]})  # bajar a Sonido
LLM → ps_press({buttons: ["cross"]})      # entrar
LLM → ps_press({buttons: ["dpad_down"], duration_ms: 500})  # bajar volumen

LLM: "Listo. He bajado el volumen 5 niveles."
```

---

## Ejemplo 3: Gameplay — mover personaje y atacar

```
Usuario: "Avanza y ataca al enemigo"

LLM → ps_stick({lx: 0, ly: -1.0})     # avanzar
LLM → ps_press({buttons: ["r2"]})      # atacar (R2)
LLM → ps_stick({})                      # soltar stick
LLM → ps_press({buttons: ["r2"], duration_ms: 0})  # soltar botón (implícito)

# Combinar stick + botón en un solo frame:
LLM → ps_stick({lx: 0.7, ly: -1.0})   # avanzar en diagonal
LLM → ps_press({buttons: ["r1"]})      # ataque especial
```

---

## Ejemplo 4: Escribir texto con teclado virtual

```
Usuario: "En el campo de búsqueda de la PS Store, busca 'Elden Ring'"

LLM → ps_keyboard({text: "Elden Ring"})

LLM: "He escrito 'Elden Ring' en el teclado virtual. ¿Confirmo la búsqueda?"

Usuario: "Sí"

LLM → ps_press({buttons: ["r2"]})  # R2 suele confirmar en teclado PS
```

---

## Ejemplo 5: Screenshot + razonamiento visual

```
Usuario: "Mira la pantalla y dime si hay enemigos cerca"

LLM → ps_screenshot()
LLM ← [imagen JPEG]

LLM: "Veo un minimapa en la esquina superior derecha con 3 puntos rojos (enemigos).
      También hay una barra de vida al 60% y un cofre cerrado a la izquierda."

Usuario: "Acércate al cofre"

LLM → ps_stick({lx: -1.0, ly: 0})
... 2 segundos ...
LLM → ps_stick({})
LLM → ps_press({buttons: ["square"]})  # interactuar

LLM: "Debería estar frente al cofre. He pulsado square para abrirlo."
```

---

## Ejemplo 6: Monitoreo de estado

```
Usuario: "¿Cómo va la conexión?"

LLM → ps_status()
LLM ← {
  "state": "streaming",
  "host": "PS5-Salon",
  "console": "PS5",
  "resolution": "1920x1080",
  "fps": 60,
  "codec": "H265",
  "bitrate_kbps": 15000,
  "packet_loss": 0.02,
  "rtt_ms": 5
}

LLM: "Conexión excelente: 1080p60 H265 a 15 Mbps, solo 0.02% packet loss y 5ms de latencia."
```

---

## Ejemplo 7: Sesión completa con setup

Flujo completo desde cero: registro, conexión, juego, desconexión.

```
Usuario: "Conéctame a la PS5 del salón y abre Fortnite"

# 1. Ver qué consolas hay
LLM → ps_list()
LLM ← [{name:"PS5-Salon", console:"PS5", host:"192.168.1.50", registered:true}]

# 2. Conectar
LLM → ps_connect({name: "PS5-Salon"})
LLM ← {ok:true, state:"connected"}

# 3. Esperar streaming
LLM → ps_status()
LLM ← {state:"streaming", ...}

# 4. Ir al menú principal
LLM → ps_home()

# 5. Screenshot para ver qué hay
LLM → ps_screenshot()
LLM ← [imagen]

# 6. Navegar a Fortnite
LLM: "Veo Fortnite en la segunda fila. Navegando..."
LLM → ps_press({buttons: ["dpad_down"]})
LLM → ps_press({buttons: ["dpad_right"]})
LLM → ps_press({buttons: ["cross"]})

# 7. Confirmar apertura
LLM → ps_screenshot()
LLM ← [imagen]
LLM: "Fortnite está cargando."

# 8. Cuando termines...
LLM → ps_sleep()
LLM ← {ok:true, state:"idle"}
```

---

## Ejemplo 8: Manejo de errores por el LLM

Un LLM con buenos prompts maneja errores gracefully:

```
LLM → ps_connect({name: "PS5-Cocina"})
LLM ← {error: "not_registered", message: "PS5-Cocina no está registrada"}

LLM: "La PS5 de la cocina no está registrada. Necesito emparejarla primero.
      ¿Está la consola encendida y en la misma red? Si es así, puedo iniciar el registro."

Usuario: "Sí, inicia registro"

LLM → ps_pair({host: "192.168.1.51"})
LLM ← {ok:true, message:"Introduce PIN de 8 dígitos"}

LLM: "La PS5 debería estar mostrando un PIN de 8 dígitos en pantalla. Dímelo y lo introduzco."
```

---

## Ejemplo 9: Detección de eventos (rumble)

```
Usuario: "Estoy peleando con un boss, ¿el mando debería vibrar?"

LLM → ps_events()
LLM ← []  # cola vacía

... espera ...

LLM → ps_events()
LLM ← [{"type":"rumble","left":200,"right":150}]

LLM: "Acabo de detectar vibración: motor izquierdo al 78%, derecho al 59%.
      El mando está respondiendo correctamente."
```

---

## Mejores prácticas para prompts

### Estructura recomendada

```
1. ps_list() — ver consolas disponibles
2. ps_connect() o ps_pair() — establecer conexión
3. ps_status() — verificar que el stream está activo
4. ps_screenshot() — ver el estado actual de la pantalla
5. Acciones (ps_press, ps_stick, etc.)
6. ps_screenshot() — verificar resultado
7. ps_disconnect() o ps_sleep() — al terminar
```

### Anti-patrones

- ❌ Llamar `ps_screenshot()` sin verificar que `state == "streaming"`
- ❌ Hacer 20 `ps_press()` seguidos sin verificar el resultado con screenshots
- ❌ No manejar `{error: "not_registered"}` — el LLM debe saber guiar el pairing
- ❌ Asumir que `ps_connect` es instantáneo — puede tardar segundos en llegar a `streaming`

### Timing

- Usar `duration_ms` solo cuando sea necesario (menús rápidos: 50-100ms, holds: 300-500ms)
- Entre acciones, esperar 50-200ms para evitar saturar el buffer del mando
- Después de `ps_connect`, esperar a que `state == "streaming"` antes de hacer screenshots
