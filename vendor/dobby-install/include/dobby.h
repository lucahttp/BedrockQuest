// dobby.h — Compat shim que mcbedvr consume vía #include <dobby.h>.
//
// Implementación subyacente: Rprop/And64InlineHook (ARM64/AArch64 inline
// hooks, MIT license, 763★ GitHub). La API nativa expuesta es:
//
//     void A64HookFunction(void *const symbol, void *const replace, void **result);
//
// Dobby expone:
//
//     int  DobbyHook(void *address, void *fake_func, void **out_origin_func);
//     int  DobbyDestroy(void *address);
//
// mcbedvr sólo invoca DobbyHook en Symbols.cpp::HookOne() y descarta el
// int retornado (la rama de error queda como guard pero A64HookFunction
// falla por SEGFAULT en lugar de código de retorno, por lo que el path
// de éxito es siempre 0). DobbyDestroy y los otros hooks de Dobby que
// expone el header original NO son usados por mcbedvr a día de hoy —
// se declaran como stubs para que el header siga compilando si alguna
// tool externa los pide.
//
// SWAP del 2026-10-10: Dobby master (jmpews/Dobby@5dfc854) sólo incluye
// assembler-arm.cc/ia32.cc/x64.cc — NO hay assembler-arm64.cc en source
// list, master está roto para ARM64. And64InlineHook es la solución
// específica para ARM64 Android documentada en tools/DOBBY.README.md §4
// como "Backup si Dobby falla". Ver tools/HOOK-ENGINE-SWAP.md.

#pragma once

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

// API real.
void A64HookFunction(void *const symbol, void *const replace, void **result);

// Adapt a la firma de Dobby. La ausencia de error code se modela como
// éxito permanente: A64HookFunction sólo falla por SEGFAULT, no por
// retorno. Los call-sites que hacen `if (DobbyHook(...) != 0)` siguen
// tomando siempre la rama `== 0` (success).
static inline int DobbyHook(void *address, void *fake_func, void **out_origin_func) {
    A64HookFunction(address, fake_func, out_origin_func);
    return 0;
}

// API mínima que el header original de Dobby expone pero mcbedvr no usa.
// Mantenidas como stubs vacíos (linker resolverá con símbolos débiles).
static inline int DobbyDestroy(void * /*address*/) { return 0; }
static inline int DobbyCodePatch(void * /*address*/, uint8_t * /*buffer*/, uint32_t /*buffer_size*/) { return 0; }

#ifdef __cplusplus
}
#endif
