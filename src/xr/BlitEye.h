#pragma once
#include <cstdint>
#include <GLES3/gl3.h>
#include <openxr/openxr.h>

namespace mcbedvr::xr {

class BlitEye {
public:
    static bool Init();
    static void Shutdown();
    static void SetWindowSize(int w, int h);
    static int  GetWindowWidth();
    static int  GetWindowHeight();
    static void Blit(GLuint swapchainArrayTex, int dstW, int dstH);
    static void BlitStereo(GLuint leftTex, GLuint rightTex, int dstW, int dstH, const XrView* views = nullptr);
    static void DrawReticle(float u, float v, bool clicking, int dstW, int dstH);
    static void DrawVoxelArm(const XrPosef& handPose, const XrView& eyeView, int dstW, int dstH, bool isLeftArm = false);
};

}  // namespace mcbedvr::xr
