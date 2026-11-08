#version 330 core
#define PI 3.14159265359

out vec4 fragColor;

uniform vec2 iResolution;
uniform int frame;

const vec3  CAMERA_DIR           = vec3(0.0, 0.5, -1.0);
const vec3  CAMERA_UP            = vec3(0.0, 1.0, 0.0);
const float TAN_HALF_FOV         = 0.57735026918962576; // tan(30°)

const vec3  SUN_DIR              = vec3(0.0, 0.8, -0.988);
const vec3  SUN_COLOR            = vec3(1.0, 1.0, 1.0);
const float SUN_INTENSITY        = 12.0;

const float PLANET_RADIUS        = 6375.0;
const float ATMOSPHERE_RADIUS    = 6560.0;

const vec3  PLANET_CENTER        = vec3(0.0, -(PLANET_RADIUS + 0.1), 0.0);

const vec3  BETA_RAYLEIGH         = vec3(5.8e-3, 1.35e-2, 3.31e-2);
const float RAYLEIGH_SCALE_HEIGHT = 8.0;
const vec3  BETA_MIE              = vec3(4.0e-3, 4.0e-3, 4.0e-3);
const float MIE_SCALE_HEIGHT      = 1.2;
const float MIE_G                 = 0.758;
const float MIE_EXTINCTION_SCALE  = 1.1;

const vec3  GROUND_ALBEDO         = vec3(0.20, 0.18, 0.15);

const float EXPOSURE              = 1.5;

const int VIEW_SAMPLES   = 12;
const int LIGHT_SAMPLES  = 6;
const int MS_STEPS       = 2;
const int MS_INTEGRATION = 3;

uint hash_u(uint x) {
    x ^= x >> 16; x *= 0x7feb352du;
    x ^= x >> 15; x *= 0x846ca68bu;
    x ^= x >> 16; return x;
}
float rand01(vec2 p, int frame) {
    uint s = hash_u(uint(p.x) + 0x9e3779b9u * hash_u(uint(p.y) + 0x9e3779b9u * uint(frame)));
    return float(s) / 4294967295.0;
}

bool intersectSphere(vec3 ro, vec3 rd, vec3 center, float R, out float t0, out float t1) {
    vec3  oc = ro - center;
    float b  = dot(rd, oc);
    float d  = length(oc);
    float h  = d - R;
    float c  = h * (d + R);
    float disc = b * b - c;
    if (disc < 0.0) return false;
    float s   = sqrt(disc);
    float t1p = -b + s;
    if (t1p < 0.0) return false;
    float t0p = (abs(t1p) > 1e-6) ? (c / t1p) : (-b - s);
    t0 = t0p;
    t1 = t1p;
    return true;
}

float phaseRayleigh(float c) {
    return (3.0 / (16.0 * PI)) * (1.0 + c * c);
}

float phaseMie(float g, float c) {
    float g2 = g * g;
    float denom = max(1.0 + g2 - 2.0 * g * c, 1e-4);
    return (3.0 / (8.0 * PI)) * ((1.0 - g2) * (1.0 + c * c))
         / ((2.0 + g2) * denom * sqrt(denom));
}

void integrateOpticalDepth(vec3 ro, vec3 rd, float t0, float t1, int N, float jitter,
                           out float odR, out float odM) {
    odR = 0.0; odM = 0.0;
    float len = t1 - t0;
    if (len <= 0.0) return;
    for (int i = 0; i < N; i++) {
        float u0 = (float(i)     + jitter) / float(N);
        float u1 = (float(i + 1) + jitter) / float(N);
        float s0 = t0 + len * u0 * u0 * u0;
        float s1 = t0 + len * u1 * u1 * u1;
        float ds = s1 - s0;
        float sm = 0.5 * (s0 + s1);
        vec3  p  = ro + rd * sm;
        float h  = length(p - PLANET_CENTER) - PLANET_RADIUS;
        h = max(h, 0.0);
        odR += exp(-h / RAYLEIGH_SCALE_HEIGHT) * ds;
        odM += exp(-h / MIE_SCALE_HEIGHT)     * ds;
    }
}

vec3 multiScatterApprox(vec3 p, float h) {
    if (h <= 0.0) return vec3(0.0);
    vec3 up = normalize(p - PLANET_CENTER);
    float tU0, tU1;
    if (!intersectSphere(p, up, PLANET_CENTER, ATMOSPHERE_RADIUS, tU0, tU1)) return vec3(0.0);
    float uLen = max(tU1, 0.0);
    if (uLen <= 1e-4) return vec3(0.0);

    vec3 sunDir = normalize(SUN_DIR);
    vec3 L = vec3(0.0);

    for (int k = 0; k < MS_STEPS; k++) {
        float uk  = (float(k) + 0.5) / float(MS_STEPS);
        float sk  = uLen * uk * uk;
        float dsk = uLen * (
            pow((float(k) + 1.0) / float(MS_STEPS), 2.0) -
            pow( float(k)        / float(MS_STEPS), 2.0));

        vec3  q  = p + up * sk;
        float hq = length(q - PLANET_CENTER) - PLANET_RADIUS;
        if (hq <= 0.0) continue;

        float tqG0, tqG1;
        if (intersectSphere(q, sunDir, PLANET_CENTER, PLANET_RADIUS, tqG0, tqG1) && tqG0 > 0.0) continue;

        float tS0, tS1;
        if (!intersectSphere(q, sunDir, PLANET_CENTER, ATMOSPHERE_RADIUS, tS0, tS1)) continue;
        float sStart = max(tS0, 0.0);
        float sLen   = tS1 - sStart;
        float ldR, ldM;
        integrateOpticalDepth(q + sunDir * sStart, sunDir, 0.0, sLen, MS_INTEGRATION, 0.5, ldR, ldM);
        vec3 qAtt = exp(-(BETA_RAYLEIGH * ldR + BETA_MIE * MIE_EXTINCTION_SCALE * ldM));

        float pR, pM;
        integrateOpticalDepth(p, up, 0.0, sk, MS_INTEGRATION, 0.5, pR, pM);
        vec3 pAtt = exp(-(BETA_RAYLEIGH * pR + BETA_MIE * MIE_EXTINCTION_SCALE * pM));

        float qDensity = exp(-hq / RAYLEIGH_SCALE_HEIGHT);
        L += qDensity * qAtt * pAtt * dsk;
    }

    vec3 beta = BETA_RAYLEIGH + BETA_MIE;
    return SUN_INTENSITY * SUN_COLOR * beta * L / (4.0 * PI);
}


void main() {
    vec3 camR = normalize(cross(CAMERA_DIR, CAMERA_UP));
    vec3 camU = cross(camR, CAMERA_DIR);

    vec2 ndc = (gl_FragCoord.xy * 2.0 - iResolution.xy) / iResolution.y;
    vec3 rd = normalize(CAMERA_DIR + ndc.x * TAN_HALF_FOV * camR + ndc.y * TAN_HALF_FOV * camU);
    vec3 ro = vec3(0.0);

    vec3  sunDir = normalize(SUN_DIR);

    vec2  seed   = gl_FragCoord.xy;
    float jitter = rand01(seed, 0);

    float tAtm0, tAtm1;
    if (!intersectSphere(ro, rd, PLANET_CENTER, ATMOSPHERE_RADIUS, tAtm0, tAtm1)) {
        fragColor = vec4(0.0, 0.0, 0.0, 1.0);
        return;
    }
    tAtm0 = max(tAtm0, 0.0);

    float tG0, tG1;
    bool hitGround = intersectSphere(ro, rd, PLANET_CENTER, PLANET_RADIUS, tG0, tG1);
    float tMax = (hitGround && tG0 > 1e-4) ? tG0 : tAtm1;
    float tMin = tAtm0;
    float len  = max(tMax - tMin, 1e-4);

    vec3  accumR  = vec3(0.0);
    vec3  accumM  = vec3(0.0);
    vec3  accumMS = vec3(0.0);
    float odR = 0.0;
    float odM = 0.0;

    for (int i = 0; i < VIEW_SAMPLES; i++) {
        float u0 = (float(i)     + jitter) / float(VIEW_SAMPLES);
        float u1 = (float(i + 1) + jitter) / float(VIEW_SAMPLES);
        float s0 = tMin + len * u0 * u0 * u0;
        float s1 = tMin + len * u1 * u1 * u1;
        float ds = s1 - s0;
        float sm = 0.5 * (s0 + s1);

        vec3  p  = ro + rd * sm;
        float h  = max(length(p - PLANET_CENTER) - PLANET_RADIUS, 0.0);

        float dzR = exp(-h / RAYLEIGH_SCALE_HEIGHT) * ds;
        float dzM = exp(-h / MIE_SCALE_HEIGHT)     * ds;

        odR += dzR;
        odM += dzM;

        vec3 viewAtt = exp(-(BETA_RAYLEIGH * odR + BETA_MIE * MIE_EXTINCTION_SCALE * odM));

        float tSG0, tSG1;
        bool sunBlocked = intersectSphere(p, sunDir, PLANET_CENTER, PLANET_RADIUS, tSG0, tSG1) && tSG0 > 0.0;

        if (!sunBlocked) {
            float tS0, tS1;
            if (intersectSphere(p, sunDir, PLANET_CENTER, ATMOSPHERE_RADIUS, tS0, tS1)) {
                float sStart = max(tS0, 0.0);
                float sLen   = tS1 - sStart;
                float jitS   = rand01(seed + vec2(1.7, 2.3), i);
                float ldR, ldM;
                integrateOpticalDepth(p + sunDir * sStart, sunDir, 0.0, sLen,
                                      LIGHT_SAMPLES, jitS, ldR, ldM);

                vec3 tau = BETA_RAYLEIGH * (odR + ldR)
                         + BETA_MIE * MIE_EXTINCTION_SCALE * (odM + ldM);
                vec3 att = exp(-tau);

                accumR += dzR * att;
                accumM += dzM * att;
            }
        }

        if (sunBlocked) {
            vec3 L_inc = multiScatterApprox(p, h);
            vec3 beta  = BETA_RAYLEIGH + BETA_MIE;
            accumMS += dzR * beta / (4.0 * PI) * L_inc * viewAtt;
        }
    }

    float cosT   = dot(rd, sunDir);
    float phaseR = phaseRayleigh(cosT);
    float phaseM = phaseMie(MIE_G, cosT);

    vec3 color = SUN_INTENSITY * SUN_COLOR
               * (accumR * BETA_RAYLEIGH * phaseR
                + accumM * BETA_MIE * phaseM)
               + accumMS;

    if (hitGround && tG0 > 1e-4) {
        vec3 gP = ro + rd * tG0;
        vec3 n  = normalize(gP - PLANET_CENTER);

        float tGG0, tGG1;
        bool gBlocked = intersectSphere(gP, sunDir, PLANET_CENTER, PLANET_RADIUS, tGG0, tGG1) && tGG0 > 0.0;

        vec3 sunAtGround = vec3(0.0);
        if (!gBlocked) {
            float tGS0, tGS1;
            if (intersectSphere(gP, sunDir, PLANET_CENTER, ATMOSPHERE_RADIUS, tGS0, tGS1)) {
                float gStart = max(tGS0, 0.0);
                float gLen   = tGS1 - gStart;
                float jitG   = rand01(seed + vec2(3.1, 4.7), 99);
                float gldR, gldM;
                integrateOpticalDepth(gP + sunDir * gStart, sunDir, 0.0, gLen,
                                      LIGHT_SAMPLES, jitG, gldR, gldM);
                vec3 gTau = BETA_RAYLEIGH * gldR + BETA_MIE * MIE_EXTINCTION_SCALE * gldM;
                sunAtGround = SUN_INTENSITY * SUN_COLOR * exp(-gTau);
            }
        }

        float NdotL = max(dot(n, sunDir), 0.0);
        vec3  groundRad = GROUND_ALBEDO / PI * sunAtGround * NdotL;
        vec3  gViewTau  = BETA_RAYLEIGH * odR + BETA_MIE * MIE_EXTINCTION_SCALE * odM;
        color += groundRad * exp(-gViewTau);
    }

    const float SUN_ANGULAR_RADIUS = 0.01465;
    if (cosT > cos(SUN_ANGULAR_RADIUS)) {
        float tSD0, tSD1;
        if (intersectSphere(ro, rd, PLANET_CENTER, ATMOSPHERE_RADIUS, tSD0, tSD1)) {
            float sStart = max(tSD0, 0.0);
            float sLen   = tSD1 - sStart;
            float jitD   = rand01(seed + vec2(5.3, 6.1), 199);
            float sdR, sdM;
            integrateOpticalDepth(ro + rd * sStart, rd, 0.0, sLen,
                                  LIGHT_SAMPLES, jitD, sdR, sdM);
            vec3 sdTau = BETA_RAYLEIGH * sdR + BETA_MIE * MIE_EXTINCTION_SCALE * sdM;
            color += SUN_INTENSITY * SUN_COLOR * exp(-sdTau);
        }
    }

    vec3 mapped = color * EXPOSURE;
    mapped = 1.0 - exp(-max(mapped, vec3(0.0)));
    mapped = pow(clamp(mapped, 0.0, 1.0), vec3(1.0 / 2.2));

    fragColor = vec4(mapped, 1.0);
}
