# mcbedvr · Controller UX & Diegetic UI Spec

> **Autor:** @Emilio Ravenna (Product Experience & Frontend Lead)
> **Audiencia:** @Pablo Lamponne (implementación del shim), @Mario Santos (arquitectura), @Gabriel Medina (validación)
> **Target hardware:** PICO 4 Ultra + 2x PICO 4 Ultra Controllers (6DoF)
> **Target game:** Bedrock 1.16.40.02 (vía shim libvrapi.so → OpenXR, Vía A)
> **Versión:** 0.1 — Boceto de diseño para revisión

---

## 1 · El principio rector

**La UI vive en el mundo, no en tu cara.**

QuestCraft ganó la conversación VR porque la interfaz **se porta como un objeto físico**: el hotbar como pulsera, el inventario como mesa flotante, el raycast del láser como única forma de interactuar con UI 2D. Lo que proponíamos en el blueprint de Mario era replicar ese patrón para Bedrock.

**Reglas duras:**
- **Nunca** overlay 2D plano en el centro de la pantalla. Eso es modo "ventana" y rompe la presencia.
- **Siempre** que se pueda, lo que pide el jugador es algo que tiene masa y costuras.
- **Toda** interacción con UI se hace con láser desde el mando dominante (raycast de 5m máx).
- **El silencio visual es progreso.** Si no la usás, no la pantalla lo invade.

---

## 2 · Mapeo de hardware

### 2.1 PICO 4 Ultra Controller (referencia rápida)

```
        [ Trigger (índice) ]
              │
   ┌──────────┼──────────┐
   │                     │
   │   ┌─────┐    ┌──┐   │
   │   │  Y  │    │ B│   │ ← Face buttons
   │   └──┬──┘    └─┬┘   │
   │   ┌──┴──┐    ┌─┴┐   │
   │   │  X  │    │ A│   │
   │   └─────┘    └──┘   │
   │                     │
   │      ┌──────┐       │
   │      │ Stick│       │ ← Thumbstick (clickable)
   │      └──────┘       │
   │                     │
   └────[ Grip (mano) ]──┘
              │
        [ Menu Button ]
```

### 2.2 Convenciones

- **Mano dominante** (configurable, default = derecha) = **acción** (lo que empuña pico/espada, lo que apunta, lo que selecciona en UI).
- **Mano secundaria** = **movimiento** (joystick de locomoción, hotbar, brújula).
- Ambos joysticks son analógicos con click.

---

## 3 · Mapeo de controles (Bedrock 1.16.40)

### 3.1 Mano secundaria (izquierda por default)

| Input | Acción Bedrock | Notas |
|---|---|---|
| **Stick X/Y** | Locomoción (caminar/strafe) | Snap-turn disponible (ver §6) |
| **Stick click** | Sprint (correr manteniendo click) | Vibración háptica corta al activar |
| **Grip (mantener)** | Sneak (agacharse sigiloso) | Sticky: requiere soltar para parar |
| **Trigger** | Place block (colocar bloque seleccionado) | Raycast desde la palma, no desde el dedo |
| **X** | Jump (saltar) | Doble tap = sprint-jump (mecánica Bedrock) |
| **Y** | Abrir/cerrar chat / comandos | Toggle |
| **Menu** | Toggle inventario principal (E) | Pulsa corta |

### 3.2 Mano dominante (derecha por default)

| Input | Acción Bedrock | Notas |
|---|---|---|
| **Stick X/Y** | Rotación de cámara (yaw / pitch suave) | Snap-turn o smooth según comfort |
| **Stick click** | Switch hotbar slot (cycle) | Mantener = abre radial menu |
| **Grip (mantener)** | Mine / Use (picar / abrir cofre) | Repetición: cada 250ms mientras se sostiene apuntando a bloque |
| **Trigger** | Attack / Break (atacar / romper con pico) | Pulsa rápida = 1 hit; mantener = continuo |
| **A** | Drop item (tirar objeto seleccionado) | Un item, no stack completo |
| **B** | Swap hands (intercambia hotbar con offhand) | Si el slot está vacío, swap a mano dominante |
| **Menu** | Pause / Game menu | Esc |

### 3.3 Gestos compuestos (capacidades del runtime OpenXR)

| Gesto | Detección | Acción |
|---|---|---|
| **Ambas manos frente a cara** (palmas a 30cm de HMD, 2s) | OpenXR hand tracking o pose-detection | Abre "radial menu" (crafting quick-access) |
| **Mano dominante al hombro contrario** | Pose-Distance < 20cm | Equipar armadura desde inventario (en mundo) |
| **Agitar ambas manos** | Aceleración > 3G | Emote de celebración / wake-up tras respawn |

---

## 4 · HUD diegético (geometría espacial)

> Coordenadas en espacio local del jugador (Y-up, -Z adelante).

### 4.1 Hotbar (pulsera flotante)

```
                    ┌─ Vista de jugador mirando al frente
                    │
   ┌────[Cabeza]────┤
   │                │
   │   ╱──╲         │
   │  │ 9 1 2 │ ← Hotbar aparece como semicírculo
   │  │ 3 4 5 │    pegado a la muñeca izquierda
   │   ╲──╱         │
   │                │   Solo visible si la muñeca
   │   [Muñeca I]←──┘   está a < 1.2m de la cabeza
   │                │   Y rotación palmar > 60°
   │                │
   └────[Cintura]───┘
```

- **Posición:** Anclada al hueso de muñeca izquierda. Radio: 12cm del centro de la palma.
- **Orientación:** Siempre perpendicular al eje del antebrazo + tilt hacia el HMD (15°).
- **Slots:** 9 bloques en arco (cada uno 2.4cm x 2.4cm, separados 1.6cm).
- **Slot activo:** Borde más grueso + glow sutil (cyan #6FFFE9 @ 60% alpha).
- **Visibilidad:**
  - **Oculto** si la muñeca está fuera del FOV del HMD o a > 1.2m de la cabeza.
  - **Fade-in 200ms** cuando entra en zona válida.
  - **Fade-out 400ms** cuando sale.
- **Interacción:** Láser desde la mano dominante. Selección = click con Trigger.

### 4.2 Inventario (mesa flotante)

```
    [ HMD ]
       │
       │  1.5m
       ↓
   ╔════════════════════════╗  ← Plano flotante
   ║ [Crafting][Armor][Inv] ║     0.9m x 0.6m
   ║ ┌──┬──┬──┬──┬──┬──┬──┐ ║
   ║ │  │  │  │  │  │  │  │ ║     Visible al pulsar
   ║ ├──┼──┼──┼──┼──┼──┼──┤ ║     "Menu" en mano izq
   ║ │  │  │  │  │  │  │  │ ║
   ║ └──┴──┴──┴──┴──┴──┴──┘ ║     Láser derecho
   ║ [Craft output]         ║     selecciona slots
   ╚════════════════════════╝
```

- **Posición:** 1.5m frente al jugador, 0.8m sobre el suelo, perpendicular a la mirada del HMD (paralelo al plano de los ojos).
- **Tamaño:** 0.9m ancho × 0.6m alto. Escala final = 4× el inventario 2D original (todo se ve).
- **Activación:** Toggle con `Menu` en mano izquierda. **NO aparece** automáticamente.
- **Interacción:**
  - **Láser desde mano dominante** para seleccionar slot.
  - **Trigger** para "agarrar" el slot → entra en cursor (sigue al láser).
  - **Trigger otra vez** para "soltar" en el slot apuntado.
  - **Grip sostenido** = "agarrar stack completo".
  - **B** = "swap con cursor".
  - **Y** = "ordenar slot".
- **Salir:** Menú izq, o alejar la mesa del FOV a > 2m, o click stick derecho.

### 4.3 Compass / Clock (muñeca derecha)

- **Posición:** Cara dorsal de la muñeca derecha (espejo del hotbar izquierdo).
- **Contenido:** Aguja de brújula + hora del mundo + indicador de dimensión (Overworld/Nether).
- **Visibilidad:** Mismas reglas que el hotbar (muñeca a < 1.2m de cabeza).

### 4.4 Health / Hunger (indicadores de "vitalidad")

- **Implementación:** **No 2D.** En lugar de eso:
  - **Borde del FOV** se torna rojo sutil (8% saturación) cuando la salud baja a < 50%.
  - **Vibración háptica bilateral** al recibir daño (intensidad = daño recibido, máx 8.0G).
  - **Indicador de hambre** = sutil oscurecimiento cromático del mundo (post-process desat -15%) cuando hambre < 30%.
  - **Indicador de aire** = viñeta azul + vibración fuerte al estar bajo agua sin respirar.

> **Por qué no corazones 2D:** rompe la presencia. La persona YA sabe que tiene hambre porque el páncreas se lo dice. Lo que necesita es **un aviso periférico**, no un overlay invasivo.

### 4.5 Crosshair (no existe como tal)

- **El láser de la mano dominante ES el crosshair.** Una línea cyan de 2mm de grosor que se extiende 5m desde el centro de la palma hacia donde apunta la mano.
- **Al apuntar a un bloque:** línea se torna amarilla + aparece outline 3D del bloque target (depth-test off).
- **Al apuntar a UI:** línea se torna verde + shrink a 1.5mm (feedback de "esto es interactivo").

---

## 5 · Onboarding (los primeros 60 segundos)

> El momento más importante. Lo que el jugador vive en el primer minuto define si cuenta la experiencia a un amigo o se lo devuelve al vendedor.

### 5.1 Secuencia de spawn

| t (s) | Evento | Diseño |
|---|---|---|
| 0 | Player aparece de pie | En la cima de un risco (chunks nuevos). Coordenada (0, 90, 0) sobre un pico de montaña. |
| 0–2 | Fade-in visual | Negro → visión periférica → centro. Fade 1500ms. |
| 2–4 | Reveal del atardecer | El jugador está mirando un sunset épico. Sol bajo en horizonte. Música: "Sweden" de C418. |
| 4–6 | Hands reveal | Aparecen las manos del jugador (render sincronizado con poses de PICO). Empuñan: pico de madera (der) + bloque de tierra (izq). |
| 6–8 | Hotbar reveal | Al levantar la muñeca izquierda > 60°, aparece la pulsera-hotbar con fade-in 200ms. |
| 8–10 | Crosshair reveal | Aparece el láser cyan desde la palma derecha. |
| 10–12 | Achievement | "Welcome to VR" achievement: sonido + vibración háptica bilateral 4G, 250ms. Toast en la muñeca derecha (no en pantalla). |
| 12–30 | Tutorial implícito | El jugador ve bloques de tierra alrededor. Invita a picar. Primer minado: vibración háptica en Trigger al romper. Item vuela al hotbar. |
| 30–60 | Libertad | Sin texto, sin flechas. El jugador explora. |

### 5.2 El "momento Pitch"

> Cuando un amigo le pregunta al dueño del visor "¿y esto qué es?", este video de 60 segundos es la venta.

**Storyboard del pitch:**
1. Se ve a la persona poniéndose el visor en la sala de su casa.
2. Pantalla del celular muestra "mcbedvr" cargando.
4. Dentro del visor: dusk en una montaña. Las manos se ven renderizadas.
5. El jugador picotea un bloque. El bloque se rompe.
6. Gira la muñeca y aparece la pulsera-hotbar flotante.
7. Aprieta el menú: mesa de inventario se materializa frente a él.
8. Voice-over (off): "Esto es Minecraft Bedrock nativo en PICO 4 Ultra. Sin PC. Sin streaming. Sin QuestCraft."

> Ese clip de 60 segundos es lo que va al grupo de WhatsApp / Discord / Reddit.

---

## 6 · Comfort settings (anti-vómitos)

> **Default saludable:** las opciones más cómodas activadas por defecto. El comfort no es opt-in, es el suelo mínimo.

### 6.1 Locomoción

| Setting | Default | Rango | Notas |
|---|---|---|---|
| **Tipo de movimiento** | **Snap-turn + Teleport** | Smooth / Snap / Teleport | Smooth sólo opt-in si activa comfort mode |
| **Snap turn ángulo** | 45° | 30°/45°/90° | Reducir mareo en nuevos jugadores |
| **Smooth turn velocidad** | 90°/s | 30°–180°/s | |
| **Vignette en movimiento** | **ON** | ON/OFF | Oscurecer bordes 25% al usar stick. Reduce vection |
| **Vignette intensidad** | 0.25 | 0.10–0.40 | |
| **Movement velocidad base** | 4.317 m/s (Bedrock default) | 1.0–8.0 m/s | Sprint x1.3 |
| **Sprint por stick** | Click stick izq | Click / Doble tap stick | |

### 6.2 Comfort overlays

| Setting | Default | Notas |
|---|---|---|
| **Vignette por comfort** | ON | Bordes oscuros al rotar / mover |
| **Fade-to-black** | Activar manualmente | Con botón de pánico (ej. doble-tap Menu) |
| **HUD estático** | OFF | Si se activa, el HUD no sigue a la cabeza |

### 6.3 Botones de seguridad

| Acción | Input |
|---|---|
| **Recentrar pose** | Mantener Menu 2s en ambas manos |
| **Reset inventario flotante** | Click stick derecho |
| **Quit to dashboard** | Long press Menu (3s) |

---

## 7 · Edge cases

### 7.1 Una mano perdida (tracking falla)

- **Si mano dominante se pierde > 1.5s:** backup automático a la mano secundaria (crosshair salta a la izq).
- **Si ambas manos se pierden > 3s:** HUD en relieve aparece en el centro del FOV (modo emergencia), locomotion sigue activa.
- **Indicador:** borde del FOV titila en ámbar (sin invadir la vista central).

### 7.2 Jugador zurdo

- **Setting "Handedness":** en menú principal.
- **Swap total:** los mapeos de las manos se intercambian; el sistema sigue siendo consistente.
- **Persiste en:** `gamerules` del mundo (Bedrock guarda por player profile).

### 7.3 Inventario + mob agresivo

- **Si el jugador recibe daño mientras inventario está abierto:** vibración háptica 8G + inventario se desvanece (fade-out 200ms) → vuelve al mundo.
- **No bloquea input de combate:** el inventario se cierra antes de procesar daño.

### 7.4 Primera vez en servidor multiplayer

- **Reveal de avatars:** los otros jugadores se renderizan como avatares de cuerpo completo (esqueleto + skin) en lugar de simple busto 2D. Coste: alto. Trade-off: vale la pena para presencia social.
- **Chat:** input de voz (PICO 4 Ultra tiene 2x micrófonos beamforming). Sin teclado flotante.
- **Emotes:** ambos manos al hombro = wave (saludar). Detección de pose simple.

---

## 8 · Referencias que tenemos que imitar (benchmarks)

| Proyecto | Qué copiamos | Fuente |
|---|---|---|
| **QuestCraft / Vivecraft** | Diegetic UI completa (hotbar pulsera, inventario mesa flotante, laser pointer) | `mcxr-org/MCXR` (halted pero el código Vivecraft sigue vivo en PRs) |
| **gta-sa-vr-quest** | ScopeAim (desacople cabeza ↔ arma), Driving/Locomotion context-switching | `dubrovskiy-yevhen-stakelogic/gta-sa-vr-quest/native/src/` |
| **Half-Life: Alyx** | Comfort settings granulares + onboarding natural sin tutoriales forzados | Referencia de diseño, no implementación |
| **Beat Saber** | Hands renderizadas con latencia < 30ms (crítico para presencia) | Best-in-class de sincronización pose ↔ render |

---

## 9 · Lo que necesita Pablo del shim

Para implementar esto, el shim `libvrapi.so → OpenXR` debe exponer a Bedrock (vía JNI / hooks) los siguientes hooks de pose + input:

| Hook | Propósito |
|---|---|
| `vrapi_GetHmdPose()` | Pose del HMD por frame (head-tracking) |
| `vrapi_GetControllerPose(hand)` | Pose de cada mando (mano dominante y secundaria) |
| `vrapi_GetButtonState(hand, button)` | Estado de cada botón (trigger, grip, face, stick) |
| `vrapi_GetAnalogValue(hand, axis)` | Stick X/Y por mando |
| `vrapi_Get推荐的Gestures(...)` (custom) | Hand-tracking gestures (palma frente a cara, agitar) |

**Lo que NO expone libvrapi.so original:**
- Hand-tracking puro (sólo controllers) → cubrimos con pose-detection simple sobre controllers.
- Vibration háptica diferenciada por mano → usamos `xrApplyHapticFeedback` por subaction path.

---

## 10 · Métricas de éxito (cómo sabemos que el producto está listo)

| Métrica | Target |
|---|---|
| **Time-to-first-block-broken** | < 90s desde carga (incluyendo onboarding) |
| **Frame time** | < 11.1ms (90 FPS) sostenido |
| **Input lag pose↔render** | < 30ms (mando visible) |
| **Comfort score** (survey a 10 testers) | ≥ 8/10 sin motion sickness tras 30min |
| **Net Promoter Score** | ≥ 7 ("se lo recomendaría a un amigo") |
| **Onboarding completion** (logran minar antes del min 5) | ≥ 95% |
| **Crash rate** | < 1% en sesiones de 1h |

---

## 11 · Decisiones abiertas que necesito del user

1. **Handedness default:** ¿diestro o configurable desde el principio? Recomiendo configurable.
2. **Snap-turn vs Smooth-turn default:** snap 45° + teleport es el más seguro para nuevos. ¿OK?
3. **Inventario diegético vs clásico 2D:** ¿full diegético desde día 0 o fallback 2D como setting? Recomiendo diegético only.
4. **Avatares multiplayer cuerpo completo:** ¿scope del MVP o post-launch?
5. **Audio engine:** ¿FMOD (que ya usa Bedrock) + spatial HRTF? Bedrock ya trae FMOD, sólo hay que confirmar binaural.

---

**Versión:** 0.1 (borrador)
**Próximo:** @Pablo Lamponne — revisá §3 y §9, dame feedback si algún hook no es obtenible desde el shim. @Mario Santos — revisá §11, necesito decisiones para bajar a v0.2.

— Emilio Ravenna