# MCBEDVR: Minecraft Bedrock Standalone VR para PICO 4 Ultra

Proyecto de investigación e ingeniería inversa para ejecutar Minecraft Bedrock en modo Realidad Virtual 6DoF nativo y standalone en visores PICO 4 Ultra (Pico OS / Android ARM64).

## Ubicación del Proyecto
- Ruta absoluta: `/Volumes/X10Pro/Projects/mcbedvr`
- Disco: Confinado en almacenamiento externo `X10Pro`

## Arquitectura de Inyección

```
[ Minecraft Bedrock APK (ARM64) ]
               │
               ▼
       [ libmcbedvr.so ]  <─── Inyección vía JNI_OnLoad o patchelf
               │
   ┌───────────┴──────────────────────────────┐
   ▼                                          ▼
[ OpenXR PICO Runtime ]              [ Dobby Hooks / Símbolos ]
- Swapchain estereoscópica           - Mapeo vía minecraft-symbols
- Dual Viewport (Ojo Izq / Der)      - Interceptación de Camera / Render
- Input 6DoF mandos PICO 4 Ultra     - Interceptación de eglSwapBuffers
```

## Estado de la Investigación
Ver [research/prior-art-analysis.md](research/prior-art-analysis.md).
