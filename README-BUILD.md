# Cómo build-ear libmcbedvr.so

Esta guía cubre las dependencias externas que el `CMakeLists.txt` espera
encontrar antes de invocar `scripts/build_android_arm64.sh`.

## 1. Headers OpenXR (obligatorio)

mcbedvr usa los headers canónicos de Khronos o — equivalentemente — los del
PICO OpenXR Mobile SDK v1.4.0+. Cualquiera sirve, ya que la API es estable.

```sh
git clone --depth 1 --branch release-1.1.45 \
    https://github.com/KhronosGroup/OpenXR-SDK.git /opt/OpenXR-SDK
export MCBEDVR_OPENXR_DIR=/opt/OpenXR-SDK
```

El `CMakeLists.txt` verifica que exista `${MCBEDVR_OPENXR_DIR}/include/openxr/openxr.h`
y aborta si no. No es necesario compilar nada del SDK — sólo necesitamos headers.

> **¿Y el loader binario?** No lo enlazamos. En runtime hacemos dlopen del
> `/system/lib64/libopenxr.so` que provee PICO OS en el visor. Compilar el
> loader de Khronos sería desperdicio de espacio.

## 2. NDK r27 (obligatorio)

Si la variable `ANDROID_NDK_HOME` no apunta al NDK, el script busca en
`/Volumes/X10Pro/Android/sdk/ndk/27.0.11718014` (instalación de la cadena
Halo). Para otra ubicación exportá la var antes.

## 3. Dobby (opcional en Fase 1)

Ver `tools/DOBBY.README.md`. Sin Dobby, mcbedvr compila pero los hooks de Fase 2
se degradan a stubs inertes. Requerido a partir de Fase 3.

## 4. Build

```sh
cd /Volumes/X10Pro/Projects/mcbedvr
./scripts/build_android_arm64.sh
```

Salida esperada:

```
[mcbedvr] NDK           = /Volumes/X10Pro/Android/sdk/ndk/27.0.11718014
-- The CXX compiler identification is Clang 17.0.x
...
[mcbedvr] OK: /Volumes/X10Pro/Projects/mcbedvr/build/dist/arm64-v8a/libmcbedvr.so
[mcbedvr] OK: ...  ELF 64-bit LSB shared object, ARM aarch64 ...
```

## 5. Sanity check (host)

Para verificar que el .cpp compila sin NDK (útil en CI sin device):

```sh
g++ -std=c++20 -Isrc -DMACRO_HOST_PATH=1 \
    src/Symbols.cpp src/Xr.cpp src/JNI_OnLoad.cpp \
    -shared -fvisibility=hidden \
    -o /tmp/libmcbedvr-host.so
```

(Las guards en `Log.h` y en `Xr.cpp` redirigen dlopen/log a stderr para no
romper en macOS.)

## 6. Próximos pasos (Fase 2)

- Compilar Dobby.
- Correr `nm -D` / Ghidra sobre `libminecraftpe.so 1.21.x` (APK legítimo) para
  validar / regenerar las firmas de `Symbols.cpp`.
- Reemplazar las entradas marcadas `PENDING REA` por offsets reales.

## 7. Próximos pasos (Fase 3)

- Implementar `src/VrCamera.cpp` (matriz view/projection derivada de los símbolos
  resueltos; inyectada en el swapchain de RenderDragon).
- Implementar `src/RenderBridge.cpp` (pegar el hook Dobby sobre el render loop real).
- Integrar el mapping de interacción que escribió @Emilio Ravenna en
  `docs/controller-ux.md`.
