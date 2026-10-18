#pragma once
#include <cstdint>
#include <openxr/openxr.h>

namespace mcbedvr::xr {

struct ControllerState {
    float thumbstick_x;
    float thumbstick_y;
    bool  thumbstick_click;
    float trigger;
    bool  trigger_click;
    float grip;
    bool  grip_click;
    bool  button_primary;   // A (right) / X (left)
    bool  button_secondary; // B (right) / Y (left)
    bool  menu_click;
    XrPosef pose;
    bool  pose_valid;
};

struct PicoInputSnapshot {
    ControllerState left;
    ControllerState right;
};

struct LaserHit {
    bool  is_hit{false};
    float u{0.0f};       // 0..1 horizontal on cinema screen
    float v{0.0f};       // 0..1 vertical on cinema screen
    float pixel_x{0.0f}; // 0..windowWidth
    float pixel_y{0.0f}; // 0..windowHeight
    bool  clicking{false};
};

class PicoInput {
public:
    static bool Init(XrInstance instance, XrSession session);
    static void Shutdown();
    static void SyncActions(XrSession session, XrSpace appSpace, XrTime predictedDisplayTime,
                            float headYawRate = 0.0f, float headPitchRate = 0.0f);
    static const PicoInputSnapshot& GetSnapshot();
    static const LaserHit& GetLaserHit();
    static void TriggerHaptic(uint32_t hand, float durationSeconds, float frequency, float amplitude);
};

}  // namespace mcbedvr::xr
