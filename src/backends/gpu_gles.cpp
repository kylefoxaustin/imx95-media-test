// SPDX-License-Identifier: BSD-3-Clause
//
// Real GPU workload: headless EGL + OpenGL ES 2.0. Renders a procedural sphere
// scene into an offscreen framebuffer; complexity scales with the GpuLevel
// (resolution x geometry subdivisions x instance count x light count x
// fragment ALU). GLES2 + EGL surfaceless is portable across host Mesa (for
// development/CI) and the i.MX95 Mali driver (for the real measurement).
//
// It estimates its DDR traffic into the shared traffic bus so the mock DDR
// monitor shows a meaningful number on a host; on target the real DDR-PMU
// monitor reads hardware counters instead.

#include <cmath>
#include <cstring>
#include <string>
#include <vector>

#include <EGL/egl.h>
#include <EGL/eglext.h>
#include <GLES2/gl2.h>

#include "backend.hpp"
#include "backends/traffic_estimate.hpp"

namespace imx95 {

namespace {

constexpr int kMaxLights = 8;
constexpr int kMaxExtra = 256;

// ---- tiny column-major mat4 helpers ----------------------------------------

struct Mat4 { float m[16]; };

Mat4 identity() {
    Mat4 r{};
    r.m[0] = r.m[5] = r.m[10] = r.m[15] = 1.0f;
    return r;
}

Mat4 mul(const Mat4& a, const Mat4& b) {
    Mat4 r{};
    for (int c = 0; c < 4; ++c)
        for (int row = 0; row < 4; ++row) {
            float s = 0.0f;
            for (int k = 0; k < 4; ++k) s += a.m[k * 4 + row] * b.m[c * 4 + k];
            r.m[c * 4 + row] = s;
        }
    return r;
}

Mat4 perspective(float fovy, float aspect, float n, float f) {
    Mat4 r{};
    float t = 1.0f / std::tan(fovy * 0.5f);
    r.m[0] = t / aspect;
    r.m[5] = t;
    r.m[10] = (f + n) / (n - f);
    r.m[11] = -1.0f;
    r.m[14] = (2.0f * f * n) / (n - f);
    return r;
}

Mat4 translate(float x, float y, float z) {
    Mat4 r = identity();
    r.m[12] = x; r.m[13] = y; r.m[14] = z;
    return r;
}

Mat4 rotateY(float a) {
    Mat4 r = identity();
    float c = std::cos(a), s = std::sin(a);
    r.m[0] = c; r.m[8] = s; r.m[2] = -s; r.m[10] = c;
    return r;
}

Mat4 rotateX(float a) {
    Mat4 r = identity();
    float c = std::cos(a), s = std::sin(a);
    r.m[5] = c; r.m[9] = -s; r.m[6] = s; r.m[10] = c;
    return r;
}

// ---- shaders ----------------------------------------------------------------

const char* kVert = R"(
attribute vec3 aPos;
attribute vec3 aNormal;
uniform mat4 uMVP;
uniform mat4 uModel;
varying vec3 vN;
varying vec3 vWorld;
void main() {
    vec4 wp = uModel * vec4(aPos, 1.0);
    vWorld = wp.xyz;
    vN = mat3(uModel) * aNormal;
    gl_Position = uMVP * vec4(aPos, 1.0);
}
)";

const char* kFrag = R"(
precision highp float;
varying vec3 vN;
varying vec3 vWorld;
uniform int uNumLights;
uniform vec3 uLightPos[8];
uniform int uExtra;
void main() {
    vec3 n = normalize(vN);
    vec3 c = vec3(0.04);
    for (int i = 0; i < 8; i++) {
        if (i >= uNumLights) break;
        vec3 l = normalize(uLightPos[i] - vWorld);
        float d = max(dot(n, l), 0.0);
        c += vec3(0.7, 0.6, 0.5) * d;
    }
    float e = 0.0;
    for (int k = 0; k < 256; k++) {
        if (k >= uExtra) break;
        e += sin(float(k) * 0.13 + vWorld.x) * cos(float(k) * 0.07 + vWorld.y);
    }
    c += abs(e) * 0.001;
    gl_FragColor = vec4(c, 1.0);
}
)";

struct Params {
    int w, h, subdiv, instances, lights, extra, passes;
};

Params params_for(GpuLevel lvl) {
    switch (lvl) {
        case GpuLevel::Low: return {1280,  720, 16,  1, 1,   0, 1};
        case GpuLevel::Mid: return {1920, 1080, 48,  8, 4,  32, 1};
        case GpuLevel::Max: return {3840, 2160, 96, 24, 8, 192, 2};
        default:            return {1280,  720, 16,  1, 1,   0, 1};
    }
}

GLuint compile(GLenum type, const char* src, std::string& err) {
    GLuint s = glCreateShader(type);
    glShaderSource(s, 1, &src, nullptr);
    glCompileShader(s);
    GLint ok = 0;
    glGetShaderiv(s, GL_COMPILE_STATUS, &ok);
    if (!ok) {
        char log[1024] = {0};
        glGetShaderInfoLog(s, sizeof(log) - 1, nullptr, log);
        err = std::string("shader compile failed: ") + log;
        glDeleteShader(s);
        return 0;
    }
    return s;
}

class GlesGpu : public Workload {
public:
    explicit GlesGpu(GpuLevel lvl) : p_(params_for(lvl)) {
        stats_.name = std::string("GPU ") + to_string(lvl);
    }

    const char* kind() const override { return "GPU"; }
    uint64_t frames_per_loop() const override { return 600; }

    bool init(std::string& err) override {
        if (!init_egl(err)) return false;
        if (!init_gl(err)) { shutdown(); return false; }
        // FBO color texture + depth + (a few) buffers.
        stats_.alloc.store(static_cast<uint64_t>(p_.w) * p_.h * 4
                           + index_count_ * 2 + vertex_bytes_);
        return true;
    }

    bool step() override {
        const float angle = stats_.frames.load(std::memory_order_relaxed) * 0.01f;

        glBindFramebuffer(GL_FRAMEBUFFER, fbo_);
        glViewport(0, 0, p_.w, p_.h);
        glEnable(GL_DEPTH_TEST);
        glClearColor(0.02f, 0.02f, 0.04f, 1.0f);

        Mat4 proj = perspective(1.05f, float(p_.w) / float(p_.h), 0.1f, 200.0f);
        int cols = static_cast<int>(std::ceil(std::sqrt(double(p_.instances))));
        float dist = std::max(7.0f, cols * 2.6f);
        Mat4 view = translate(0.0f, 0.0f, -dist);

        glUseProgram(prog_);
        glBindBuffer(GL_ARRAY_BUFFER, vbo_);
        glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, ibo_);
        glEnableVertexAttribArray(0);
        glEnableVertexAttribArray(1);
        glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 6 * sizeof(float), (void*)0);
        glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, 6 * sizeof(float),
                              (void*)(3 * sizeof(float)));

        float lights[kMaxLights * 3];
        for (int i = 0; i < p_.lights; ++i) {
            float a = angle + i * (6.2832f / p_.lights);
            lights[i * 3 + 0] = std::cos(a) * 8.0f;
            lights[i * 3 + 1] = std::sin(a * 1.3f) * 6.0f;
            lights[i * 3 + 2] = 6.0f + std::sin(a) * 4.0f;
        }
        glUniform3fv(uLightPos_, p_.lights, lights);
        glUniform1i(uNumLights_, p_.lights);
        glUniform1i(uExtra_, p_.extra);

        for (int pass = 0; pass < p_.passes; ++pass) {
            glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
            for (int i = 0; i < p_.instances; ++i) {
                int gx = i % cols, gy = i / cols;
                Mat4 model = mul(translate((gx - cols * 0.5f) * 2.6f,
                                           (gy - cols * 0.5f) * 2.6f, 0.0f),
                                 mul(rotateY(angle + i), rotateX(angle * 0.7f)));
                Mat4 mvp = mul(proj, mul(view, model));
                glUniformMatrix4fv(uMVP_, 1, GL_FALSE, mvp.m);
                glUniformMatrix4fv(uModel_, 1, GL_FALSE, model.m);
                glDrawElements(GL_TRIANGLES, index_count_, GL_UNSIGNED_SHORT, 0);
            }
        }
        glFinish();  // force the GPU to actually complete the frame

        stats_.frames.fetch_add(1, std::memory_order_relaxed);
        uint64_t fb = static_cast<uint64_t>(p_.w) * p_.h * 4 * p_.passes;
        stats_.bytes.fetch_add(fb, std::memory_order_relaxed);
        traffic_estimate_add(fb, fb);  // rough color+depth read/write estimate
        return true;
    }

    void shutdown() override {
        if (dpy_ != EGL_NO_DISPLAY) {
            eglMakeCurrent(dpy_, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);
            if (ctx_ != EGL_NO_CONTEXT) eglDestroyContext(dpy_, ctx_);
            if (surf_ != EGL_NO_SURFACE) eglDestroySurface(dpy_, surf_);
            eglTerminate(dpy_);
        }
        dpy_ = EGL_NO_DISPLAY;
        ctx_ = EGL_NO_CONTEXT;
        surf_ = EGL_NO_SURFACE;
    }

private:
    bool init_egl(std::string& err) {
        const char* cexts = eglQueryString(EGL_NO_DISPLAY, EGL_EXTENSIONS);
        if (cexts && std::strstr(cexts, "EGL_MESA_platform_surfaceless")) {
            auto getPlatformDisplay = reinterpret_cast<PFNEGLGETPLATFORMDISPLAYEXTPROC>(
                eglGetProcAddress("eglGetPlatformDisplayEXT"));
            if (getPlatformDisplay)
                dpy_ = getPlatformDisplay(EGL_PLATFORM_SURFACELESS_MESA, EGL_DEFAULT_DISPLAY,
                                          nullptr);
        }
        if (dpy_ == EGL_NO_DISPLAY) dpy_ = eglGetDisplay(EGL_DEFAULT_DISPLAY);
        if (dpy_ == EGL_NO_DISPLAY) { err = "eglGetDisplay failed"; return false; }

        EGLint major = 0, minor = 0;
        if (!eglInitialize(dpy_, &major, &minor)) { err = "eglInitialize failed"; return false; }
        if (!eglBindAPI(EGL_OPENGL_ES_API)) { err = "eglBindAPI(ES) failed"; return false; }

        const EGLint cfg_attr[] = {
            EGL_RENDERABLE_TYPE, EGL_OPENGL_ES2_BIT,
            EGL_SURFACE_TYPE,    EGL_PBUFFER_BIT,
            EGL_RED_SIZE, 8, EGL_GREEN_SIZE, 8, EGL_BLUE_SIZE, 8, EGL_ALPHA_SIZE, 8,
            EGL_DEPTH_SIZE, 16,
            EGL_NONE};
        EGLConfig cfg;
        EGLint n = 0;
        if (!eglChooseConfig(dpy_, cfg_attr, &cfg, 1, &n) || n < 1) {
            err = "eglChooseConfig found no ES2 config";
            return false;
        }

        const EGLint ctx_attr[] = {EGL_CONTEXT_CLIENT_VERSION, 2, EGL_NONE};
        ctx_ = eglCreateContext(dpy_, cfg, EGL_NO_CONTEXT, ctx_attr);
        if (ctx_ == EGL_NO_CONTEXT) { err = "eglCreateContext failed"; return false; }

        const char* dexts = eglQueryString(dpy_, EGL_EXTENSIONS);
        bool surfaceless = dexts && std::strstr(dexts, "EGL_KHR_surfaceless_context");
        if (!surfaceless) {
            const EGLint pb[] = {EGL_WIDTH, 16, EGL_HEIGHT, 16, EGL_NONE};
            surf_ = eglCreatePbufferSurface(dpy_, cfg, pb);
            if (surf_ == EGL_NO_SURFACE) { err = "eglCreatePbufferSurface failed"; return false; }
        }
        if (!eglMakeCurrent(dpy_, surf_, surf_, ctx_)) {
            err = "eglMakeCurrent failed";
            return false;
        }
        return true;
    }

    bool init_gl(std::string& err) {
        GLuint vs = compile(GL_VERTEX_SHADER, kVert, err);
        if (!vs) return false;
        GLuint fs = compile(GL_FRAGMENT_SHADER, kFrag, err);
        if (!fs) { glDeleteShader(vs); return false; }
        prog_ = glCreateProgram();
        glAttachShader(prog_, vs);
        glAttachShader(prog_, fs);
        glBindAttribLocation(prog_, 0, "aPos");
        glBindAttribLocation(prog_, 1, "aNormal");
        glLinkProgram(prog_);
        glDeleteShader(vs);
        glDeleteShader(fs);
        GLint ok = 0;
        glGetProgramiv(prog_, GL_LINK_STATUS, &ok);
        if (!ok) {
            char log[1024] = {0};
            glGetProgramInfoLog(prog_, sizeof(log) - 1, nullptr, log);
            err = std::string("program link failed: ") + log;
            return false;
        }
        uMVP_ = glGetUniformLocation(prog_, "uMVP");
        uModel_ = glGetUniformLocation(prog_, "uModel");
        uNumLights_ = glGetUniformLocation(prog_, "uNumLights");
        uLightPos_ = glGetUniformLocation(prog_, "uLightPos");
        uExtra_ = glGetUniformLocation(prog_, "uExtra");

        build_sphere();

        // Offscreen framebuffer: RGBA texture color + depth renderbuffer.
        glGenTextures(1, &tex_);
        glBindTexture(GL_TEXTURE_2D, tex_);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, p_.w, p_.h, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);

        glGenRenderbuffers(1, &depth_);
        glBindRenderbuffer(GL_RENDERBUFFER, depth_);
        glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH_COMPONENT16, p_.w, p_.h);

        glGenFramebuffers(1, &fbo_);
        glBindFramebuffer(GL_FRAMEBUFFER, fbo_);
        glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, tex_, 0);
        glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_RENDERBUFFER, depth_);
        if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE) {
            err = "framebuffer incomplete (resolution may exceed GL_MAX_*_SIZE)";
            return false;
        }
        return true;
    }

    void build_sphere() {
        const int sd = p_.subdiv;
        std::vector<float> verts;
        verts.reserve((sd + 1) * (sd + 1) * 6);
        for (int ring = 0; ring <= sd; ++ring) {
            float phi = 3.14159265f * ring / sd;
            for (int sec = 0; sec <= sd; ++sec) {
                float theta = 6.2831853f * sec / sd;
                float x = std::sin(phi) * std::cos(theta);
                float y = std::cos(phi);
                float z = std::sin(phi) * std::sin(theta);
                verts.insert(verts.end(), {x, y, z, x, y, z});  // pos == normal (unit sphere)
            }
        }
        std::vector<unsigned short> idx;
        for (int ring = 0; ring < sd; ++ring) {
            for (int sec = 0; sec < sd; ++sec) {
                unsigned short a = static_cast<unsigned short>(ring * (sd + 1) + sec);
                unsigned short b = static_cast<unsigned short>(a + sd + 1);
                idx.insert(idx.end(), {a, b, static_cast<unsigned short>(a + 1),
                                       static_cast<unsigned short>(a + 1), b,
                                       static_cast<unsigned short>(b + 1)});
            }
        }
        index_count_ = static_cast<GLsizei>(idx.size());
        vertex_bytes_ = verts.size() * sizeof(float);

        glGenBuffers(1, &vbo_);
        glBindBuffer(GL_ARRAY_BUFFER, vbo_);
        glBufferData(GL_ARRAY_BUFFER, vertex_bytes_, verts.data(), GL_STATIC_DRAW);
        glGenBuffers(1, &ibo_);
        glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, ibo_);
        glBufferData(GL_ELEMENT_ARRAY_BUFFER, idx.size() * sizeof(unsigned short), idx.data(),
                     GL_STATIC_DRAW);
    }

    Params p_;
    EGLDisplay dpy_ = EGL_NO_DISPLAY;
    EGLContext ctx_ = EGL_NO_CONTEXT;
    EGLSurface surf_ = EGL_NO_SURFACE;
    GLuint prog_ = 0, vbo_ = 0, ibo_ = 0, tex_ = 0, depth_ = 0, fbo_ = 0;
    GLint uMVP_ = -1, uModel_ = -1, uNumLights_ = -1, uLightPos_ = -1, uExtra_ = -1;
    GLsizei index_count_ = 0;
    uint64_t vertex_bytes_ = 0;
};

} // namespace

std::unique_ptr<Workload> make_gpu_workload(GpuLevel lvl) {
    return std::make_unique<GlesGpu>(lvl);
}

const char* gpu_backend_name() { return "gles"; }

} // namespace imx95
