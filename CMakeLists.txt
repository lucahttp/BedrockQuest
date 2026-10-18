# mcbedvr — Inyector C++ OpenXR sobre Minecraft Bedrock 1.21+ para PICO 4 Ultra.
# Patrón arquitectónico derivado de gta-sa-vr-quest (Dubrovskiy) — adaptado a Bedrock.
# Build target: Android ARM64 (arm64-v8a) usando NDK r27 + Khronos OpenXR 1.x headers
# + OpenXR loader de Khronos. El runtime OpenXR de PICO se inyecta via dlopen del
# /system/lib64 del visor, NO se enlaza estáticamente.

cmake_minimum_required(VERSION 3.22)
project(mcbedvr
    VERSION 0.1.0
    DESCRIPTION "Minecraft Bedrock 1.21+ standalone VR injector for PICO 4 Ultra"
    LANGUAGES CXX
)

# ---------------------------------------------------------------------------
# Toolchain Android
# ---------------------------------------------------------------------------
# Se asume invocación con -DCMAKE_TOOLCHAIN_FILE=$ANDROID_NDK/build/cmake/android.toolchain.cmake
# desde scripts/build_android_arm64.sh. Si no está seteado, intentamos apuntar al NDK del SDK
# del X10Pro; si eso también falta, fallamos rápido — Fase 1 no admite compilación host.
if(NOT DEFINED CMAKE_SYSTEM_NAME OR NOT CMAKE_SYSTEM_NAME STREQUAL "Android")
    if(DEFINED ENV{ANDROID_NDK_HOME})
        set(ANDROID_NDK "$ENV{ANDROID_NDK_HOME}")
    elseif(EXISTS "/Volumes/X10Pro/Android/sdk/ndk/27.0.11718014")
        set(ANDROID_NDK "/Volumes/X10Pro/Android/sdk/ndk/27.0.11718014")
    else()
        message(FATAL_ERROR
            "mcbedvr requiere toolchain Android. Exportá ANDROID_NDK_HOME o invocá "
            "scripts/build_android_arm64.sh (que lo setea solo).")
    endif()
    set(CMAKE_SYSTEM_NAME Android)
    set(CMAKE_ANDROID_NDK "${ANDROID_NDK}")
    set(CMAKE_ANDROID_ARCH_ABI "arm64-v8a")
    set(CMAKE_ANDROID_API 29) # PICO 4 Ultra = API 31; Bedrock 1.21 corre con API 26+
endif()

# Mantener toolchain para diagnóstico.
message(STATUS "mcbedvr: NDK           = ${CMAKE_ANDROID_NDK}")
# FORCE ABI a arm64-v8a: la toolchain del NDK r27 con cmake 3.22.1 cae a
# armeabi-v7a si el script pasa -DCMAKE_ANDROID_ARCH_ABI antes de tiempo;
# hacemos cache FORCE después del project() para garantizar aarch64.
set(CMAKE_ANDROID_ARCH_ABI "arm64-v8a" CACHE STRING "Target ABI" FORCE)
set(CMAKE_SYSTEM_NAME "Android" CACHE STRING "" FORCE)
message(STATUS "mcbedvr: ABI           = ${CMAKE_ANDROID_ARCH_ABI}")
message(STATUS "mcbedvr: API           = ${CMAKE_ANDROID_API}")

# ---------------------------------------------------------------------------
# OpenXR — headers Khronos, loader binario por variable de cache
# ---------------------------------------------------------------------------
# Aceptamos cualquiera de:
#   - Khronos OpenXR-SDK clonado en local (RECOMENDADO para reproducibilidad)
#   - PICO OpenXR Mobile SDK descomprimido en local (provee headers equivalentes)
# Para el loader binario (libopenxr_loader.so) se pasa MCBEDVR_OPENXR_LOADER_SO
# con la ruta al AAR descomprimido. El runtime final en el visor NO es este loader;
# mcbedvr dlopen()ea /system/lib64/libopenxr.so de PICO OS en runtime.

set(MCBEDVR_OPENXR_DIR "" CACHE PATH
    "Directorio raíz con include/openxr/openxr.h. Esperado: Khronos OpenXR-SDK 1.1.x o PICO OpenXR Mobile SDK v1.4.0+.")
if(NOT MCBEDVR_OPENXR_DIR AND DEFINED ENV{MCBEDVR_OPENXR_DIR})
    set(MCBEDVR_OPENXR_DIR "$ENV{MCBEDVR_OPENXR_DIR}")
endif()
if(NOT MCBEDVR_OPENXR_DIR)
    set(MCBEDVR_OPENXR_DIR "${CMAKE_CURRENT_SOURCE_DIR}/vendor/openxr")
endif()
if(NOT EXISTS "${MCBEDVR_OPENXR_DIR}/include/openxr/openxr.h")
    message(FATAL_ERROR
        "mcbedvr: no se encontró include/openxr/openxr.h en '${MCBEDVR_OPENXR_DIR}'. "
        "Cloná KhronosGroup/OpenXR-SDK@1.1.45 y pasá -DMCBEDVR_OPENXR_DIR=<path>, "
        "o exportá MCBEDVR_OPENXR_DIR. Ver README-BUILD.md §1.")
endif()

# Headers OpenXR siempre disponibles en el momento de compilación. No necesitamos
# el loader de Khronos enlazado en este target — el runtime será dlopen() del
# runtime de PICO. Por eso NO agregamos una biblioteca IMPORTED para el loader.

# ---------------------------------------------------------------------------
# Dobby — hook engine
# ---------------------------------------------------------------------------
# Dobby es GPL-3.0 / dynamic linking friendly. mcbedvr NO se redistribuye; el
# usuario compila Dobby del source contra el NDK r27 y pasa MCBEDVR_DOBBY_DIR.
# Degradamos a "stub" si no está (Fase 1 todavía no hookea; emite warnings).

set(MCBEDVR_DOBBY_DIR "" CACHE PATH
    "Directorio raíz de Dobby con include/dobby.h y lib/libdobby.a|libdobby.so. Ver tools/DOBBY.README.md para build script.")
if(NOT MCBEDVR_DOBBY_DIR AND DEFINED ENV{MCBEDVR_DOBBY_DIR})
    set(MCBEDVR_DOBBY_DIR "$ENV{MCBEDVR_DOBBY_DIR}")
endif()

set(MCBEDVR_HAS_DOBBY FALSE)
if(MCBEDVR_DOBBY_DIR AND EXISTS "${MCBEDVR_DOBBY_DIR}/include/dobby.h")
    # Buscar el artefacto; preferimos .a estático para que libmcbedvr.so sea self-contained.
    if(EXISTS "${MCBEDVR_DOBBY_DIR}/lib/arm64-v8a/libdobby.a")
        set(MCBEDVR_DOBBY_LIB "${MCBEDVR_DOBBY_DIR}/lib/arm64-v8a/libdobby.a")
        set(MCBEDVR_HAS_DOBBY TRUE)
    elseif(EXISTS "${MCBEDVR_DOBBY_DIR}/lib/arm64-v8a/libdobby.so")
        set(MCBEDVR_DOBBY_LIB "${MCBEDVR_DOBBY_DIR}/lib/arm64-v8a/libdobby.so")
        set(MCBEDVR_HAS_DOBBY TRUE)
    else()
        message(WARNING
            "mcbedvr: MCBEDVR_DOBBY_DIR apunta a '${MCBEDVR_DOBBY_DIR}', "
            "pero no se encuentra lib/arm64-v8a/libdobby.{a,so}. "
            "mcbedvr se compila sin Dobby (Fase 1, stubs). "
            "Ver tools/DOBBY.README.md.")
    endif()
else()
    message(STATUS
        "mcbedvr: MCBEDVR_DOBBY_DIR no definido o sin include/dobby.h. "
        "Compilando stubs de Fase 1 (sin hook engine). "
        "Ver tools/DOBBY.README.md para build script.")
endif()

if(MCBEDVR_HAS_DOBBY)
    message(STATUS "mcbedvr: Dobby        = ${MCBEDVR_DOBBY_LIB}")
endif()

# ---------------------------------------------------------------------------
# Source set
# ---------------------------------------------------------------------------
set(MCBEDVR_SOURCES
    src/JNI_OnLoad.cpp
    src/JNI_Hooks.cpp
    src/Xr.cpp
    src/Symbols.cpp
    src/xr/SwappyBridge.cpp
    src/xr/XrContext.cpp     # Fase 2.A: estado OpenXR pesado
    src/xr/SyncClock.cpp     # Fase 2.B: cadencia Choreographer↔GameThread
    src/xr/BlitEye.cpp       # Framebuffer blit to OpenXR swapchain
    src/xr/PicoInput.cpp     # Task T08: PICO 4 Ultra 6DoF controller input
    src/xr/InputBridge.cpp   # GameActivity & Bedrock Android input bridge
)

# Headers públicos (para IDE + consumidores externos que importen libmcbedvr).
set(MCBEDVR_HEADERS
    src/MCBedVr.h
    src/Log.h
    src/Xr.h
    src/Symbols.h
    src/xr/SwappyBridge.h
    src/xr/XrContext.h       # Fase 2.A
    src/xr/SyncClock.h       # Fase 2.B
    src/xr/BlitEye.h
    src/xr/PicoInput.h
    src/xr/InputBridge.h
)

add_library(mcbedvr SHARED ${MCBEDVR_SOURCES} ${MCBEDVR_HEADERS})

# ---------------------------------------------------------------------------
# Includes
# ---------------------------------------------------------------------------
target_include_directories(mcbedvr
    PRIVATE
        src
        ${MCBEDVR_OPENXR_DIR}/include
)

# ---------------------------------------------------------------------------
# Compilación
# ---------------------------------------------------------------------------
target_compile_features(mcbedvr PRIVATE cxx_std_20)
set_target_properties(mcbedvr PROPERTIES
    CXX_STANDARD 20
    CXX_STANDARD_REQUIRED ON
    CXX_EXTENSIONS OFF
    POSITION_INDEPENDENT_CODE ON
)

# -fvisibility=hidden: la única entry pública es JNI_OnLoad; nada más debe
# filtrarse y poder colisionar con símbolos del propio Bedrock en runtime.
target_compile_options(mcbedvr PRIVATE
    -Wall -Wextra
    -Wno-unused-parameter
    -Wno-missing-field-initializers
    -fvisibility=hidden
    $<$<CONFIG:Release>:-O2>
    $<$<CONFIG:Debug>:-O0 -g>
)

# Link con OpenXR loader y Bionic libs básicas.
add_library(openxr_loader SHARED IMPORTED)
set_target_properties(openxr_loader PROPERTIES
    IMPORTED_LOCATION "${MCBEDVR_OPENXR_DIR}/lib/${CMAKE_ANDROID_ARCH_ABI}/libopenxr_loader.so"
)

find_library(log-lib     log)
find_library(android-lib android)
find_library(dl-lib      dl)
find_library(egl-lib     EGL)
find_library(gles-lib    GLESv3)

target_link_libraries(mcbedvr PRIVATE
    openxr_loader
    ${log-lib}
    ${android-lib}
    ${dl-lib}
    ${egl-lib}
    ${gles-lib}
)


if(MCBEDVR_HAS_DOBBY)
    # Dobby ARM64 hace trampas con RWX pages que PICO OS rechaza vía SELinux.
    # El parche necesario está en tools/DOBBY.README.md §3.
    # SWAP 2026-10-10: la impl real aquí es Rprop/And64InlineHook (AArch64).
    # Ver tools/HOOK-ENGINE-SWAP.md para la justificación.
    target_compile_definitions(mcbedvr PRIVATE MCBEDVR_HAS_DOBBY=1)
    target_include_directories(mcbedvr PRIVATE "${MCBEDVR_DOBBY_DIR}/include")
    target_link_libraries(mcbedvr PRIVATE "${MCBEDVR_DOBBY_LIB}")
endif()

# ---------------------------------------------------------------------------
# Output
# ---------------------------------------------------------------------------
# CMake normalmente pone la .so en ${CMAKE_BINARY_DIR}/libmcbedvr.so. Lo
# retrasladamos a dist/<abi>/ para que el siguiente paso (strip + zipalign)
# tenga una ruta estable.
set_target_properties(mcbedvr PROPERTIES
    LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/dist/${CMAKE_ANDROID_ARCH_ABI}"
)

# ---------------------------------------------------------------------------
# Install
# ---------------------------------------------------------------------------
install(TARGETS mcbedvr
    ARCHIVE DESTINATION lib
    LIBRARY DESTINATION lib
    RUNTIME DESTINATION bin
)

# ---------------------------------------------------------------------------
# Resumen post-config
# ---------------------------------------------------------------------------
message(STATUS "mcbedvr ${PROJECT_VERSION} configured:")
message(STATUS "  sources  : ${MCBEDVR_SOURCES}")
message(STATUS "  openxr   : ${MCBEDVR_OPENXR_DIR}")
if(MCBEDVR_HAS_DOBBY)
    message(STATUS "  dobby    : ${MCBEDVR_DOBBY_LIB}")
else()
    message(STATUS "  dobby    : <stub>")
endif()
message(STATUS "  output   : dist/${CMAKE_ANDROID_ARCH_ABI}/libmcbedvr.so")
