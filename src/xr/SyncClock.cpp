// SyncClock.cpp — Implementación del reloj de sincronización de frames.
//
// Fase 2.B (contrato lockado turno 12). Sin mutex en hot path: los
// contadores y timestamps son atomics. SyncClockStats() toma un mutex
// sólo para garantizar consistencia entre los 8 campos del snapshot.
//
// Fase 2.B NO conecta a Swappy tracer — los stubs PreSwapCb/PostSwapCb
// en SwappyBridge.cpp siguen siendo logs. La conexión se hace en Fase 3
// reemplazando los stubs por llamadas a SyncClockOnFrameBegin/End.

#include "xr/SyncClock.h"
#include "xr/SwappyBridge.h"
#include "Log.h"

#include <atomic>
#include <chrono>
#include <mutex>

namespace mcbedvr::xr {
namespace {

// ─── Estado global ──────────────────────────────────────────────────────

std::atomic<bool>          g_inited{false};
std::atomic<ClockSource>   g_source{ClockSource::ChoreographerOnly};

// Hot path: contadores y timestamps en atomics. Lock-free reads/writes.
std::atomic<std::uint64_t> g_choreo_count{0};
std::atomic<std::uint64_t> g_frame_begin_count{0};
std::atomic<std::uint64_t> g_frame_end_count{0};
std::atomic<std::int64_t>  g_last_choreo_ns{0};
std::atomic<std::int64_t>  g_last_fb_ns{0};
std::atomic<std::int64_t>  g_last_fe_ns{0};
std::atomic<std::int64_t>  g_predicted_display_ns{0};
std::atomic<std::int64_t>  g_predicted_period_ns{0};

// frame_id se incrementa en cada FrameBegin. En Fase 2 los stubs de
// SwappyBridge.cpp todavía no llaman a SyncClockOnFrameBegin, así que
// frame_id queda en 0 hasta Fase 3.
std::atomic<std::uint64_t> g_frame_id{0};

// Lock para SyncClockStats() consistency (no para hot path).
std::mutex                  g_stats_mtx;

}  // namespace

const char* ClockSourceName(ClockSource s) {
    switch (s) {
        case ClockSource::ChoreographerOnly:        return "ChoreographerOnly";
        case ClockSource::ChoreographerPlusTracer:  return "ChoreographerPlusTracer";
        case ClockSource::TracerOnly:               return "TracerOnly";
    }
    return "?";
}

bool SyncClockInit() {
    bool expected = false;
    if (!g_inited.compare_exchange_strong(expected, true)) {
        LOG_TICKW("SyncClockInit re-llamado — noop");
        return true;
    }
    // Mapeo SwappyBridgeStatus → ClockSource (lockado turno 12 §Compatibilidad).
    auto sw = SwappyBridgeStatus_();
    ClockSource src = (sw == SwappyBridgeStatus::TracerActive)
                          ? ClockSource::ChoreographerPlusTracer
                          : ClockSource::ChoreographerOnly;
    g_source.store(src);
    LOG_TICK("SyncClockInit OK — source=%s (Swappy=%s)",
             ClockSourceName(src), StatusName(sw));
    return true;
}

void SyncClockShutdown() {
    if (!g_inited.load()) return;
    g_inited.store(false);
    g_source.store(ClockSource::ChoreographerOnly);
    LOG_TICK("SyncClockShutdown OK (counts: choreo=%llu fb=%llu fe=%llu frame_id=%llu)",
             (unsigned long long)g_choreo_count.load(),
             (unsigned long long)g_frame_begin_count.load(),
             (unsigned long long)g_frame_end_count.load(),
             (unsigned long long)g_frame_id.load());
}

ClockSource SyncClockSource_() { return g_source.load(); }

void SyncClockOnChoreographerTick(std::int64_t frame_time_nanos,
                                  double /*dt_seconds*/) {
    if (!g_inited.load(std::memory_order_acquire)) return;
    g_choreo_count.fetch_add(1, std::memory_order_relaxed);
    g_last_choreo_ns.store(frame_time_nanos, std::memory_order_relaxed);
}

void SyncClockOnFrameBegin(int /*fd*/) {
    if (!g_inited.load(std::memory_order_acquire)) return;
    g_frame_begin_count.fetch_add(1, std::memory_order_relaxed);
    g_frame_id.fetch_add(1, std::memory_order_relaxed);
    auto now = std::chrono::steady_clock::now().time_since_epoch().count();
    g_last_fb_ns.store(static_cast<std::int64_t>(now), std::memory_order_relaxed);
}

void SyncClockOnFrameEnd(long /*timeout_ns*/) {
    if (!g_inited.load(std::memory_order_acquire)) return;
    g_frame_end_count.fetch_add(1, std::memory_order_relaxed);
    auto now = std::chrono::steady_clock::now().time_since_epoch().count();
    g_last_fe_ns.store(static_cast<std::int64_t>(now), std::memory_order_relaxed);
}

std::uint64_t SyncClockFrameId() { return g_frame_id.load(std::memory_order_relaxed); }

ClockStats SyncClockStats() {
    // Lock para garantizar consistencia entre los 8 campos.
    std::lock_guard<std::mutex> lk(g_stats_mtx);
    ClockStats s{};
    s.choreo_tick_count = g_choreo_count.load(std::memory_order_relaxed);
    s.frame_begin_count = g_frame_begin_count.load(std::memory_order_relaxed);
    s.frame_end_count   = g_frame_end_count.load(std::memory_order_relaxed);
    s.last_choreo_time_ns  = g_last_choreo_ns.load(std::memory_order_relaxed);
    s.last_frame_begin_ns  = g_last_fb_ns.load(std::memory_order_relaxed);
    s.last_frame_end_ns    = g_last_fe_ns.load(std::memory_order_relaxed);
    s.predicted_display_time_ns   = g_predicted_display_ns.load(std::memory_order_relaxed);
    s.predicted_display_period_ns = g_predicted_period_ns.load(std::memory_order_relaxed);
    return s;
}

void SyncClockDumpDiagnostics() {
    LOG_TICK("==== SyncClock diagnostics ====");
    LOG_TICK("  inited        : %s", g_inited.load() ? "yes" : "no");
    LOG_TICK("  source        : %s", ClockSourceName(g_source.load()));
    auto s = SyncClockStats();
    LOG_TICK("  choreo ticks  : %llu (last=%lld ns)",
             (unsigned long long)s.choreo_tick_count,
             (long long)s.last_choreo_time_ns);
    LOG_TICK("  frame begins  : %llu (last=%lld ns)",
             (unsigned long long)s.frame_begin_count,
             (long long)s.last_frame_begin_ns);
    LOG_TICK("  frame ends    : %llu (last=%lld ns)",
             (unsigned long long)s.frame_end_count,
             (long long)s.last_frame_end_ns);
    LOG_TICK("  frame_id      : %llu",
             (unsigned long long)SyncClockFrameId());
    LOG_TICK("================================");
}

}  // namespace mcbedvr::xr
