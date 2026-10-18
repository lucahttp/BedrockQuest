# Análisis Forense de Prior Art: Minecraft Bedrock VR Standalone en PICO 4 Ultra

## 1. thebreenis/minecraft-gearvr-fixed-for-quest3
- **Base:** Minecraft Bedrock v1.16.40.02 (Gear VR Edition oficial).
- **Hallazgo Crítico de Arquitectura:** Las últimas versiones de Gear VR SÍ incluyeron librerías nativas de 64 bits (`arm64-v8a`), derribando el mito de que era 100% de 32 bits.
- **Limitación en PICO OS:** Fue parcheado para Meta HorizonOS, que provee `libvrapi.so`. PICO OS no tiene VrApi de Meta.
- **Solución PICO:** Utilizar un shim `libvrapi.so -> OpenXR` (como el desarrollado en `OVRPort` / `ovrplugin-openxr-shim`).

## 2. minecraft-linux / mcpelauncher-manifest
- **Propósito original:** Ejecutar `libminecraftpe.so` de Android en Linux y macOS x86/ARM64 sin Android OS mediante una reimplementación de Bionic libc y JNI.
- **Por qué no tiene VR:** Renderiza en una ventana única 2D mediante EGL/GLFW sin hooks de matriz estéreo ni OpenXR.
- **El tesoro técnico extraído (`minecraft-symbols`):** El repositorio `minecraft-linux/minecraft-symbols` contiene desensamblado y mapeo de tablas de símbolos (`symbols` y `vtables`) de `libminecraftpe.so`:
  - `_ZN13MinecraftGame...` (ciclo del juego, inicialización).
  - `_ZN3App16setRenderingSizeEii` y `_ZN3App17setUISizeAndScaleEiif` (viewport y UI).
  - `_ZN3mce8Platform3OGL12InitBindingsEv` (abstracción gráfica de RenderDragon / GLES).

## 3. dubrovskiy-yevhen-stakelogic / gta-sa-vr-quest
- **Patrón de inyección de oro:** Inyector nativo en C++ (`libsavr.so`) con OpenXR embebido cargado en `JNI_OnLoad`.
- **Resolución de símbolos stripped:** `Symbols.cpp` usa firmas binarias para resolver funciones críticas en un binario cerrado de Rockstar sin símbolos de depuración.
- **Bifurcación de cámara:** `VrCamera.cpp` intercepta la matriz de cámara para doble pase estéreo.
- **Puntería y tracking 6DoF:** `ScopeAim.cpp` y `Xr.cpp` desacoplan la orientación de cabeza y manos.

## 4. Estrategia mcbedvr para PICO 4 Ultra
- **Ruta A (Inmediata / Motor Oficial):** Parchear Bedrock v1.16.40.02 ARM64 con shim `libvrapi.so -> OpenXR Pico`.
- **Ruta B (Bedrock Moderno 1.21+):** Scaffolding de inyección nativa `libmcbedvr.so` combinando los símbolos de `mcpelauncher` con el pipeline OpenXR de `gta-sa-vr-quest`.
