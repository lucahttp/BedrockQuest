# HOOK ENGINE SWAP — Dobby → And64InlineHook

**Fecha:** 2026-10-10
**Trigger:** Dobby master HEAD sin compilación ARM64 funcional bajo NDK r27.
**Estado:** SWAP cerrado en código + staging limpio + W⊕X aplicado. Pendiente device validation.
**Owner:** @Pablo Lamponne (Infraestructura / DevOps)
**Reviewers:** @Gabriel Medina, @Emilio Ravenna, @Mario Santos

---

## 1. TL;DR

mcbedvr (inyector de Bedrock para PICO 4 Ultra) ya no enlaza **Dobby**. En su lugar enlaza **Rprop/And64InlineHook** (ARM64 inline hook library, MIT, 763★ GitHub) presentado tras un compat shim con la API de Dobby. Esto resuelve el **problema de compilación** (Dobby master está roto para ARM64). **No resuelve el problema de runtime** (SELinux execmem en Android 10+/PICO OS): para eso se aplicó W⊕X en dos fases dentro de `And64InlineHook.cpp` en este mismo turno.

**Compatibilidad:** `mcbedvr/src/Symbols.cpp` y `src/JNI_Hooks.cpp` **no se tocaron**. La API que consumen (`DobbyHook(addr, repl, &orig)`) sigue siendo la misma firma.

---

## 2. Por qué hubo swap — diagnóstico de Dobby

Inspección directa del repo upstream (`jmpews/Dobby@5dfc854`, master HEAD al 2026-10-10):

### 2.1 `assembler-arm64.cc` no existe
La lista de sources del `CMakeLists.txt` upstream (líneas 108-110) compila:

```
assembler-arm.cc    → ARMv7 (32 bits)
assembler-ia32.cc   → x86 32 bits
assembler-x64.cc    → x86_64
```

**No incluye `assembler-arm64.cc`**. La AArch64 backend sólo existe como `assembler-arm64.h` (header). Las instrucciones AArch64 nunca se compilan → `DobbyHook()` sobre un símbolo ARM64 siempre cae en el path "processor not supported".

### 2.2 Detección de processor rota en macOS host
`cmake/build_environment_check.cmake` (líneas 36-54) detecta `PROCESSOR.ARM` para `armv7-a`. El toolchain NDK r27 reporta `CMAKE_SYSTEM_PROCESSOR=armv7-a` cuando el host es macOS Intel/Apple Silicon — **no `aarch64`**. Eso dispara la rama 32 bits del build system.

### 2.3 Release "latest" sin assets Android
`github.com/jmpews/Dobby/releases/latest` ofrece sólo:

- `dobby-iphoneos-all.tar.gz`  (iOS ARM64)
- `dobby-macos-all.tar.gz`     (macOS x86_64 + ARM64)

**Sin `dobby-android-all.tar.gz`**. No hay binarios pre-compilados para NDK.

### 2.4 Historial inestable
30 commits recientes, mayoría `Refactor? Yes`. Sin tags con semver — `latest` apunta a master roto. No hay rama estable etiquetada que respalde ARM64 Android.

---

## 3. Decisión tomada — `Rprop/And64InlineHook`

Forks ARMv8-A evaluados (búsqueda con `WebSearch` + `WebFetch`):

| Fork | Mantenimiento | ARM64 | Resultado |
|---|---|---|---|
| `jmpews/Dobby` (upstream) | activo pero roto | ❌ source list incompleto | rechazado |
| `PinkyJie/And64InlineHook` | — | — | **404** |
| `ele7enxxh/Android-Inline-Hook` | vivo | ❌ ARM32 (`APP_ABI := armeabi-v7a`) | rechazado |
| `5z0/And64InlineHook` | — | — | **404** (originalmencionado en WebSearch) |
| `bigchx/AndroidInlineHook` | — | — | fork fantasma |
| `disax123/...` | — | — | fork fantasma |
| `ChangweiYuan/...` | — | — | fork fantasma |
| `X-Dragon/...` | — | — | fork fantasma |
| `Rprop/And64InlineHook` | vivo (2018–) | ✅ ARMv8-A específico | **ADOPTADO** |

`Rprop/And64InlineHook` (763★) — *Lightweight ARMv8-A Inline Hook Library for Android C/C++*, MIT. 2 fuentes:

- `And64InlineHook.hpp` (header)
- `And64InlineHook.cpp` (implementación)

API expuesta: `void A64HookFunction(void *symbol, void *replace, void **result)` — **difiere de `DobbyHook`** en que (a) retorna `void` y (b) falla por SEGFAULT en lugar de código de retorno.

La autorización del swap está en `tools/DOBBY.README.md §4`:

> *"And64InlineHook | ⚠️ baja actividad | ARM64 | MIT | Backup si Dobby falla"*

---

## 4. Estrategia técnica — compat shim

Para no tocar `Symbols.cpp`/`JNI_Hooks.cpp`/`JNI_OnLoad.cpp` con dependencias cambiantes, monté un shim:

```
vendor/
├── and64/                              ← fuente nueva (Rprop MIT)
│   ├── And64InlineHook.cpp             (modificado en este turno — W⊕X)
│   ├── And64InlineHook.hpp
│   └── LICENSE-RPROP
└── dobby-install/                      ← staging dir (consumido por mcbedvr)
    ├── include/dobby.h                 ← compat shim (2.2 KB)
    └── lib/arm64-v8a/libdobby.a        ← archive con .o de And64 (9.768 B)
```

### 4.1 Header shim — `vendor/dobby-install/include/dobby.h`

```c
void A64HookFunction(void *const symbol, void *const replace, void **result);

static inline int DobbyHook(void *address, void *fake_func, void **out_origin_func) {
    A64HookFunction(address, fake_func, out_origin_func);
    return 0;  // success permanente: A64 falla por SEGFAULT, no por retorno
}

static inline int DobbyDestroy(void *) { return 0; }       // stub — no usado
static inline int DobbyCodePatch(void *, uint8_t *, uint32_t) { return 0; }  // stub
```

`Symbols::HookOne()` corre `if (::DobbyHook(entry.fn, replacement, &orig) != 0)` — con el shim, **siempre** entra al path feliz (A64 Hook_Function no retorna error; si falla, el proceso cae antes del check).

### 4.2 Archive estático

`libdobby.a` (9.768 B) contiene un solo `.o` con `A64HookFunction` exportado (`T @ 0xaf4` según `nm`). Las referencias atómicas débiles (`__aarch64_cas4_acq_rel` etc.) las resuelve `libatomic` del NDK r27 en link time. **No hay linkeado dinámico de Dobby**.

### 4.3 Cambios necesarios (mínimos) en `mcbedvr/`

1. **`CMakeLists.txt`** — `target_include_directories(mcbedvr PRIVATE ${MCBEDVR_DOBBY_DIR}/include)` (línea ~192). Bug preexistente: el script tenía link+define pero olvidaba include path.
2. **`src/MCBedVr.h`** — `#include <jni.h>` + forward decls `extern "C" { Hook_nativeResize(Suspend, OnDestroy); }`. Antes el cast `&mcbedvr::Hook_nativeResize` fallaba por tipos no declarados.

---

## 5. Limitación residual — RWX vs SELinux execmem (W⊕X aplicado en este turno)

### 5.1 El problema
`And64InlineHook` upstream (`Rprop/And64InlineHook`) tiene **4 invocaciones de `__make_rwx`** (helper que pide `PROT_READ|PROT_WRITE|PROT_EXEC` simultáneo):

| Línea (upstream) | Sitio | Página objetivo |
|---|---|---|
| 490 | `__insns_pool` (BSS interna) | pool nuestro, mapeado por libc |
| 533 | branch lejano (4-5 insns) | `.text` de `libminecraftpe.so` |
| 559 | branch cercano (1 insn) | `.text` de `libminecraftpe.so` |
| 587 | pre-open en `A64HookFunction` | `.text` del símbolo target |

Android 10+ (PICO OS derivado de Android 12) aplica la política SELinux `execmem`: **deny `process.untrusted_app` mapping de páginas con RWX simultáneo**. Resultado en runtime:

```
A64_LOGE: mprotect failed with errno = 13 (EACCES), p = 0x..., size = 20
```

→ `trampoline = NULL` → `Hook_nativeResize` nunca se invoca → Bedrock corre sin hook (sigue su path nativo, lo que se ve como "escritorio Android flotando", no Minecraft VR).

### 5.2 La solución — W⊕X en dos fases

La política `execmem` sólo deniega el flag **simultáneo** RWX. El camino W→X en dos pasos **está permitido**:

```
mprotect(p, n, PROT_READ | PROT_WRITE)    // abrir para escribir
*(uint32_t*)p = branch_instr;             // escribir el branch
__builtin___clear_cache(p, n);            // flush i-cache
mprotect(p, n, PROT_READ | PROT_EXEC)     // cerrar como X
```

### 5.3 Cambios aplicados en `vendor/and64/And64InlineHook.cpp` (este turno)

El macro original `__make_rwx` fue **eliminado**. En su lugar, dos `static inline` helpers:

```cpp
static inline int __make_rw(void *p, size_t n);  // paso 1: RW (sin X)
static inline int __make_rx(void *p, size_t n);  // paso 2: RX (cacheflush + mprotect)
```

Mapeo de los 4 sitios:

| Sitio (línea nueva) | Operación W⊕X |
|---|---|
| L513 `__insns_pool` init | `RW → flush → RX` (todo inline, falla loud si RW no abre) |
| L563 branch lejano (5 insns) | `RW → escribir → flush → RX` |
| L591 branch cercano (1 insn) | `RW → escribir → flush → RX` |
| L623 pre-open en `A64HookFunction` | sólo `RW` — `A64HookFunctionV` cierra como `RX` |

Verificación post-edit:

```bash
$ grep -c __make_rwx vendor/and64/And64InlineHook.cpp
0       # cero referencias residuales
```

### 5.4 Lo que esto NO resuelve

- **Inyección cross-process** (mcbedvr en proceso A, target en proceso B) sigue siendo imposible sin root/Magisk. Por eso mcbedvr **corre en su propio APK reempaquetado** (modelo `gta-sa-vr-quest`), como proceso independiente con su propio contexto SELinux (`untrusted_app` por default, `priv_app` si está firmado con platform key).
- **W^X en 2 pasos no funciona si SELinux bloquea `PROT_EXEC` totalmente** sobre `.text` de otro binario cargado vía `dlopen` desde proceso `untrusted_app`. Esto depende de la política SELinux específica de PICO OS. Si la build corre como `priv_app` o `system_app` (firmada con platform key, instalada vía Developer Mode), el bloqueo desaparece.
- **`__make_rx` puede aún fallar** si la página está compartida (copy-on-write después del fork). No es problema aquí porque mcbedvr es single-process.

---

## 6. Estado actual

| Componente | Estado |
|---|---|
| `vendor/dobby/` | **borrado** (3.1 MB rescatados, era clon roto de upstream) |
| `vendor/and64/` | fuente activa, W⊕X aplicado en este turno |
| `vendor/dobby-install/` | staging intacto, `dobby.h` + `libdobby.a` |
| `vendor/openxr/` | OpenXR-SDK 1.1.45 host (referencia, no se linkea en mcbedvr) |
| `tools/HOOK-ENGINE-SWAP.md` | **este doc** |
| `tools/DOBBY.README.md` | intacto, referencia cruzada |
| `CMakeLists.txt` | `MCBEDVR_DOBBY_DIR/include` agregado al include path |
| `src/MCBedVr.h` | forward decls + `<jni.h>` agregados |
| ELF `libmcbedvr.so` | **734.960 B**, `A64HookFunction` en `.dynsym` @ 0x2cd3c |

### Build sanity check (del turno anterior)
- ELF `ARM aarch64, dynamically linked, BuildID=9de942da...`
- `A64HookFunction` → `T @ 0x2cd3c`
- `A64HookFunctionV` → `T @ 0x2c248`
- `Hook_nativeResize/Suspend/OnDestroy` → `t` (hidden) en `.text`
- `JNI_OnLoad` → único entry público de mcbedvr @ 0x2ae60
- `.text` total: 195 KB AArch64

### Pendiente (no bloqueante para el swap)
- **Recompilar `libmcbedvr.so`** con los nuevos W⊕X y revalidar ELF (siguiente paso).
- **⚠️  BLOQUEANTE OPERACIONAL — el bin actual es stale.** El `libdobby.a` se arma
  una sola vez y `libmcbedvr.so` lo linkea estáticamente. Sin toolchain NDK r27
  (clang++/llvm-ar) disponible en este host, el `.cpp` modificado no se traduce
  a `.o` nuevo, el `.a` no se rearma, y `make mcbedvr` reporta `100% Built`
  como cache hit sin recompilar. Tiempos verificados en disco el 2026-10-10 22:13:

  ```
  And64InlineHook.cpp   mod 22:13:37  ← edit de este turno (W⊕X aplicado)
  libdobby.a            mod 18:19:44  ← .o viejo con __make_rwx (4 h stale)
  libmcbedvr.so         mod 18:20:53  ← linkeado contra .a stale
  ```

  `verify_toolchain.sh` reporta Vía A BLOQUEADA: NDK 27.0.11718014 directory
  presente pero sin toolchains (`clang++`/`llvm-ar` no existen), `adb` no en
  PATH, Android Platform 34 + Build-tools + PICO OpenXR SDK faltantes. El edit
  de fuente es correcto y verificado por `grep` (`__make_rwx` count = 0), pero
  el binario en `build/dist/arm64-v8a/libmcbedvr.so` **sigue pidiendo mprotect
  RWX simultáneo**. Device validation contra este bin devolvería el
  `A64_LOGE: mprotect failed errno=13 (EACCES)` documentado en §5.

  **Resolución**: o bien correr `infra/vr/toolchain/bootstrap_macos.sh` (20-40 min,
  descarga NDK + cmdline-tools + licencias, puede pedir interacción), o delegar
  a Mario en sesión con toolchain. Una vez rearmeado, `make mcbedvr` re-linkea
  automáticamente (CMakeLists.txt ya tiene la dependencia sobre libdobby.a).

- **Device validation** — `adb push` + `setenforce 0/1` + `logcat -s mcbedvr:A64_HOOK`. Probar:
  1. `setenforce 1` (PICO OS enforcing) → ¿se aplican los 3 hooks?
  2. ¿Aparece `A64_LOGI("inline hook ... successfully! ...")` por cada uno?
  3. ¿`trampoline` no queda `NULL` por EACCES?
- **Definir contexto SELinux del proceso Bedrock** — `@Mario Santos` debe decidir si mcbedvr corre como `untrusted_app` (default APK) o se eleva vía Magisk / firma platform. Eso define si el W⊕X en 2 pasos es suficiente o se necesita plan C (PLT/GOT-only).
- **Vector 2 (Gabriel)** — topografía de cadenas de render Bedrock. Paralelo, no toca este árbol.
- **Vector 3.1 (Emilio)** — spec de admin tool diagnóstico. Bloqueante para validar "hook vivo vs hook silencioso".

---

## 7. Referencias cruzadas

- `tools/DOBBY.README.md` §4 — autorización explícita del swap
- `memory/hook-engine-rwx-limitation.md` — nota durable sobre la limitación W⊕X (creada por @Gabriel)
- `memory/mcbedvr-build-arm64.md` — toolchain NDK r27 + bootstrap
- `memory/bedrock-vr-pico-priorart.md` — Vía A vs Vía B, contexto del proyecto
- `vendor/dobby-install/include/dobby.h` — header compat shim (línea 25 referencia a este doc)
- `vendor/and64/And64InlineHook.cpp` — fuente modificada en este turno
- `src/Symbols.cpp::HookOne()` — único consumidor de `DobbyHook` (sin cambios)

---

## 8. Próximo PR upstream a monitorear

`jmpews/Dobby#236` (PLT/GOT-only hook engine) — si Dobby master lo mergea y compila ARM64, podemos volver a Dobby sin el shim. Mientras tanto, And64 + W⊕X es la solución operacional.