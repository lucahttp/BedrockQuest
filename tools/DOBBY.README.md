# Dobby — instalación para mcbedvr (Android ARM64)

## 1. ¿Qué es?

[Dobby](https://github.com/jmpews/Dobby) es un hook engine ligero (similar a
Frida-gum o Substrate) que corre enteramente en userspace ARM64. mcbedvr lo
usa en Fase 2+ para ganchear funciones internas de `libminecraftpe.so` sin
parchear la .so en disco (compatible con el modelo de carga vía `dlopen`).

Licencia: GPL-3.0. La .so de mcbedvr.linkea dinámicamente, así que no obliga
a relicenciar mcbedvr.

## 2. Build nativo

```sh
git clone --depth 1 https://github.com/jmpews/Dobby.git /opt/Dobby
cd /opt/Dobby
# Compilar para Android ARM64 usando el toolchain del NDK.
cmake -B build-android \
    -DCMAKE_TOOLCHAIN_FILE=$ANDROID_NDK_HOME/build/cmake/android.toolchain.cmake \
    -DCMAKE_SYSTEM_NAME=Android \
    -DCMAKE_ANDROID_ARCH_ABI=arm64-v8a \
    -DCMAKE_ANDROID_API=29 \
    -DDOBBY_BUILD_SHARED=OFF \
    -DDOBBY_BUILD_STATIC=ON \
    .
cmake --build build-android -j
```

Artefacto esperado: `build-android/libdobby.a` (o según flags arriba).

Copiá la .a a una ruta accesible y exportá:

```sh
mkdir -p /opt/Dobby-install/lib/arm64-v8a
cp build-android/libdobby.a /opt/Dobby-install/lib/arm64-v8a/
mkdir -p /opt/Dobby-install/include
cp include/dobby.h /opt/Dobby-install/include/
export MCBEDVR_DOBBY_DIR=/opt/Dobby-install
```

## 3. Patch PICO OS

PICO OS corre con SELinux en modo enforcing. El `trampoline` que Dobby asigna
por defecto usa `mprotect()` para hacer la página RWX, lo cual es rechazado
por la política `untrusted_app`. Antes de habilitar Dobby en runtime, el
usuario tiene que permitirlo vía:

```sh
# En una ventana de adb con device en modo developer:
adb shell setenforce 0   # SOLO TEMPORAL — no es persistible
```

…lo cual es **inaceptable** para un release. Por eso mcbedvr Fase 3 va a
usar la variante de Dobby con **hook por PLT/GOT en lugar de inline patch**,
lo cual no necesita RWX. Track: [jmpews/Dobby#236](https://github.com/jmpews/Dobby/issues/236).

Hasta que ese PR mergue, **mcbedvr compila sin Dobby** (lo verifica el
`CMakeLists.txt` via `MCBEDVR_DOBBY_DIR` indefinido). El branch queda ABI-stable;
los hooks se activan más tarde sin recompilar.

## 4. ¿Por qué no MythicalTitanHooker o And64InlineHook?

Alternativas evaluadas:

| Lib | Mantenida | Soporte ARM64 Android | Licencia | Veredicto |
|---|---|---|---|---|
| **Dobby** | ✅ activo (2025+) | ✅ | GPL-3.0 | Elección |
| And64InlineHook | ⚠️ baja actividad | ✅ | MIT | Backup si Dobby falla |
| frida-gum | ✅ | ✅ | GPL-3.0 | ❌ overkill para libmcbedvr.so |
| Substrate | ⚠️ legacy | ✅ | BSD/MIT | ❌ no actualizada para Android 14+ |

Dobby es la apuesta correcta.
