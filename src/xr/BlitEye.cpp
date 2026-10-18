#include "xr/BlitEye.h"
#include "xr/PicoInput.h"
#include "xr/XrContext.h"
#include "Log.h"
#include <cstdio>
#include <cmath>
#include <cstring>
#include <vector>

namespace mcbedvr::xr {
namespace {

GLuint g_blitFbo{0};

int s_windowWidth = 4320;
int s_windowHeight = 2160;

GLuint s_reticleProgram = 0;
GLuint s_reticleVao = 0;
GLuint s_reticleVbo = 0;
GLint  s_uCenterLoc = -1;
GLint  s_uAspectLoc = -1;
GLint  s_uColorLoc = -1;

GLuint s_armProgram = 0;
GLuint s_armVaoRight = 0;
GLuint s_armVboRight = 0;
GLsizei s_rightArmVertexCount = 0;
GLuint s_armVaoLeft = 0;
GLuint s_armVboLeft = 0;
GLsizei s_leftArmVertexCount = 0;
GLint  s_uArmMVPLoc = -1;

GLuint g_depthRb = 0;
int g_depthRbW = 0;
int g_depthRbH = 0;

struct Mat4 {
    float m[16]; // column-major

    static Mat4 Identity() {
        Mat4 r{};
        r.m[0] = 1.0f; r.m[5] = 1.0f; r.m[10] = 1.0f; r.m[15] = 1.0f;
        return r;
    }

    static Mat4 Multiply(const Mat4& a, const Mat4& b) {
        Mat4 r{};
        for (int c = 0; c < 4; c++) {
            for (int row = 0; row < 4; row++) {
                r.m[c * 4 + row] =
                    a.m[0 * 4 + row] * b.m[c * 4 + 0] +
                    a.m[1 * 4 + row] * b.m[c * 4 + 1] +
                    a.m[2 * 4 + row] * b.m[c * 4 + 2] +
                    a.m[3 * 4 + row] * b.m[c * 4 + 3];
            }
        }
        return r;
    }

    static Mat4 ProjectionFromFov(const XrFovf& fov, float nearZ = 0.05f, float farZ = 50.0f) {
        float tanLeft = std::tan(fov.angleLeft);
        float tanRight = std::tan(fov.angleRight);
        float tanDown = std::tan(fov.angleDown);
        float tanUp = std::tan(fov.angleUp);

        float width = tanRight - tanLeft;
        float height = tanUp - tanDown;

        Mat4 r{};
        r.m[0] = 2.0f / width;
        r.m[5] = 2.0f / height;
        r.m[8] = (tanRight + tanLeft) / width;
        r.m[9] = (tanUp + tanDown) / height;
        r.m[10] = -(farZ + nearZ) / (farZ - nearZ);
        r.m[11] = -1.0f;
        r.m[14] = -(2.0f * farZ * nearZ) / (farZ - nearZ);
        return r;
    }

    static Mat4 FromPose(const XrPosef& pose) {
        float qx = pose.orientation.x;
        float qy = pose.orientation.y;
        float qz = pose.orientation.z;
        float qw = pose.orientation.w;

        Mat4 r = Identity();
        r.m[0] = 1.0f - 2.0f * (qy * qy + qz * qz);
        r.m[1] = 2.0f * (qx * qy + qz * qw);
        r.m[2] = 2.0f * (qx * qz - qy * qw);

        r.m[4] = 2.0f * (qx * qy - qz * qw);
        r.m[5] = 1.0f - 2.0f * (qx * qx + qz * qz);
        r.m[6] = 2.0f * (qy * qz + qx * qw);

        r.m[8] = 2.0f * (qx * qz + qy * qw);
        r.m[9] = 2.0f * (qy * qz - qx * qw);
        r.m[10] = 1.0f - 2.0f * (qx * qx + qy * qy);

        r.m[12] = pose.position.x;
        r.m[13] = pose.position.y;
        r.m[14] = pose.position.z;
        r.m[15] = 1.0f;
        return r;
    }

    static Mat4 ViewFromEyePose(const XrPosef& eyePose) {
        float qx = eyePose.orientation.x;
        float qy = eyePose.orientation.y;
        float qz = eyePose.orientation.z;
        float qw = eyePose.orientation.w;

        Mat4 r = Identity();
        r.m[0] = 1.0f - 2.0f * (qy * qy + qz * qz);
        r.m[4] = 2.0f * (qx * qy + qz * qw);
        r.m[8] = 2.0f * (qx * qz - qy * qw);

        r.m[1] = 2.0f * (qx * qy - qz * qw);
        r.m[5] = 1.0f - 2.0f * (qx * qx + qz * qz);
        r.m[9] = 2.0f * (qy * qz + qx * qw);

        r.m[2] = 2.0f * (qx * qz + qy * qw);
        r.m[6] = 2.0f * (qy * qz - qx * qw);
        r.m[10] = 1.0f - 2.0f * (qx * qx + qy * qy);

        float px = eyePose.position.x;
        float py = eyePose.position.y;
        float pz = eyePose.position.z;

        r.m[12] = -(r.m[0] * px + r.m[4] * py + r.m[8] * pz);
        r.m[13] = -(r.m[1] * px + r.m[5] * py + r.m[9] * pz);
        r.m[14] = -(r.m[2] * px + r.m[6] * py + r.m[10] * pz);
        r.m[15] = 1.0f;
        return r;
    }
};

struct ArmVertex {
    float x, y, z;
    float r, g, b, a;
};

static void AddBox(std::vector<ArmVertex>& verts,
                   float minX, float maxX,
                   float minY, float maxY,
                   float minZ, float maxZ,
                   float cr, float cg, float cb) {
    // Top (+Y): factor 1.0f
    float tr = cr * 1.0f, tg = cg * 1.0f, tb = cb * 1.0f;
    verts.push_back({minX, maxY, maxZ, tr, tg, tb, 1.0f});
    verts.push_back({maxX, maxY, maxZ, tr, tg, tb, 1.0f});
    verts.push_back({maxX, maxY, minZ, tr, tg, tb, 1.0f});
    verts.push_back({minX, maxY, maxZ, tr, tg, tb, 1.0f});
    verts.push_back({maxX, maxY, minZ, tr, tg, tb, 1.0f});
    verts.push_back({minX, maxY, minZ, tr, tg, tb, 1.0f});

    // Bottom (-Y): factor 0.5f
    float br = cr * 0.5f, bg = cg * 0.5f, bb = cb * 0.5f;
    verts.push_back({minX, minY, minZ, br, bg, bb, 1.0f});
    verts.push_back({maxX, minY, minZ, br, bg, bb, 1.0f});
    verts.push_back({maxX, minY, maxZ, br, bg, bb, 1.0f});
    verts.push_back({minX, minY, minZ, br, bg, bb, 1.0f});
    verts.push_back({maxX, minY, maxZ, br, bg, bb, 1.0f});
    verts.push_back({minX, minY, maxZ, br, bg, bb, 1.0f});

    // Right (+X): factor 0.75f
    float rr = cr * 0.75f, rg = cg * 0.75f, rb = cb * 0.75f;
    verts.push_back({maxX, minY, minZ, rr, rg, rb, 1.0f});
    verts.push_back({maxX, maxY, minZ, rr, rg, rb, 1.0f});
    verts.push_back({maxX, maxY, maxZ, rr, rg, rb, 1.0f});
    verts.push_back({maxX, minY, minZ, rr, rg, rb, 1.0f});
    verts.push_back({maxX, maxY, maxZ, rr, rg, rb, 1.0f});
    verts.push_back({maxX, minY, maxZ, rr, rg, rb, 1.0f});

    // Left (-X): factor 0.70f
    float lr = cr * 0.70f, lg = cg * 0.70f, lb = cb * 0.70f;
    verts.push_back({minX, minY, maxZ, lr, lg, lb, 1.0f});
    verts.push_back({minX, maxY, maxZ, lr, lg, lb, 1.0f});
    verts.push_back({minX, maxY, minZ, lr, lg, lb, 1.0f});
    verts.push_back({minX, minY, maxZ, lr, lg, lb, 1.0f});
    verts.push_back({minX, maxY, minZ, lr, lg, lb, 1.0f});
    verts.push_back({minX, minY, minZ, lr, lg, lb, 1.0f});

    // Back (+Z): factor 0.80f
    float zpr = cr * 0.80f, zpg = cg * 0.80f, zpb = cb * 0.80f;
    verts.push_back({minX, minY, maxZ, zpr, zpg, zpb, 1.0f});
    verts.push_back({maxX, minY, maxZ, zpr, zpg, zpb, 1.0f});
    verts.push_back({maxX, maxY, maxZ, zpr, zpg, zpb, 1.0f});
    verts.push_back({minX, minY, maxZ, zpr, zpg, zpb, 1.0f});
    verts.push_back({maxX, maxY, maxZ, zpr, zpg, zpb, 1.0f});
    verts.push_back({minX, maxY, maxZ, zpr, zpg, zpb, 1.0f});

    // Front (-Z): factor 0.85f
    float zmr = cr * 0.85f, zmg = cg * 0.85f, zmb = cb * 0.85f;
    verts.push_back({maxX, minY, minZ, zmr, zmg, zmb, 1.0f});
    verts.push_back({minX, minY, minZ, zmr, zmg, zmb, 1.0f});
    verts.push_back({minX, maxY, minZ, zmr, zmg, zmb, 1.0f});
    verts.push_back({maxX, minY, minZ, zmr, zmg, zmb, 1.0f});
    verts.push_back({minX, maxY, minZ, zmr, zmg, zmb, 1.0f});
    verts.push_back({maxX, maxY, minZ, zmr, zmg, zmb, 1.0f});
}

bool InitArmShader() {
    const char* vsSrc =
        "#version 300 es\n"
        "layout(location = 0) in vec3 aPos;\n"
        "layout(location = 1) in vec4 aColor;\n"
        "uniform mat4 uMVP;\n"
        "out vec4 vColor;\n"
        "void main() {\n"
        "    vColor = aColor;\n"
        "    gl_Position = uMVP * vec4(aPos, 1.0);\n"
        "}\n";

    const char* fsSrc =
        "#version 300 es\n"
        "precision mediump float;\n"
        "in vec4 vColor;\n"
        "out vec4 fragColor;\n"
        "void main() {\n"
        "    fragColor = vColor;\n"
        "}\n";

    GLuint vs = glCreateShader(GL_VERTEX_SHADER);
    glShaderSource(vs, 1, &vsSrc, nullptr);
    glCompileShader(vs);

    GLuint fs = glCreateShader(GL_FRAGMENT_SHADER);
    glShaderSource(fs, 1, &fsSrc, nullptr);
    glCompileShader(fs);

    s_armProgram = glCreateProgram();
    glAttachShader(s_armProgram, vs);
    glAttachShader(s_armProgram, fs);
    glLinkProgram(s_armProgram);

    glDeleteShader(vs);
    glDeleteShader(fs);

    GLint linked = 0;
    glGetProgramiv(s_armProgram, GL_LINK_STATUS, &linked);
    if (!linked) {
        LOG_XRE("InitArmShader: program link failed");
        glDeleteProgram(s_armProgram);
        s_armProgram = 0;
        return false;
    }

    s_uArmMVPLoc = glGetUniformLocation(s_armProgram, "uMVP");

    // Build right arm geometry (Skin hand + Cyan sleeve)
    std::vector<ArmVertex> rightVerts;
    AddBox(rightVerts, -0.038f, 0.038f, -0.038f, 0.038f, -0.08f, 0.08f, 0.76f, 0.54f, 0.41f);
    AddBox(rightVerts, -0.042f, 0.042f, -0.042f, 0.042f, 0.08f, 0.28f, 0.0f, 0.66f, 0.61f);
    s_rightArmVertexCount = static_cast<GLsizei>(rightVerts.size());

    glGenVertexArrays(1, &s_armVaoRight);
    glGenBuffers(1, &s_armVboRight);
    glBindVertexArray(s_armVaoRight);
    glBindBuffer(GL_ARRAY_BUFFER, s_armVboRight);
    glBufferData(GL_ARRAY_BUFFER, rightVerts.size() * sizeof(ArmVertex), rightVerts.data(), GL_STATIC_DRAW);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, sizeof(ArmVertex), reinterpret_cast<void*>(offsetof(ArmVertex, x)));
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(1, 4, GL_FLOAT, GL_FALSE, sizeof(ArmVertex), reinterpret_cast<void*>(offsetof(ArmVertex, r)));

    // Build left arm geometry (Skin hand + Dark wristband indicator + Cyan sleeve)
    std::vector<ArmVertex> leftVerts;
    AddBox(leftVerts, -0.038f, 0.038f, -0.038f, 0.038f, -0.08f, 0.08f, 0.76f, 0.54f, 0.41f);
    AddBox(leftVerts, -0.040f, 0.040f, -0.040f, 0.040f, 0.03f, 0.07f, 0.22f, 0.16f, 0.12f);
    AddBox(leftVerts, -0.042f, 0.042f, -0.042f, 0.042f, 0.08f, 0.28f, 0.0f, 0.66f, 0.61f);
    s_leftArmVertexCount = static_cast<GLsizei>(leftVerts.size());

    glGenVertexArrays(1, &s_armVaoLeft);
    glGenBuffers(1, &s_armVboLeft);
    glBindVertexArray(s_armVaoLeft);
    glBindBuffer(GL_ARRAY_BUFFER, s_armVboLeft);
    glBufferData(GL_ARRAY_BUFFER, leftVerts.size() * sizeof(ArmVertex), leftVerts.data(), GL_STATIC_DRAW);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, sizeof(ArmVertex), reinterpret_cast<void*>(offsetof(ArmVertex, x)));
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(1, 4, GL_FLOAT, GL_FALSE, sizeof(ArmVertex), reinterpret_cast<void*>(offsetof(ArmVertex, r)));

    glBindBuffer(GL_ARRAY_BUFFER, 0);
    glBindVertexArray(0);

    LOG_XR("BlitEye::InitArmShader() OK (prog=%u, rightVerts=%d, leftVerts=%d)",
           s_armProgram, s_rightArmVertexCount, s_leftArmVertexCount);
    return true;
}

bool InitReticleShader() {
    const char* vsSrc =
        "#version 300 es\n"
        "layout(location = 0) in vec2 aPos;\n"
        "uniform vec2 uCenter;\n"
        "uniform vec2 uAspect;\n"
        "out vec2 vPos;\n"
        "void main() {\n"
        "    vPos = aPos;\n"
        "    vec2 ndcCenter = vec2(uCenter.x * 2.0 - 1.0, 1.0 - uCenter.y * 2.0);\n"
        "    gl_Position = vec4(ndcCenter + aPos * uAspect, 0.0, 1.0);\n"
        "}\n";

    const char* fsSrc =
        "#version 300 es\n"
        "precision mediump float;\n"
        "in vec2 vPos;\n"
        "out vec4 fragColor;\n"
        "uniform vec4 uColor;\n"
        "void main() {\n"
        "    float d = length(vPos);\n"
        "    if (d > 1.0) discard;\n"
        "    float ring = smoothstep(0.65, 0.75, d) * (1.0 - smoothstep(0.85, 0.95, d));\n"
        "    float dot = 1.0 - smoothstep(0.18, 0.28, d);\n"
        "    float a = max(ring, dot) * uColor.a;\n"
        "    if (a < 0.02) discard;\n"
        "    fragColor = vec4(uColor.rgb, a);\n"
        "}\n";

    GLuint vs = glCreateShader(GL_VERTEX_SHADER);
    glShaderSource(vs, 1, &vsSrc, nullptr);
    glCompileShader(vs);

    GLuint fs = glCreateShader(GL_FRAGMENT_SHADER);
    glShaderSource(fs, 1, &fsSrc, nullptr);
    glCompileShader(fs);

    s_reticleProgram = glCreateProgram();
    glAttachShader(s_reticleProgram, vs);
    glAttachShader(s_reticleProgram, fs);
    glLinkProgram(s_reticleProgram);

    glDeleteShader(vs);
    glDeleteShader(fs);

    GLint linked = 0;
    glGetProgramiv(s_reticleProgram, GL_LINK_STATUS, &linked);
    if (!linked) {
        LOG_XRE("InitReticleShader: program link failed");
        glDeleteProgram(s_reticleProgram);
        s_reticleProgram = 0;
        return false;
    }

    s_uCenterLoc = glGetUniformLocation(s_reticleProgram, "uCenter");
    s_uAspectLoc = glGetUniformLocation(s_reticleProgram, "uAspect");
    s_uColorLoc = glGetUniformLocation(s_reticleProgram, "uColor");

    static const float kQuadVerts[8] = {
        -1.0f, -1.0f,
         1.0f, -1.0f,
        -1.0f,  1.0f,
         1.0f,  1.0f,
    };

    glGenVertexArrays(1, &s_reticleVao);
    glGenBuffers(1, &s_reticleVbo);

    glBindVertexArray(s_reticleVao);
    glBindBuffer(GL_ARRAY_BUFFER, s_reticleVbo);
    glBufferData(GL_ARRAY_BUFFER, sizeof(kQuadVerts), kQuadVerts, GL_STATIC_DRAW);

    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 0, nullptr);

    glBindBuffer(GL_ARRAY_BUFFER, 0);
    glBindVertexArray(0);

    LOG_XR("BlitEye::InitReticleShader() OK (prog=%u, vao=%u)", s_reticleProgram, s_reticleVao);
    return true;
}

}  // namespace

bool BlitEye::Init() {
    if (g_blitFbo == 0) {
        glGenFramebuffers(1, &g_blitFbo);
        LOG_XR("BlitEye::Init() — helper blit FBO created (fbo=%u)", g_blitFbo);
    }
    return g_blitFbo != 0;
}

void BlitEye::Shutdown() {
    if (g_blitFbo != 0) {
        glDeleteFramebuffers(1, &g_blitFbo);
        LOG_XR("BlitEye::Shutdown() — helper blit FBO deleted (fbo=%u)", g_blitFbo);
        g_blitFbo = 0;
    }
    if (g_depthRb != 0) {
        glDeleteRenderbuffers(1, &g_depthRb);
        g_depthRb = 0;
        g_depthRbW = 0;
        g_depthRbH = 0;
    }
    if (s_reticleProgram != 0) {
        glDeleteProgram(s_reticleProgram);
        s_reticleProgram = 0;
    }
    if (s_reticleVao != 0) {
        glDeleteVertexArrays(1, &s_reticleVao);
        s_reticleVao = 0;
    }
    if (s_reticleVbo != 0) {
        glDeleteBuffers(1, &s_reticleVbo);
        s_reticleVbo = 0;
    }
    if (s_armProgram != 0) {
        glDeleteProgram(s_armProgram);
        s_armProgram = 0;
    }
    if (s_armVaoRight != 0) {
        glDeleteVertexArrays(1, &s_armVaoRight);
        s_armVaoRight = 0;
    }
    if (s_armVboRight != 0) {
        glDeleteBuffers(1, &s_armVboRight);
        s_armVboRight = 0;
    }
    if (s_armVaoLeft != 0) {
        glDeleteVertexArrays(1, &s_armVaoLeft);
        s_armVaoLeft = 0;
    }
    if (s_armVboLeft != 0) {
        glDeleteBuffers(1, &s_armVboLeft);
        s_armVboLeft = 0;
    }
}

void BlitEye::SetWindowSize(int w, int h) {
    if (w > 1 && h > 1) {
        s_windowWidth = w;
        s_windowHeight = h;
        LOG_XR("BlitEye::SetWindowSize(%dx%d)", w, h);
    }
}

int BlitEye::GetWindowWidth() {
    return s_windowWidth;
}

int BlitEye::GetWindowHeight() {
    return s_windowHeight;
}

void BlitEye::DrawReticle(float u, float v, bool clicking, int dstW, int dstH) {
    if (s_reticleProgram == 0) {
        InitReticleShader();
    }
    if (s_reticleProgram == 0 || dstW <= 0 || dstH <= 0) return;

    GLint prevProgram = 0;
    GLint prevVao = 0;
    GLint prevVbo = 0;
    glGetIntegerv(GL_CURRENT_PROGRAM, &prevProgram);
    glGetIntegerv(GL_VERTEX_ARRAY_BINDING, &prevVao);
    glGetIntegerv(GL_ARRAY_BUFFER_BINDING, &prevVbo);

    GLboolean prevBlend = glIsEnabled(GL_BLEND);
    GLint prevBlendSrc = 0, prevBlendDst = 0;
    glGetIntegerv(GL_BLEND_SRC_RGB, &prevBlendSrc);
    glGetIntegerv(GL_BLEND_DST_RGB, &prevBlendDst);

    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glViewport(0, 0, dstW, dstH);

    glUseProgram(s_reticleProgram);
    glBindVertexArray(s_reticleVao);

    float radiusPx = 24.0f;
    float aspectX = (radiusPx * 2.0f) / static_cast<float>(dstW);
    float aspectY = (radiusPx * 2.0f) / static_cast<float>(dstH);

    glUniform2f(s_uCenterLoc, u, v);
    glUniform2f(s_uAspectLoc, aspectX, aspectY);

    if (clicking) {
        glUniform4f(s_uColorLoc, 0.15f, 1.0f, 0.35f, 0.95f); // Green when clicking
    } else {
        glUniform4f(s_uColorLoc, 0.10f, 0.85f, 1.0f, 0.90f); // Cyan when hovering
    }

    glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);

    glBindVertexArray(prevVao);
    glBindBuffer(GL_ARRAY_BUFFER, prevVbo);
    glUseProgram(prevProgram);

    if (!prevBlend) {
        glDisable(GL_BLEND);
    } else {
        glBlendFunc(prevBlendSrc, prevBlendDst);
    }
}

void BlitEye::Blit(GLuint swapchainArrayTex, int dstW, int dstH) {
    if (swapchainArrayTex == 0 || dstW <= 0 || dstH <= 0) {
        return;
    }

    if (g_blitFbo == 0) {
        Init();
    }
    if (g_blitFbo == 0) {
        LOG_XRE("BlitEye::Blit failed: could not initialize blit FBO");
        return;
    }

    GLint prevReadFbo = 0;
    GLint prevDrawFbo = 0;
    GLint prevViewport[4] = {0, 0, 0, 0};

    glGetIntegerv(GL_READ_FRAMEBUFFER_BINDING, &prevReadFbo);
    glGetIntegerv(GL_DRAW_FRAMEBUFFER_BINDING, &prevDrawFbo);
    glGetIntegerv(GL_VIEWPORT, prevViewport);

    int srcW = s_windowWidth;
    int srcH = s_windowHeight;
    if (prevDrawFbo != 0 && prevViewport[2] > 1 && prevViewport[3] > 1) {
        srcW = prevViewport[2];
        srcH = prevViewport[3];
    }

    if (prevReadFbo == 0 && prevDrawFbo != 0) {
        glBindFramebuffer(GL_READ_FRAMEBUFFER, prevDrawFbo);
    }

    GLboolean prevScissor = glIsEnabled(GL_SCISSOR_TEST);
    if (prevScissor) {
        glDisable(GL_SCISSOR_TEST);
    }

    glBindFramebuffer(GL_DRAW_FRAMEBUFFER, g_blitFbo);

    // Eye layer 0 (Left Eye)
    glFramebufferTextureLayer(GL_DRAW_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, swapchainArrayTex, 0, 0);
    glBlitFramebuffer(0, 0, srcW, srcH, 0, 0, dstW, dstH, GL_COLOR_BUFFER_BIT, GL_NEAREST);

    // Eye layer 1 (Right Eye)
    glFramebufferTextureLayer(GL_DRAW_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, swapchainArrayTex, 0, 1);
    glBlitFramebuffer(0, 0, srcW, srcH, 0, 0, dstW, dstH, GL_COLOR_BUFFER_BIT, GL_NEAREST);

    // Restore previous framebuffer bindings, viewport, and scissor
    glBindFramebuffer(GL_READ_FRAMEBUFFER, prevReadFbo);
    glBindFramebuffer(GL_DRAW_FRAMEBUFFER, prevDrawFbo);
    glViewport(prevViewport[0], prevViewport[1], prevViewport[2], prevViewport[3]);
    if (prevScissor) {
        glEnable(GL_SCISSOR_TEST);
    }
}

void BlitEye::DrawVoxelArm(const XrPosef& handPose, const XrView& eyeView, int dstW, int dstH, bool isLeftArm) {
    if (s_armProgram == 0) {
        InitArmShader();
    }
    if (s_armProgram == 0 || dstW <= 0 || dstH <= 0) return;

    glUseProgram(s_armProgram);
    glViewport(0, 0, dstW, dstH);

    Mat4 proj = Mat4::ProjectionFromFov(eyeView.fov, 0.05f, 50.0f);
    Mat4 view = Mat4::ViewFromEyePose(eyeView.pose);
    Mat4 model = Mat4::FromPose(handPose);

    Mat4 vp = Mat4::Multiply(proj, view);
    Mat4 mvp = Mat4::Multiply(vp, model);

    glUniformMatrix4fv(s_uArmMVPLoc, 1, GL_FALSE, mvp.m);

    if (isLeftArm) {
        glBindVertexArray(s_armVaoLeft);
        glDrawArrays(GL_TRIANGLES, 0, s_leftArmVertexCount);
    } else {
        glBindVertexArray(s_armVaoRight);
        glDrawArrays(GL_TRIANGLES, 0, s_rightArmVertexCount);
    }
    glBindVertexArray(0);
}

void BlitEye::BlitStereo(GLuint leftTex, GLuint rightTex, int dstW, int dstH, const XrView* views) {
    if (leftTex == 0 || rightTex == 0 || dstW <= 0 || dstH <= 0) {
        return;
    }

    if (g_blitFbo == 0) {
        Init();
    }
    if (g_blitFbo == 0) {
        LOG_XRE("BlitEye::BlitStereo failed: could not initialize blit FBO");
        return;
    }

    // Attach or reallocate depth renderbuffer on g_blitFbo
    if (g_depthRb == 0 || g_depthRbW != dstW || g_depthRbH != dstH) {
        if (g_depthRb == 0) {
            glGenRenderbuffers(1, &g_depthRb);
        }
        glBindRenderbuffer(GL_RENDERBUFFER, g_depthRb);
        glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH_COMPONENT24, dstW, dstH);
        glBindFramebuffer(GL_DRAW_FRAMEBUFFER, g_blitFbo);
        glFramebufferRenderbuffer(GL_DRAW_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_RENDERBUFFER, g_depthRb);
        g_depthRbW = dstW;
        g_depthRbH = dstH;
        glBindRenderbuffer(GL_RENDERBUFFER, 0);
    }

    GLint prevReadFbo = 0;
    GLint prevDrawFbo = 0;
    GLint prevViewport[4] = {0, 0, 0, 0};

    glGetIntegerv(GL_READ_FRAMEBUFFER_BINDING, &prevReadFbo);
    glGetIntegerv(GL_DRAW_FRAMEBUFFER_BINDING, &prevDrawFbo);
    glGetIntegerv(GL_VIEWPORT, prevViewport);

    int srcW = s_windowWidth;
    int srcH = s_windowHeight;
    if (prevDrawFbo != 0 && prevViewport[2] > 1 && prevViewport[3] > 1) {
        srcW = prevViewport[2];
        srcH = prevViewport[3];
    }

    GLint srcFbo = (prevDrawFbo != 0) ? prevDrawFbo : 0;
    glBindFramebuffer(GL_READ_FRAMEBUFFER, srcFbo);

    GLboolean prevScissor = glIsEnabled(GL_SCISSOR_TEST);
    if (prevScissor) {
        glDisable(GL_SCISSOR_TEST);
    }

    glBindFramebuffer(GL_DRAW_FRAMEBUFFER, g_blitFbo);

    const auto& laser = PicoInput::GetLaserHit();
    bool isCinema = GetQuadLayerMode();
    const auto& snap = PicoInput::GetSnapshot();

    for (int eye = 0; eye < 2; eye++) {
        GLuint tex = (eye == 0) ? leftTex : rightTex;
        glFramebufferTexture2D(GL_DRAW_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, tex, 0);
        glBlitFramebuffer(0, 0, srcW, srcH, 0, 0, dstW, dstH, GL_COLOR_BUFFER_BIT, GL_NEAREST);

        if (isCinema) {
            if (laser.is_hit) {
                DrawReticle(laser.u, laser.v, laser.clicking, dstW, dstH);
            }
        } else {
            // 360 Immersive VR Mode: No floating reticle! Render 3D Tracked Minecraft Arms
            if (views != nullptr) {
                glEnable(GL_DEPTH_TEST);
                glDepthFunc(GL_LEQUAL);
                glDepthMask(GL_TRUE);
                glEnable(GL_CULL_FACE);
                glCullFace(GL_BACK);
                glClear(GL_DEPTH_BUFFER_BIT);

                if (snap.left.pose_valid) {
                    DrawVoxelArm(snap.left.pose, views[eye], dstW, dstH, /*isLeftArm*/ true);
                }
                if (snap.right.pose_valid) {
                    DrawVoxelArm(snap.right.pose, views[eye], dstW, dstH, /*isLeftArm*/ false);
                }

                glDisable(GL_CULL_FACE);
                glDisable(GL_DEPTH_TEST);
            }
        }
    }

    // Restore previous framebuffer bindings, viewport, and scissor
    glBindFramebuffer(GL_READ_FRAMEBUFFER, prevReadFbo);
    glBindFramebuffer(GL_DRAW_FRAMEBUFFER, prevDrawFbo);
    glViewport(prevViewport[0], prevViewport[1], prevViewport[2], prevViewport[3]);
    if (prevScissor) {
        glEnable(GL_SCISSOR_TEST);
    }
}

}  // namespace mcbedvr::xr
