// SyncClock.h — Estado timing liviano: cadencia Choreographer↔GameThread.
//
// Fase 2.B (contrato lockado turno 12, ver memory/decomp-ports-vr.md §"Subfases
// Fase 2"). El reloj se alimenta de dos fuentes:
//
//   (1) JNI Choreographer tick (frameTimeNanos, dt) — vía Bedrock_nOnChoreographer
//       registrada en JNI_Hooks.cpp. Ticks a vsync (60-90 Hz en PICO 4 Ultra).
//
//   (2) Swappy tracer preSwap/postSwap — vía SwappyBridge.cpp en Fase 3.
//       Fase 2.B ya define los entry points SyncClockOnFrameBegin/End pero
//       NO se conectan al tracer (SwappyBridge.cpp sigue con stubs en Fase 2
//       según lockado §"Compatibilidad firmas").
//
// Estado: `ClockSource` mapea 1-a-1 desde SwappyBridgeStatus:
//   - Swappy TracerActive     → ChoreographerPlusTracer
//   - Swappy FallbackChoreographerOnly / Failed / NotInitialized → ChoreographerOnly
//
// Threading: todas las operaciones son lock-free (atomics). Los getters
// retornan snapshots no-atómicos entre sí; el caller que necesite
// consistencia (p.ej. `SyncClockStats()` para diagnóstico) lee bajo el
// lock interno en SyncClockStats().

#pragma once

#include <cstdint>

namespace mcbedvr::xr {

enum class ClockSource : std::uint8_t {
    ChoreographerOnly       = 0,  // F1 fallback (sin tracer Swappy)
    ChoreographerPlusTracer = 1,  // tracer Swappy + Choreographer tick (Fase 3)
    TracerOnly              = 2,  // (placeholder Fase 3, no usado en Fase 2)
};

const char* ClockSourceName(ClockSource s);

// Snapshot read-only. NO atómico entre campos — sólo SyncClockStats() toma
// el lock interno para garantizar consistencia.
struct ClockStats {
    std::uint64_t choreo_tick_count{0};
    std::uint64_t frame_begin_count{0};
    std::uint64_t frame_end_count{0};
    std::int64_t  last_choreo_time_ns{0};
    std::int64_t  last_frame_begin_ns{0};
    std::int64_t  last_frame_end_ns{0};
    std::int64_t  predicted_display_time_ns{0};
    std::int64_t  predicted_display_period_ns{0};
};

// ─── Lifecycle ──────────────────────────────────────────────────────────

// Inicializa SyncClock. Idempotente. Mapea SwappyBridgeStatus → ClockSource.
// Retorna true si la inicialización procedió (sea cual sea el source resultante).
bool   SyncClockInit();

// Cierra SyncClock. Loggea counts acumulados. Idempotente.
void   SyncClockShutdown();

// ─── Source ─────────────────────────────────────────────────────────────

ClockSource SyncClockSource_();

// ─── Tick sources ───────────────────────────────────────────────────────

// Conectado a Bedrock_nOnChoreographer (Fase 2.C). Incrementa contador y
// guarda timestamp del último tick. Lock-free.
void   SyncClockOnChoreographerTick(std::int64_t frame_time_nanos,
                                    double dt_seconds);

// Conectado a SwappyTracerPreSwapCallback(void*, int) — Fase 3. En Fase 2
// los stubs de SwappyBridge.cpp NO llaman a esta función. Safe-no-op si
// SyncClock no está inited.
void   SyncClockOnFrameBegin(int fd);

// Conectado a SwappyPostSwapCallback(void*, long) — Fase 3. Idem.
void   SyncClockOnFrameEnd(long timeout_ns);

// ─── Getters ────────────────────────────────────────────────────────────

// Snapshot consistente bajo lock interno. Para hot-path Emilio prefiere
// SyncClockFrameId() (lock-free).
ClockStats      SyncClockStats();
std::uint64_t   SyncClockFrameId();

// ─── Diagnostics ────────────────────────────────────────────────────────

void SyncClockDumpDiagnostics();

}  // namespace mcbedvr::xr
