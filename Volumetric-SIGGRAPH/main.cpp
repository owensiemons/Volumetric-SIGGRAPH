// =====================================================================================
//  Volumetric cloud renderer  -  OpenGL 3.3 core, GLFW + GLAD, single file
//
//  Technique (in the spirit of Horizon Zero Dawn / Nubis):
//    * Tileable 3D noise generated on the CPU at startup (Perlin-Worley + Worley FBM)
//    * 2D weather map controlling coverage and cloud type
//    * Full-screen ray marching through a cloud layer (1.5 km - 4 km altitude)
//    * Beer-Lambert extinction, light marching toward the sun for self-shadowing
//    * Dual-lobe Henyey-Greenstein phase function (silver linings)
//    * Multiple-scattering approximation (Wrenninge octaves) + "powder" darkening
//    * Height-dependent cloud profiles (stratus -> cumulus), wind + shear, detail erosion
//    * Aerial perspective, ACES tone mapping, half-resolution rendering option
//
//  Controls:
//    Left mouse drag   look around          W/A/S/D   move       Q/E   down/up
//    Shift             move fast            Arrows    sun elevation / azimuth
//    Z / X             coverage -/+         C / V     density -/+
//    B / N             detail erosion -/+   R         toggle half / full resolution
//    P                 pause wind           Esc       quit
//
//  Units: 1.0 = 1 km.  BUILD IN RELEASE MODE - noise generation is much slower in Debug.
// =====================================================================================

#include <glad/glad.h>
#include <GLFW/glfw3.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <iostream>
#include <string>
#include <thread>
#include <vector>

// -------------------------------------------------------------------------------------
//  Tiny vector math
// -------------------------------------------------------------------------------------
struct Vec3 { float x, y, z; };
static Vec3 operator+(Vec3 a, Vec3 b) { return { a.x + b.x, a.y + b.y, a.z + b.z }; }
static Vec3 operator-(Vec3 a, Vec3 b) { return { a.x - b.x, a.y - b.y, a.z - b.z }; }
static Vec3 operator*(Vec3 a, float s) { return { a.x * s, a.y * s, a.z * s }; }
static Vec3 cross(Vec3 a, Vec3 b) { return { a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x }; }
static Vec3 normalize(Vec3 a) { float l = std::sqrt(a.x * a.x + a.y * a.y + a.z * a.z); return l > 0 ? a * (1.0f / l) : a; }

// -------------------------------------------------------------------------------------
//  Tileable noise (CPU)
// -------------------------------------------------------------------------------------
namespace noise {

    static inline uint32_t hash(uint32_t x) {
        x ^= x >> 16; x *= 0x7feb352dU; x ^= x >> 15; x *= 0x846ca68bU; x ^= x >> 16;
        return x;
    }
    static inline uint32_t hash3(int x, int y, int z, uint32_t seed) {
        return hash((uint32_t)x * 73856093u ^ hash((uint32_t)y * 19349663u ^ hash((uint32_t)z * 83492791u ^ seed)));
    }
    static inline float toFloat(uint32_t h) { return (h >> 8) * (1.0f / 16777216.0f); }
    static inline int wrap(int i, int n) { int r = i % n; return r < 0 ? r + n : r; }

    // Worley (cellular) noise with one feature point per cell, wrapping at 'cells'.
    struct WorleyGrid {
        int n;
        std::vector<float> pts; // per-cell feature point, local coords in [0,1)
        WorleyGrid(int cells, uint32_t seed) : n(cells), pts((size_t)cells* cells* cells * 3) {
            for (int z = 0; z < n; ++z)
                for (int y = 0; y < n; ++y)
                    for (int x = 0; x < n; ++x) {
                        uint32_t h = hash3(x, y, z, seed);
                        size_t i = ((size_t)(z * n + y) * n + x) * 3;
                        pts[i + 0] = toFloat(h);
                        pts[i + 1] = toFloat(hash(h + 0x9e3779b9u));
                        pts[i + 2] = toFloat(hash(h + 0x85ebca6bu));
                    }
        }
        // Returns distance to nearest feature point (0 = at point, ~1 = far)
        float eval(float u, float v, float w) const {
            float x = u * n, y = v * n, z = w * n;
            int cx = (int)std::floor(x), cy = (int)std::floor(y), cz = (int)std::floor(z);
            float fx = x - cx, fy = y - cy, fz = z - cz;
            float best = 8.0f;
            for (int dz = -1; dz <= 1; ++dz)
                for (int dy = -1; dy <= 1; ++dy)
                    for (int dx = -1; dx <= 1; ++dx) {
                        int ix = wrap(cx + dx, n), iy = wrap(cy + dy, n), iz = wrap(cz + dz, n);
                        const float* p = &pts[((size_t)(iz * n + iy) * n + ix) * 3];
                        float ddx = dx + p[0] - fx, ddy = dy + p[1] - fy, ddz = dz + p[2] - fz;
                        best = std::min(best, ddx * ddx + ddy * ddy + ddz * ddz);
                    }
            return std::min(std::sqrt(best), 1.0f);
        }
    };

    // 3-octave inverted Worley FBM: bright "puffs" at feature points
    struct WorleyFbm {
        WorleyGrid g0, g1, g2;
        WorleyFbm(int cells, uint32_t seed) : g0(cells, seed), g1(cells * 2, seed + 1), g2(cells * 4, seed + 2) {}
        float eval(float u, float v, float w) const {
            return (1.0f - g0.eval(u, v, w)) * 0.625f +
                (1.0f - g1.eval(u, v, w)) * 0.25f +
                (1.0f - g2.eval(u, v, w)) * 0.125f;
        }
    };

    static inline float fade(float t) { return t * t * t * (t * (t * 6.0f - 15.0f) + 10.0f); }
    static inline float lerp(float a, float b, float t) { return a + (b - a) * t; }
    static inline float grad(uint32_t h, float x, float y, float z) {
        switch (h % 12u) {
        case 0: return  x + y;  case 1: return -x + y;  case 2: return  x - y;  case 3: return -x - y;
        case 4: return  x + z;  case 5: return -x + z;  case 6: return  x - z;  case 7: return -x - z;
        case 8: return  y + z;  case 9: return -y + z;  case 10: return y - z;  default: return -y - z;
        }
    }

    // Tileable gradient (Perlin) noise, period in lattice cells. Returns roughly [-1, 1].
    static float perlin(float u, float v, float w, int period, uint32_t seed) {
        float x = u * period, y = v * period, z = w * period;
        int X = (int)std::floor(x), Y = (int)std::floor(y), Z = (int)std::floor(z);
        float fx = x - X, fy = y - Y, fz = z - Z;
        int x0 = wrap(X, period), x1 = wrap(X + 1, period);
        int y0 = wrap(Y, period), y1 = wrap(Y + 1, period);
        int z0 = wrap(Z, period), z1 = wrap(Z + 1, period);
        float ux = fade(fx), uy = fade(fy), uz = fade(fz);

        float n000 = grad(hash3(x0, y0, z0, seed), fx, fy, fz);
        float n100 = grad(hash3(x1, y0, z0, seed), fx - 1.0f, fy, fz);
        float n010 = grad(hash3(x0, y1, z0, seed), fx, fy - 1.0f, fz);
        float n110 = grad(hash3(x1, y1, z0, seed), fx - 1.0f, fy - 1.0f, fz);
        float n001 = grad(hash3(x0, y0, z1, seed), fx, fy, fz - 1.0f);
        float n101 = grad(hash3(x1, y0, z1, seed), fx - 1.0f, fy, fz - 1.0f);
        float n011 = grad(hash3(x0, y1, z1, seed), fx, fy - 1.0f, fz - 1.0f);
        float n111 = grad(hash3(x1, y1, z1, seed), fx - 1.0f, fy - 1.0f, fz - 1.0f);

        float nx00 = lerp(n000, n100, ux), nx10 = lerp(n010, n110, ux);
        float nx01 = lerp(n001, n101, ux), nx11 = lerp(n011, n111, ux);
        return lerp(lerp(nx00, nx10, uy), lerp(nx01, nx11, uy), uz);
    }

    static float perlinFbm(float u, float v, float w, int period, int octaves, uint32_t seed) {
        float sum = 0.0f, amp = 1.0f, norm = 0.0f;
        for (int i = 0; i < octaves; ++i) {
            sum += amp * perlin(u, v, w, period, seed + i * 1013u);
            norm += amp; amp *= 0.5f; period *= 2;
        }
        return sum / norm;
    }

} // namespace noise

// Run f(i) for i in [0, n) on all CPU cores
template <typename F>
static void parallelFor(int n, F f) {
    unsigned tc = std::max(1u, std::thread::hardware_concurrency());
    std::atomic<int> next{ 0 };
    std::vector<std::thread> threads;
    for (unsigned t = 0; t < tc; ++t)
        threads.emplace_back([&] { int i; while ((i = next++) < n) f(i); });
    for (auto& t : threads) t.join();
}

// Stretch each channel to the full [0,1] range, then quantize to bytes
static std::vector<uint8_t> normalizeToBytes(const std::vector<float>& data, int channels) {
    std::vector<uint8_t> out(data.size());
    for (int c = 0; c < channels; ++c) {
        float mn = 1e9f, mx = -1e9f;
        for (size_t i = c; i < data.size(); i += channels) { mn = std::min(mn, data[i]); mx = std::max(mx, data[i]); }
        float inv = (mx > mn) ? 1.0f / (mx - mn) : 1.0f;
        for (size_t i = c; i < data.size(); i += channels) {
            float v = std::clamp((data[i] - mn) * inv, 0.0f, 1.0f);
            out[i] = (uint8_t)(v * 255.0f + 0.5f);
        }
    }
    return out;
}

static void setTexParams(GLenum target) {
    glTexParameteri(target, GL_TEXTURE_WRAP_S, GL_REPEAT);
    glTexParameteri(target, GL_TEXTURE_WRAP_T, GL_REPEAT);
    if (target == GL_TEXTURE_3D) glTexParameteri(target, GL_TEXTURE_WRAP_R, GL_REPEAT);
    glTexParameteri(target, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
    glTexParameteri(target, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glGenerateMipmap(target);
}

// Low-frequency shape noise: R = Perlin-Worley, G/B/A = Worley FBM at increasing frequencies
static GLuint makeShapeTexture(int N) {
    using namespace noise;
    WorleyFbm wPW(4, 21), wG(4, 41), wB(8, 61), wA(16, 81);
    std::vector<float> data((size_t)N * N * N * 4);
    parallelFor(N, [&](int z) {
        for (int y = 0; y < N; ++y)
            for (int x = 0; x < N; ++x) {
                float u = (x + 0.5f) / N, v = (y + 0.5f) / N, w = (z + 0.5f) / N;
                float p = perlinFbm(u, v, w, 4, 5, 11) * 0.5f + 0.5f;
                float wf = wPW.eval(u, v, w);
                size_t i = ((size_t)(z * N + y) * N + x) * 4;
                data[i + 0] = wf + p * (1.0f - wf);     // Perlin dilated by Worley -> billowy blobs
                data[i + 1] = wG.eval(u, v, w);
                data[i + 2] = wB.eval(u, v, w);
                data[i + 3] = wA.eval(u, v, w);
            }
        });
    auto bytes = normalizeToBytes(data, 4);
    GLuint tex; glGenTextures(1, &tex);
    glBindTexture(GL_TEXTURE_3D, tex);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    glTexImage3D(GL_TEXTURE_3D, 0, GL_RGBA8, N, N, N, 0, GL_RGBA, GL_UNSIGNED_BYTE, bytes.data());
    setTexParams(GL_TEXTURE_3D);
    return tex;
}

// High-frequency detail noise used to erode the cloud edges
static GLuint makeDetailTexture(int N) {
    using namespace noise;
    WorleyFbm wR(2, 101), wG(4, 121), wB(8, 141);
    std::vector<float> data((size_t)N * N * N * 3);
    parallelFor(N, [&](int z) {
        for (int y = 0; y < N; ++y)
            for (int x = 0; x < N; ++x) {
                float u = (x + 0.5f) / N, v = (y + 0.5f) / N, w = (z + 0.5f) / N;
                size_t i = ((size_t)(z * N + y) * N + x) * 3;
                data[i + 0] = wR.eval(u, v, w);
                data[i + 1] = wG.eval(u, v, w);
                data[i + 2] = wB.eval(u, v, w);
            }
        });
    auto bytes = normalizeToBytes(data, 3);
    GLuint tex; glGenTextures(1, &tex);
    glBindTexture(GL_TEXTURE_3D, tex);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    glTexImage3D(GL_TEXTURE_3D, 0, GL_RGB8, N, N, N, 0, GL_RGB, GL_UNSIGNED_BYTE, bytes.data());
    setTexParams(GL_TEXTURE_3D);
    return tex;
}

// Weather map: R = coverage, G = cloud type (0 stratus .. 1 cumulus)
static GLuint makeWeatherTexture(int N) {
    using namespace noise;
    WorleyFbm wC(3, 301);
    std::vector<float> data((size_t)N * N * 2);
    parallelFor(N, [&](int y) {
        for (int x = 0; x < N; ++x) {
            float u = (x + 0.5f) / N, v = (y + 0.5f) / N;
            float p = perlinFbm(u, v, 0.37f, 3, 5, 201) * 0.5f + 0.5f;
            float wf = wC.eval(u, v, 0.37f);
            size_t i = ((size_t)y * N + x) * 2;
            data[i + 0] = wf * 0.45f + p * 0.55f;
            data[i + 1] = perlinFbm(u, v, 0.71f, 2, 3, 401);
        }
        });
    auto bytes = normalizeToBytes(data, 2);
    GLuint tex; glGenTextures(1, &tex);
    glBindTexture(GL_TEXTURE_2D, tex);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RG8, N, N, 0, GL_RG, GL_UNSIGNED_BYTE, bytes.data());
    setTexParams(GL_TEXTURE_2D);
    return tex;
}

// -------------------------------------------------------------------------------------
//  Shaders
// -------------------------------------------------------------------------------------
static const char* kVertexShader = R"GLSL(
#version 330 core
// Full-screen triangle generated from gl_VertexID - no vertex buffer needed
void main() {
    vec2 p = vec2((gl_VertexID << 1) & 2, gl_VertexID & 2);
    gl_Position = vec4(p * 2.0 - 1.0, 0.0, 1.0);
}
)GLSL";

static const char* kFragmentShader = R"GLSL(
#version 330 core
out vec4 FragColor;

uniform sampler3D uShape;
uniform sampler3D uDetail;
uniform sampler2D uWeather;

uniform vec2  uResolution;
uniform float uTime;
uniform vec3  uCamPos;
uniform vec3  uCamFwd;
uniform vec3  uCamRight;
uniform vec3  uCamUp;
uniform float uTanHalfFov;
uniform vec3  uSunDir;        // normalized, pointing toward the sun
uniform float uCoverage;      // 0..1
uniform float uDensity;       // extinction per km at full density
uniform float uDetailStrength;

const float PI            = 3.14159265;
const float LAYER_BOTTOM  = 1.5;     // km
const float LAYER_TOP     = 4.0;     // km
const float MAX_DIST      = 35.0;    // km
const float SHAPE_SCALE   = 0.22;    // shape noise repeats every ~4.5 km
const float DETAIL_SCALE  = 1.6;
const float WEATHER_SCALE = 0.025;   // weather map repeats every 40 km
const vec3  WIND_DIR      = vec3(0.944, 0.0, 0.330);
const float WIND_SPEED    = 0.03;    // km/s

float remap(float v, float lo, float hi, float nlo, float nhi) {
    return nlo + (v - lo) * (nhi - nlo) / (hi - lo);
}

// Interleaved gradient noise - cheap per-pixel jitter that hides ray-march banding
float ign(vec2 p) {
    return fract(52.9829189 * fract(dot(p, vec2(0.06711056, 0.00583715))));
}

// Vertical density profile for a cloud type: 0 = stratus, 0.5 = stratocumulus, 1 = cumulus
float heightGradient(float h, float type) {
    float stratus  = smoothstep(0.0, 0.07, h) * (1.0 - smoothstep(0.12, 0.25, h));
    float stratocu = smoothstep(0.0, 0.12, h) * (1.0 - smoothstep(0.30, 0.55, h));
    float cumulus  = smoothstep(0.0, 0.15, h) * (1.0 - smoothstep(0.55, 0.95, h));
    float a = mix(stratus, stratocu, clamp(type * 2.0, 0.0, 1.0));
    return mix(a, cumulus, clamp(type * 2.0 - 1.0, 0.0, 1.0));
}

float cloudDensity(vec3 p, float lod, bool withDetail) {
    float h = (p.y - LAYER_BOTTOM) / (LAYER_TOP - LAYER_BOTTOM);
    if (h <= 0.0 || h >= 1.0) return 0.0;

    vec3 wind = WIND_DIR * WIND_SPEED * uTime;

    // Weather drifts slower than the noise, so clouds appear to evolve
    vec2 weather = textureLod(uWeather, (p.xz + wind.xz * 0.3) * WEATHER_SCALE, 0.0).rg;
    float cov  = clamp(uCoverage + (weather.r - 0.5) * 0.7, 0.0, 1.0);
    float type = weather.g;

    // Shear: cloud tops lean downwind
    vec3 sp = p + wind + WIND_DIR * h * 0.6;

    vec4 lf = textureLod(uShape, sp * SHAPE_SCALE, lod);
    float lowFbm = lf.g * 0.625 + lf.b * 0.25 + lf.a * 0.125;
    float shape = lf.r * 0.65 + lowFbm * 0.35;

    float d = shape * heightGradient(h, type) - (1.0 - cov);
    if (d <= 0.0) return 0.0;

    if (withDetail) {
        vec3 hf = textureLod(uDetail, sp * DETAIL_SCALE, lod).rgb;
        float hfFbm = hf.r * 0.625 + hf.g * 0.25 + hf.b * 0.125;
        // wispy at the base, billowy toward the top
        float hfMod = mix(hfFbm, 1.0 - hfFbm, clamp(h * 5.0, 0.0, 1.0));
        d = remap(d, hfMod * uDetailStrength, 1.0, 0.0, 1.0);
    }
    return max(d, 0.0) * uDensity;
}

// Henyey-Greenstein phase function
float hg(float c, float g) {
    float g2 = g * g;
    return (1.0 - g2) / (4.0 * PI * pow(max(1.0 + g2 - 2.0 * g * c, 1e-4), 1.5));
}
// Strong forward lobe (silver lining) + weaker back lobe
float phase(float c, float k) {
    return mix(hg(c, 0.8 * k), hg(c, -0.25 * k), 0.3);
}

// Optical depth from p toward the sun (march with growing steps)
float lightOpticalDepth(vec3 p, float lod) {
    float od = 0.0;
    float stepLen = 0.05;
    float traveled = 0.0;
    for (int i = 0; i < 6; ++i) {
        vec3 q = p + uSunDir * (traveled + stepLen * 0.5);
        if (q.y > LAYER_TOP || q.y < LAYER_BOTTOM) break;
        od += cloudDensity(q, lod + float(i) * 0.4, i < 2) * stepLen;
        traveled += stepLen;
        stepLen *= 1.7;
    }
    return od;
}

// Multiple-scattering approximation: sum of progressively softer, more transmissive octaves
float sunEnergy(float od, float cosTheta) {
    float a = 1.0, b = 1.0, k = 1.0, sum = 0.0;
    for (int i = 0; i < 4; ++i) {
        sum += a * exp(-od * b) * phase(cosTheta, k);
        a *= 0.5; b *= 0.4; k *= 0.6;
    }
    return sum;
}

vec3 sunColor() {
    float e = uSunDir.y;
    vec3 c = mix(vec3(1.0, 0.45, 0.2), vec3(1.0, 0.96, 0.9), smoothstep(-0.05, 0.35, e));
    return c * 14.0 * smoothstep(-0.1, 0.05, e);
}
vec3 skyZenith()  { return mix(vec3(0.05, 0.08, 0.18), vec3(0.22, 0.42, 0.85), smoothstep(-0.05, 0.3, uSunDir.y)); }
vec3 skyHorizon() { return mix(vec3(0.85, 0.45, 0.28), vec3(0.75, 0.85, 0.98), smoothstep(-0.05, 0.3, uSunDir.y)); }

vec3 skyColor(vec3 rd, bool withSun) {
    vec3 zen = skyZenith(), hor = skyHorizon();
    vec3 col;
    if (rd.y >= 0.0) {
        col = mix(hor, zen, pow(rd.y, 0.45));
    } else {
        vec3 ground = vec3(0.16, 0.18, 0.15) * (0.25 + 0.75 * clamp(uSunDir.y * 2.0, 0.0, 1.0));
        col = mix(hor, ground, 1.0 - exp(rd.y * 30.0));
    }
    if (withSun) {
        float sd = max(dot(rd, uSunDir), 0.0);
        vec3 sc = sunColor();
        col += sc * pow(sd, 12.0) * 0.03;                                    // glow
        col += sc * smoothstep(0.99955, 0.9997, sd) * 4.0 * step(0.0, rd.y); // disk
    }
    return col;
}

bool raySlab(vec3 ro, vec3 rd, out float t0, out float t1) {
    t0 = 0.0; t1 = 0.0;
    if (abs(rd.y) < 1e-6) {
        if (ro.y < LAYER_BOTTOM || ro.y > LAYER_TOP) return false;
        t1 = MAX_DIST;
        return true;
    }
    float ta = (LAYER_BOTTOM - ro.y) / rd.y;
    float tb = (LAYER_TOP    - ro.y) / rd.y;
    t0 = max(min(ta, tb), 0.0);
    t1 = min(max(ta, tb), MAX_DIST);
    return t1 > t0;
}

vec3 aces(vec3 x) {
    return clamp((x * (2.51 * x + 0.03)) / (x * (2.43 * x + 0.59) + 0.14), 0.0, 1.0);
}

void main() {
    vec2 uv = (gl_FragCoord.xy / uResolution) * 2.0 - 1.0;
    float aspect = uResolution.x / uResolution.y;
    vec3 rd = normalize(uCamFwd + uv.x * aspect * uTanHalfFov * uCamRight + uv.y * uTanHalfFov * uCamUp);
    vec3 ro = uCamPos;

    vec3 sky = skyColor(rd, true);
    vec3 col = sky;
    float jitter = ign(gl_FragCoord.xy);

    float t0, t1;
    if (raySlab(ro, rd, t0, t1)) {
        float cosTheta = dot(rd, uSunDir);
        vec3 sc  = sunColor();
        vec3 zen = skyZenith(), hor = skyHorizon();
        float powderWeight = 0.6 * (0.5 - 0.5 * cosTheta); // powder only when looking away from sun

        float T = 1.0;           // transmittance along view ray
        vec3  L = vec3(0.0);     // in-scattered light
        float hitSum = 0.0, hitW = 0.0;

        float t = t0 + (0.04 + t0 * 0.012) * jitter;
        for (int i = 0; i < 256; ++i) {
            if (t >= t1 || T < 0.01) break;
            float dt = 0.04 + t * 0.012;                      // step grows with distance
            vec3 p = ro + rd * t;
            float lod = clamp(log2(1.0 + t * 0.3), 0.0, 3.0); // blurrier noise far away

            float dens = cloudDensity(p, lod, true);
            if (dens > 0.0) {
                float od = lightOpticalDepth(p, lod);
                float h  = (p.y - LAYER_BOTTOM) / (LAYER_TOP - LAYER_BOTTOM);

                vec3 ambient = mix(hor * 0.3 + zen * 0.1, zen * 0.9 + hor * 0.3, h);
                float powder = 1.0 - exp(-od * 2.0);
                vec3 lum = sc * sunEnergy(od, cosTheta) * mix(1.0, powder, powderWeight) + ambient;

                float Tr = exp(-dens * dt);
                float absorbed = T * (1.0 - Tr);
                L += lum * absorbed;          // energy-conserving integration (albedo = 1)
                hitSum += t * absorbed;
                hitW   += absorbed;
                T *= Tr;
            }
            t += dt;
        }

        // Aerial perspective: distant clouds fade into the sky
        float dist = hitW > 0.0 ? hitSum / hitW : t1;
        float fade = exp(-dist * 0.035) * (1.0 - smoothstep(MAX_DIST * 0.6, MAX_DIST, dist));
        vec3 haze  = skyColor(rd, false) * (1.0 - T);
        col = sky * T + mix(haze, L, fade);
    }

    col = aces(col * 0.8);
    col = pow(col, vec3(1.0 / 2.2));
    col += (jitter - 0.5) / 255.0;    // dither to avoid 8-bit banding in the sky
    FragColor = vec4(col, 1.0);
}
)GLSL";

static GLuint compileShader(GLenum type, const char* src) {
    GLuint s = glCreateShader(type);
    glShaderSource(s, 1, &src, nullptr);
    glCompileShader(s);
    GLint ok; glGetShaderiv(s, GL_COMPILE_STATUS, &ok);
    if (!ok) {
        char log[4096]; glGetShaderInfoLog(s, sizeof(log), nullptr, log);
        std::cerr << (type == GL_VERTEX_SHADER ? "Vertex" : "Fragment") << " shader error:\n" << log << "\n";
        glDeleteShader(s);
        return 0;
    }
    return s;
}

static GLuint createProgram(const char* vs, const char* fs) {
    GLuint v = compileShader(GL_VERTEX_SHADER, vs);
    GLuint f = compileShader(GL_FRAGMENT_SHADER, fs);
    if (!v || !f) return 0;
    GLuint p = glCreateProgram();
    glAttachShader(p, v); glAttachShader(p, f);
    glLinkProgram(p);
    glDeleteShader(v); glDeleteShader(f);
    GLint ok; glGetProgramiv(p, GL_LINK_STATUS, &ok);
    if (!ok) {
        char log[4096]; glGetProgramInfoLog(p, sizeof(log), nullptr, log);
        std::cerr << "Link error:\n" << log << "\n";
        return 0;
    }
    return p;
}

// -------------------------------------------------------------------------------------
//  Application state & input
// -------------------------------------------------------------------------------------
struct AppState {
    Vec3  camPos{ 0.0f, 0.4f, 0.0f };
    float yaw = 0.0f, pitch = 0.3f;     // radians
    float sunElev = 0.30f, sunAzim = 0.6f;
    float coverage = 0.55f, density = 30.0f, detail = 0.18f;
    bool  halfRes = true, paused = false;
    float cloudTime = 0.0f;
    bool  dragging = false;
    double lastMX = 0.0, lastMY = 0.0;
    bool  prevR = false, prevP = false;
} g;

static Vec3 camForward() {
    return { std::sin(g.yaw) * std::cos(g.pitch), std::sin(g.pitch), -std::cos(g.yaw) * std::cos(g.pitch) };
}

void framebuffer_size_callback(GLFWwindow* window, int width, int height);
void process_input(GLFWwindow* window, float dt);

// Off-screen target so we can render at reduced resolution and upscale
static GLuint gFbo = 0, gFboTex = 0;
static int gFboW = 0, gFboH = 0;
static void ensureFbo(int w, int h) {
    if (gFbo && w == gFboW && h == gFboH) return;
    if (!gFbo) { glGenFramebuffers(1, &gFbo); glGenTextures(1, &gFboTex); }
    glBindTexture(GL_TEXTURE_2D, gFboTex);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glBindFramebuffer(GL_FRAMEBUFFER, gFbo);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, gFboTex, 0);
    if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE)
        std::cerr << "Framebuffer incomplete!\n";
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    gFboW = w; gFboH = h;
}

int main() {
    if (!glfwInit()) return -1;
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
#ifdef __APPLE__
    glfwWindowHint(GLFW_OPENGL_FORWARD_COMPAT, GL_TRUE);
#endif

    GLFWwindow* window = glfwCreateWindow(1280, 720, "Volumetric Clouds", nullptr, nullptr);
    if (!window) { glfwTerminate(); return -1; }
    glfwMakeContextCurrent(window);
    glfwSetFramebufferSizeCallback(window, framebuffer_size_callback);
    glfwSwapInterval(1);

    if (!gladLoadGLLoader((GLADloadproc)glfwGetProcAddress)) {
        std::cout << "Failed to init GLAD\n";
        return -1;
    }
    std::cout << "GL: " << glGetString(GL_VERSION) << "  |  " << glGetString(GL_RENDERER) << "\n";

    // ---- Noise textures ----
    std::cout << "Generating cloud noise textures (use Release build for speed)...\n";
    auto tStart = std::chrono::steady_clock::now();
    GLuint shapeTex = makeShapeTexture(128);
    GLuint detailTex = makeDetailTexture(64);
    GLuint weatherTex = makeWeatherTexture(256);
    double secs = std::chrono::duration<double>(std::chrono::steady_clock::now() - tStart).count();
    std::cout << "Done in " << secs << " s\n";

    // ---- Shader program ----
    GLuint prog = createProgram(kVertexShader, kFragmentShader);
    if (!prog) { glfwTerminate(); return -1; }

    GLuint vao; glGenVertexArrays(1, &vao);   // core profile requires a bound VAO

    glUseProgram(prog);
    glUniform1i(glGetUniformLocation(prog, "uShape"), 0);
    glUniform1i(glGetUniformLocation(prog, "uDetail"), 1);
    glUniform1i(glGetUniformLocation(prog, "uWeather"), 2);
    GLint locRes = glGetUniformLocation(prog, "uResolution");
    GLint locTime = glGetUniformLocation(prog, "uTime");
    GLint locCamPos = glGetUniformLocation(prog, "uCamPos");
    GLint locFwd = glGetUniformLocation(prog, "uCamFwd");
    GLint locRight = glGetUniformLocation(prog, "uCamRight");
    GLint locUp = glGetUniformLocation(prog, "uCamUp");
    GLint locFov = glGetUniformLocation(prog, "uTanHalfFov");
    GLint locSun = glGetUniformLocation(prog, "uSunDir");
    GLint locCoverage = glGetUniformLocation(prog, "uCoverage");
    GLint locDensity = glGetUniformLocation(prog, "uDensity");
    GLint locDetail = glGetUniformLocation(prog, "uDetailStrength");

    const float fovY = 60.0f * 3.14159265f / 180.0f;
    double lastTime = glfwGetTime(), titleTimer = 0.0;
    int frames = 0;

    while (!glfwWindowShouldClose(window)) {
        double now = glfwGetTime();
        float dt = (float)(now - lastTime);
        lastTime = now;

        glfwPollEvents();
        process_input(window, dt);
        if (!g.paused) g.cloudTime += dt;

        int fbw, fbh;
        glfwGetFramebufferSize(window, &fbw, &fbh);
        if (fbw == 0 || fbh == 0) { glfwWaitEvents(); continue; }  // minimized

        float scale = g.halfRes ? 0.5f : 1.0f;
        int rw = std::max(1, (int)(fbw * scale)), rh = std::max(1, (int)(fbh * scale));
        ensureFbo(rw, rh);

        // Camera basis
        Vec3 fwd = camForward();
        Vec3 right = normalize(cross(fwd, { 0.0f, 1.0f, 0.0f }));
        Vec3 up = cross(right, fwd);
        Vec3 sun = normalize({ std::cos(g.sunElev) * std::sin(g.sunAzim),
                                 std::sin(g.sunElev),
                                -std::cos(g.sunElev) * std::cos(g.sunAzim) });

        // ---- Render clouds into the off-screen buffer ----
        glBindFramebuffer(GL_FRAMEBUFFER, gFbo);
        glViewport(0, 0, rw, rh);
        glUseProgram(prog);
        glUniform2f(locRes, (float)rw, (float)rh);
        glUniform1f(locTime, g.cloudTime);
        glUniform3f(locCamPos, g.camPos.x, g.camPos.y, g.camPos.z);
        glUniform3f(locFwd, fwd.x, fwd.y, fwd.z);
        glUniform3f(locRight, right.x, right.y, right.z);
        glUniform3f(locUp, up.x, up.y, up.z);
        glUniform1f(locFov, std::tan(fovY * 0.5f));
        glUniform3f(locSun, sun.x, sun.y, sun.z);
        glUniform1f(locCoverage, g.coverage);
        glUniform1f(locDensity, g.density);
        glUniform1f(locDetail, g.detail);

        glActiveTexture(GL_TEXTURE0); glBindTexture(GL_TEXTURE_3D, shapeTex);
        glActiveTexture(GL_TEXTURE1); glBindTexture(GL_TEXTURE_3D, detailTex);
        glActiveTexture(GL_TEXTURE2); glBindTexture(GL_TEXTURE_2D, weatherTex);

        glBindVertexArray(vao);
        glDrawArrays(GL_TRIANGLES, 0, 3);

        // ---- Upscale to the window ----
        glBindFramebuffer(GL_READ_FRAMEBUFFER, gFbo);
        glBindFramebuffer(GL_DRAW_FRAMEBUFFER, 0);
        glBlitFramebuffer(0, 0, rw, rh, 0, 0, fbw, fbh, GL_COLOR_BUFFER_BIT, GL_LINEAR);
        glBindFramebuffer(GL_FRAMEBUFFER, 0);

        glfwSwapBuffers(window);

        // ---- Stats in the title bar ----
        frames++;
        titleTimer += dt;
        if (titleTimer >= 0.5) {
            char title[256];
            std::snprintf(title, sizeof(title),
                "Volumetric Clouds | %.1f fps | %s | coverage %.2f  density %.0f  detail %.2f | alt %.2f km",
                frames / titleTimer, g.halfRes ? "half-res" : "full-res",
                g.coverage, g.density, g.detail, g.camPos.y);
            glfwSetWindowTitle(window, title);
            frames = 0; titleTimer = 0.0;
        }
    }

    glDeleteProgram(prog);
    glDeleteVertexArrays(1, &vao);
    glDeleteTextures(1, &shapeTex);
    glDeleteTextures(1, &detailTex);
    glDeleteTextures(1, &weatherTex);
    if (gFbo) { glDeleteFramebuffers(1, &gFbo); glDeleteTextures(1, &gFboTex); }
    glfwTerminate();
    return 0;
}

void framebuffer_size_callback(GLFWwindow* window, int width, int height) {
    (void)window;
    glViewport(0, 0, width, height);
}

void process_input(GLFWwindow* window, float dt) {
    auto down = [&](int key) { return glfwGetKey(window, key) == GLFW_PRESS; };

    if (down(GLFW_KEY_ESCAPE)) glfwSetWindowShouldClose(window, true);

    // Mouse look while left button held
    double mx, my;
    glfwGetCursorPos(window, &mx, &my);
    if (glfwGetMouseButton(window, GLFW_MOUSE_BUTTON_LEFT) == GLFW_PRESS) {
        if (g.dragging) {
            g.yaw += (float)(mx - g.lastMX) * 0.003f;
            g.pitch -= (float)(my - g.lastMY) * 0.003f;
            g.pitch = std::clamp(g.pitch, -1.5f, 1.5f);
        }
        g.dragging = true;
    }
    else {
        g.dragging = false;
    }
    g.lastMX = mx; g.lastMY = my;

    // Fly camera (km per second)
    float speed = (down(GLFW_KEY_LEFT_SHIFT) ? 3.0f : 0.4f) * dt;
    Vec3 fwd = camForward();
    Vec3 right = normalize(cross(fwd, { 0.0f, 1.0f, 0.0f }));
    if (down(GLFW_KEY_W)) g.camPos = g.camPos + fwd * speed;
    if (down(GLFW_KEY_S)) g.camPos = g.camPos - fwd * speed;
    if (down(GLFW_KEY_D)) g.camPos = g.camPos + right * speed;
    if (down(GLFW_KEY_A)) g.camPos = g.camPos - right * speed;
    if (down(GLFW_KEY_E)) g.camPos.y += speed;
    if (down(GLFW_KEY_Q)) g.camPos.y -= speed;
    g.camPos.y = std::max(g.camPos.y, 0.02f);

    // Sun
    if (down(GLFW_KEY_UP))    g.sunElev += 0.4f * dt;
    if (down(GLFW_KEY_DOWN))  g.sunElev -= 0.4f * dt;
    if (down(GLFW_KEY_RIGHT)) g.sunAzim += 0.6f * dt;
    if (down(GLFW_KEY_LEFT))  g.sunAzim -= 0.6f * dt;
    g.sunElev = std::clamp(g.sunElev, -0.15f, 1.55f);

    // Cloud parameters
    if (down(GLFW_KEY_X)) g.coverage += 0.25f * dt;
    if (down(GLFW_KEY_Z)) g.coverage -= 0.25f * dt;
    if (down(GLFW_KEY_V)) g.density *= 1.0f + dt;
    if (down(GLFW_KEY_C)) g.density /= 1.0f + dt;
    if (down(GLFW_KEY_N)) g.detail += 0.2f * dt;
    if (down(GLFW_KEY_B)) g.detail -= 0.2f * dt;
    g.coverage = std::clamp(g.coverage, 0.0f, 1.0f);
    g.density = std::clamp(g.density, 2.0f, 300.0f);
    g.detail = std::clamp(g.detail, 0.0f, 0.6f);

    // Toggles (on key press, not hold)
    bool r = down(GLFW_KEY_R), p = down(GLFW_KEY_P);
    if (r && !g.prevR) g.halfRes = !g.halfRes;
    if (p && !g.prevP) g.paused = !g.paused;
    g.prevR = r; g.prevP = p;
}