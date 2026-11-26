#version 330 core
#define PI 3.14159265359

layout(location = 0) out vec4 fragColor;

uniform vec2 iResolution;
uniform int frame;
uniform int postprocess;

uniform vec3 CAMERA_POS;
uniform vec3 CAMERA_DIR;
uniform vec3 CAMERA_UP;
uniform vec3 CAMERA_RIGHT;

//Camera Parameters
const float TAN_HALF_FOV         = 0.57735026918962576; // tan(30°), half of the fov

//Post Process
const float EXPOSURE              = 2.3;

//Solar Parameters
const vec3  SUN_DIR              = normalize(vec3(0.290204, 0.09, -0.955741)); // 
const vec3  SUN_COLOR            = vec3(1.0, 1.0, 1.0);
const vec3  SUN_INTENSITY        = vec3(12.0);   // irradiance
const float SUN_ANGULAR_RADIUS   = 0.01465;      // angular radius of the disk
const float SUN_SOLID_ANGLE      = PI * SUN_ANGULAR_RADIUS * SUN_ANGULAR_RADIUS;
const vec3  SUN_RADIANCE         = SUN_INTENSITY / SUN_SOLID_ANGLE; // radiance seen by camera

//World Parameters
const float PLANET_RADIUS        = 6375.0;
const float ATMOSPHERE_RADIUS    = 6560.0;
const vec3  PLANET_CENTER        = vec3(0.0, -(PLANET_RADIUS + 0.1), 0.0);

//Cloud Parameters
const float HG_G = 0.85;
const float SIGMA_S = 0.7;
const float SIGMA_A = 0.2;
const vec3  CLOUD_CENTER = vec3(0.0, 3.0, -10.0); 
const float CLOUD_RADIUS = 2.5;

//Atmosphere Parameters (km-1)
const vec3  BETA_RAYLEIGH         = vec3(5.8e-3, 1.35e-2, 3.31e-2);
const float RAYLEIGH_SCALE_HEIGHT = 8.0;
const vec3  BETA_MIE              = vec3(4.0e-3, 4.0e-3, 4.0e-3);
const float MIE_SCALE_HEIGHT      = 1.2;
const float MIE_G                 = 0.758;
const float MIE_EXTINCTION_SCALE  = 1.1;
const vec3  OZONE_ABSORPTION      = vec3(0.20556e-2, 0.49788e-2, 0.02136e-2);
const float OZONE_CENTER          = 25.0;
const float OZONE_HALF_WIDTH      = 15.0;

//Ground Parameters
const vec3  GROUND_ALBEDO         = vec3(0.20, 0.18, 0.15);

//Rendering Samples
const int ATM_SAMPLES   = 8;
const int CLOUD_SAMPLES   = 1;
const int ATM_CLOUD_SAMPLES = 8;
const int MS_STEPS       = 3;
const int MS_INTEGRATION = 3;

const float dt = 0.01;
const float SUN_CLOUD_DT = 0.1;


// == RNG stuff ==
uint pcg_hash(uint v) {
    uint state = v * 747796405u + 2891336453u;
    uint word  = ((state >> ((state >> 28u) + 4u)) ^ state) * 277803737u;
    return (word >> 22u) ^ word;
}

float rand(inout uint rng) {
    rng = pcg_hash(rng);
    return float(rng >> 8u) * (1.0 / 16777216.0);
}

uint rngSeed(uvec2 px, uint frameNum) {
    uint idx = px.y * uint(iResolution.x) + px.x;
    return pcg_hash(idx ^ pcg_hash(frameNum));
}


// == SDF stuff and other utils ==
float sdf_sphere(vec3 p, vec3 center, float rad) {
    return length(center - p) - rad;
}

float smin(float a, float b, float k) {
    k *= 6.0;
    float h = max( k-abs(a-b), 0.0 )/k;
    return min( a, b ) - h*h*h*k*(1.0/6.0);
}

//define cloud 
float map(vec3 p) {
    float s1 = sdf_sphere(p, CLOUD_CENTER, CLOUD_RADIUS / 1.3);
    float s2 = sdf_sphere(p, CLOUD_CENTER + vec3(0.2, 0.0, 0.0), CLOUD_RADIUS / 4.0);
    return s1;//smin(s1, s2, 0.05);
}

vec3 tonemap(vec3 color) {
    vec3 m = color * EXPOSURE;
    m = 1.0 - exp(-max(m, vec3(0.0)));
    return pow(clamp(m, 0.0, 1.0), vec3(1.0 / 2.2));
}

vec3 generateRay(vec2 fragCoord) {
    vec2 uv = (fragCoord * 2.0 - iResolution.xy) / iResolution.y;
    return normalize(CAMERA_DIR + uv.x * TAN_HALF_FOV * CAMERA_RIGHT
                                + uv.y * TAN_HALF_FOV * CAMERA_UP);
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

bool intersectScene(vec3 ro, vec3 rd,
                    out float tAtmo0,  out float tAtmo1,
                    out float tGround0, out float tGround1,
                    out bool  hitGround, inout uint rng)
{

    bool hitAtmo = intersectSphere(ro, rd, PLANET_CENTER,
                                   ATMOSPHERE_RADIUS, tAtmo0, tAtmo1);
    hitGround = hitAtmo &&
                intersectSphere(ro, rd, PLANET_CENTER,
                                PLANET_RADIUS, tGround0, tGround1);
    return hitAtmo;
}

// analytic entry/exit interval of the cloud volume
bool cloudInterval(vec3 ro, vec3 rd, out float t0, out float t1) {
    if (!intersectSphere(ro, rd, CLOUD_CENTER, CLOUD_RADIUS, t0, t1)) return false;
    t0 = max(t0, 0.0);
    return t1 > t0;
}

void basis(vec3 n, out vec3 t, out vec3 b) { // Claude generated
    float s = n.z >= 0.0 ? 1.0 : -1.0;
    float a = -1.0 / (s + n.z);
    float c = n.x * n.y * a;
    t = vec3(1.0 + s * n.x * n.x * a, s * c, -s * n.x);
    b = vec3(c, s + n.y * n.y * a, -n.y);
}

vec3 AtmosTransmittance(float odR, float odM, float odO) {
    return exp(-((BETA_RAYLEIGH) * odR + BETA_MIE * MIE_EXTINCTION_SCALE * odM + OZONE_ABSORPTION * odO));
}

float ozoneDensity(float h) {
    return max(0.0, 1.0 - abs(h - OZONE_CENTER) / OZONE_HALF_WIDTH);
}

float mu_t(vec3 pos) {
    if (map(pos) < 0.0) return SIGMA_A + SIGMA_S;
    return 0.0;
}

// == phase functions ==
float HG_phase(float g, float cos_t) {
    float denom = 1.0 + g * g - 2.0 * g * cos_t;
    return (1.0 - g * g) / (4.0 * PI * denom * sqrt(denom));
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

vec3 sample_HG_phase(vec3 dir, float g, inout uint rng) { // Claude generated, this is inversion sampling
    float u1 = rand(rng);
    float u2 = rand(rng);

    float cos_theta;
    if (abs(g) < 1e-3) {
        cos_theta = 1.0 - 2.0 * u1;
    } else {
        float sq = (1.0 - g * g) / (1.0 - g + 2.0 * g * u1);
        cos_theta = (1.0 + g * g - sq * sq) / (2.0 * g);
    }

    float sin_theta = sqrt(max(0.0, 1.0 - cos_theta * cos_theta));
    float phi = 6.2831853 * u2;

    vec3 t, b;
    basis(dir, t, b);
    return sin_theta * cos(phi) * t + sin_theta * sin(phi) * b + cos_theta * dir;
}

// == transmittance / optical depth calculations ==
// integrate optical depth for just Rayleigh and Mie
void integrateOpticalDepthAtmos(vec3 ro, vec3 rd, float t0, float t1, int N, float jitter,
                           out float odR, out float odM, out float odO) {
    odR = 0.0; odM = 0.0; odO = 0.0;
    float len = t1 - t0;
    if (len <= 0.0) return;
    for (int i = 0; i < N; i++) {
        float u0 = (float(i)     + jitter) / float(N);
        float u1 = min((float(i + 1) + jitter) / float(N), 1.0);
        float s0 = t0 + len * u0 * u0 * u0;
        float s1 = t0 + len * u1 * u1 * u1;
        float ds = s1 - s0;
        float sm = 0.5 * (s0 + s1);
        vec3  p  = ro + rd * sm;
        float h  = length(p - PLANET_CENTER) - PLANET_RADIUS;
        h = max(h, 0.0);
        odR += exp(-h / RAYLEIGH_SCALE_HEIGHT) * ds;
        odM += exp(-h / MIE_SCALE_HEIGHT)      * ds;
        odO += ozoneDensity(h)                 * ds;
    }
}

// integrate optical depth for Rayleigh, Mie, AND cloud
void integrateOpticalDepth(vec3 ro, vec3 rd, float t0, float t1, int N, float jitter,
                           out float odR, out float odM, out float odO, out float odC) {
    odR = 0.0; odM = 0.0; odC = 0.0;
    
    float len = t1 - t0;
    if (len <= 0.0) return;
    
    integrateOpticalDepthAtmos(ro, rd, t0, t1, N, jitter, odR, odM, odO);

    float ct0; float ct1; // if the cloud exists on the interval we give it a finer pass
    if (cloudInterval(ro, rd, ct0, ct1)) {

        if (ct0 < t0) ct0 = t0;
        if (ct1 > t1) ct1 = t1;

        float cLen = ct1 - ct0;
        for (float s = 0.0; s < cLen; s += dt) {
            float h = min(dt, cLen - s);
            vec3  p = ro + (ct0 + s + jitter * h) * rd;
            odC += mu_t(p) * h;
        }
    } // replace with ratio / delta tracking?
}

// riemman sum / raymarch
float integrateOpticalDepthCloud(vec3 x, vec3 y, inout uint rng) {
    vec3 d = y - x;
    float len = length(d);
    if (len < 1e-6) return 1.0;
    vec3 dir = d / len;

    float optical_depth = 0.0;

    for (float s = 0.0; s < len; s += dt) {
        float h = min(dt, len - s);
        optical_depth += mu_t(x + (s + rand(rng) * h) * dir) * h;
    }

    return optical_depth;
}

// transmittance from p to sun, including cloud
vec3 sunTransmittance(vec3 p, vec3 sunDir, float jitter) {
    float tG0, tG1;
    if (intersectSphere(p, sunDir, PLANET_CENTER, PLANET_RADIUS, tG0, tG1) && tG0 > 0.0) {
        return vec3(0.0);
    }
    float tS0, tS1;
    if (!intersectSphere(p, sunDir, PLANET_CENTER, ATMOSPHERE_RADIUS, tS0, tS1))
        return vec3(0.0);
    float sStart = max(tS0, 0.0);
    float sLen   = tS1 - sStart;
    float ldR, ldM, ldO, ldC;
    integrateOpticalDepth(p + sunDir * sStart, sunDir, 0.0, sLen,
                          ATM_SAMPLES, jitter, ldR, ldM, ldO, ldC);

    return AtmosTransmittance(ldR, ldM, ldO) * exp(-ldC);
}

// == Background stuff ==
// returns unattenuated radiance, path transmittance is applied by the caller
vec3 shadeGround(vec3 gP, vec3 n, vec3 sunDir, float jitter) {
    gP = gP + 1e-2 * n;
    vec3 sunAtt = sunTransmittance(gP, sunDir, jitter);
    float NdotL = max(dot(n, sunDir), 0.0);
    return GROUND_ALBEDO / PI * SUN_INTENSITY * SUN_COLOR * sunAtt * NdotL;
}
// returns unattenuated radiance, path transmittance is applied by the caller
vec3 shadeSunDisk(float cosT) {
    // cos is decreasing, so the inner edge is the larger one: keep edge0 < edge1
    float w = smoothstep(cos(SUN_ANGULAR_RADIUS), cos(SUN_ANGULAR_RADIUS * 0.9), cosT);
    if (w <= 0.0) return vec3(0.0);
    return SUN_RADIANCE * SUN_COLOR * w;
}

// == Atmosphere stuff ==
vec3 multiScatterApprox(vec3 p, float h) {
    if (h <= 0.0) return vec3(0.0);
    vec3 up = normalize(p - PLANET_CENTER);
    float tU0, tU1;
    if (!intersectSphere(p, up, PLANET_CENTER, ATMOSPHERE_RADIUS, tU0, tU1)) return vec3(0.0);
    float uLen = max(tU1, 0.0);
    if (uLen <= 1e-4) return vec3(0.0);

    vec3 L = vec3(0.0);

    for (int k = 0; k < MS_STEPS; k++) {
        float a0 = float(k)     / float(MS_STEPS);
        float a1 = float(k + 1) / float(MS_STEPS);
        float sk0 = uLen * a0 * a0;
        float sk1 = uLen * a1 * a1;
        float dsk = sk1 - sk0;
        float sk  = 0.5 * (sk0 + sk1);  

        vec3  q  = p + up * sk;
        float hq = length(q - PLANET_CENTER) - PLANET_RADIUS;
        if (hq <= 0.0) continue;

        float qg0, qg1;
        if (intersectSphere(q, SUN_DIR, PLANET_CENTER, PLANET_RADIUS, qg0, qg1) && qg0 > 0.0) continue;  

        float s0, s1;
        if (!intersectSphere(q, SUN_DIR, PLANET_CENTER, ATMOSPHERE_RADIUS, s0, s1)) continue;
        float sStart = max(s0, 0.0);
        float ldR, ldM, ldO;
        integrateOpticalDepthAtmos(q + SUN_DIR * sStart, SUN_DIR, 0.0, s1 - sStart,
                              MS_INTEGRATION, 0.5, ldR, ldM, ldO);
        vec3 T_sun = AtmosTransmittance(ldR, ldM, ldO);

        float pR, pM, pO;
        integrateOpticalDepthAtmos(q, -up, 0.0, sk, MS_INTEGRATION, 0.5, pR, pM, pO);
        vec3 T_qp = AtmosTransmittance(pR, pM, pO);

        float rhoR = exp(-hq / RAYLEIGH_SCALE_HEIGHT);
        float rhoM = exp(-hq / MIE_SCALE_HEIGHT);
        vec3  betaRho = BETA_RAYLEIGH * rhoR + BETA_MIE * rhoM;   // 不再用 betaR+betaM 配纯瑞利密度

        L += betaRho * T_sun * T_qp * dsk;
    }

    return SUN_INTENSITY * SUN_COLOR * L / (4.0 * PI);
}

void traceAtmosphereAndCloud(vec3 ro, vec3 rd,
                     float tMin, float tMax, 
                     float tCloud0, float tCloud1,
                     bool hasCloud,
                     inout uint rng,
                     out vec3 inscatterR,
                     out vec3 inscatterM,
                     out vec3 inscatterMS,
                     out float odR,
                     out float odM,
                     out float odO)
{
    inscatterR  = vec3(0.0);
    inscatterM  = vec3(0.0);
    inscatterMS = vec3(0.0);
    odR = 0.0;
    odM = 0.0;
    odO = 0.0;

    // first integrate from start to cloud

    float len = max(tCloud0 - tMin, 1e-4);

    for (int i = 0; i < ATM_SAMPLES; i++) {
        float jitter = rand(rng);
        float u0 = (float(i)     + jitter) / float(ATM_SAMPLES);
        float u1 = min((float(i + 1) + jitter) / float(ATM_SAMPLES), 1.0);
        float s0 = tMin + len * u0 * u0 * u0; // we are placing more samples near the camera with the assumption that atmos is denser there,
        float s1 = tMin + len * u1 * u1 * u1; // if cam moves higher this needs to change
        float ds = s1 - s0;
        float sm = 0.5 * (s0 + s1);

        vec3  p  = ro + rd * sm;
        float h  = max(length(p - PLANET_CENTER) - PLANET_RADIUS, 0.0);

        float dzR = exp(-h / RAYLEIGH_SCALE_HEIGHT) * ds;
        float dzM = exp(-h / MIE_SCALE_HEIGHT)      * ds;
        float dzO = ozoneDensity(h)                 * ds;

        vec3 atmosAtt = AtmosTransmittance(odR + 0.5 * dzR, odM + 0.5 * dzM, odO + 0.5 * dzO);

        odR += dzR;
        odM += dzM;

        float jitS = rand(rng);
        vec3 sunAtt = sunTransmittance(p, SUN_DIR, jitS);

        inscatterR += dzR * sunAtt * atmosAtt;
        inscatterM += dzM * sunAtt * atmosAtt;
        
        vec3 L_inc = multiScatterApprox(p, h);
        inscatterMS += (dzR * BETA_RAYLEIGH + dzM * BETA_MIE) * L_inc * atmosAtt;
    }

    // then integrate from cloud start to cloud end (more samples here)
    float odC = 0.0;

    if (hasCloud) {
        float cloud_len = max(tCloud1 - tCloud0, 1e-4);
        float step_size = cloud_len / float(ATM_CLOUD_SAMPLES);
        vec3 curr = ro + (tCloud0 + rand(rng) * step_size) * rd;

        for (int i = 0; i < ATM_CLOUD_SAMPLES; i++) {
            float h  = max(length(curr - PLANET_CENTER) - PLANET_RADIUS, 0.0);

            float dzR = exp(-h / RAYLEIGH_SCALE_HEIGHT) * step_size;
            float dzM = exp(-h / MIE_SCALE_HEIGHT)      * step_size;
            float dzO = ozoneDensity(h)                 * step_size;
            float dzC = mu_t(curr)                      * step_size;

            vec3 atmosAtt = AtmosTransmittance(odR + 0.5 * dzR, odM + 0.5 * dzM, odO + 0.5 * dzO);
            vec3 cloudAtt = vec3(exp(-(odC + 0.5 * dzC)));

            odR += dzR;
            odM += dzM;
            odC += dzC;

            float jitS = rand(rng);
            vec3 sunAtt = sunTransmittance(curr, SUN_DIR, jitS);

            inscatterR += dzR * sunAtt * atmosAtt * cloudAtt;
            inscatterM += dzM * sunAtt * atmosAtt * cloudAtt;
        
            vec3 L_inc = multiScatterApprox(curr, h);
            inscatterMS += (dzR * BETA_RAYLEIGH + dzM * BETA_MIE) * L_inc * atmosAtt * cloudAtt;

            curr = curr + step_size * rd;
        }
    }


    // finally integrate from cloud end to atmosphere end

    len = max(tMax - tCloud1, 1e-4);
    vec3 cloudAtt = vec3(exp(-odC));

    for (int i = 0; i < ATM_SAMPLES; i++) {
        float jitter = rand(rng);
        float u0 = (float(i)     + jitter) / float(ATM_SAMPLES);
        float u1 = min((float(i + 1) + jitter) / float(ATM_SAMPLES), 1.0);
        float s0 = tCloud1 + len * u0 * u0 * u0; // we are placing more samples near the camera with the assumption that atmos is denser there,
        float s1 = tCloud1 + len * u1 * u1 * u1; // if cam moves higher this needs to change
        float ds = s1 - s0;
        float sm = 0.5 * (s0 + s1);

        vec3  p  = ro + rd * sm;
        float h  = max(length(p - PLANET_CENTER) - PLANET_RADIUS, 0.0);

        float dzR = exp(-h / RAYLEIGH_SCALE_HEIGHT) * ds;
        float dzM = exp(-h / MIE_SCALE_HEIGHT)      * ds;
        float dzO = ozoneDensity(h)                 * ds;
 
        vec3 atmosAtt = AtmosTransmittance(odR + 0.5 * dzR, odM + 0.5 * dzM, odO + 0.5 * dzO);

        odR += dzR;
        odM += dzM;

        float jitS = rand(rng);
        vec3 sunAtt = sunTransmittance(p, SUN_DIR, jitS);

        inscatterR += dzR * sunAtt * atmosAtt * cloudAtt;
        inscatterM += dzM * sunAtt * atmosAtt * cloudAtt;
        
        vec3 L_inc = multiScatterApprox(p, h);
        inscatterMS += (dzR * BETA_RAYLEIGH + dzM * BETA_MIE) * L_inc * atmosAtt * cloudAtt;
    }
}

void traceAtmosphere(vec3 ro, vec3 rd,
                     float tMin, float tMax,
                     inout uint rng,
                     out vec3 inscatterR,
                     out vec3 inscatterM,
                     out vec3 inscatterMS,
                     out float odR,
                     out float odM,
                     out float odO)
{
    inscatterR  = vec3(0.0);
    inscatterM  = vec3(0.0);
    inscatterMS = vec3(0.0);
    odR = 0.0;
    odM = 0.0;
    odO = 0.0;


    float len = max(tMax - tMin, 1e-4);

    for (int i = 0; i < ATM_SAMPLES; i++) {
        float jitter = rand(rng);
        float u0 = (float(i)     + jitter) / float(ATM_SAMPLES);
        float u1 = min((float(i + 1) + jitter) / float(ATM_SAMPLES), 1.0);
        float s0 = tMin + len * u0 * u0 * u0; // we are placing more samples near the camera with the assumption that atmos is denser there,
        float s1 = tMin + len * u1 * u1 * u1; // if cam moves higher this needs to change
        float ds = s1 - s0;
        float sm = 0.5 * (s0 + s1);

        vec3  p  = ro + rd * sm;
        float h  = max(length(p - PLANET_CENTER) - PLANET_RADIUS, 0.0);

        float dzR = exp(-h / RAYLEIGH_SCALE_HEIGHT) * ds;
        float dzM = exp(-h / MIE_SCALE_HEIGHT)      * ds;
        float dzO = ozoneDensity(h)                 * ds;

        vec3 atmosAtt = AtmosTransmittance(odR + 0.5 * dzR, odM + 0.5 * dzM, odO + 0.5 * dzO);

        odR += dzR;
        odM += dzM;

        float jitS = rand(rng);
        vec3 sunAtt = sunTransmittance(p, SUN_DIR, jitS);

        inscatterR += dzR * sunAtt * atmosAtt;
        inscatterM += dzM * sunAtt * atmosAtt;
        
        vec3 L_inc = multiScatterApprox(p, h);
        inscatterMS += (dzR * BETA_RAYLEIGH + dzM * BETA_MIE) * L_inc * atmosAtt;
    }
}

void atmosRadTrans(vec3 ro, vec3 rd,
                     float tMin, float tMax,
                     float jitter,
                     inout uint rng, 
                     bool hasCloud,
                     float tCloud0, float tCloud1,
                     out vec3 T_atm, out vec3 atmRad
                     ) {

    vec3  aR, aM, aMS;
    float aOdR, aOdM, aOdO;

    if (hasCloud) {
        traceAtmosphereAndCloud(ro, rd, tMin, tMax, tCloud0, tCloud1, hasCloud, rng,
                    aR, aM, aMS, aOdR, aOdM, aOdO);
    } else {
        traceAtmosphere(ro, rd, tMin, tMax, rng,
                    aR, aM, aMS, aOdR, aOdM, aOdO);
    }

    T_atm = AtmosTransmittance(aOdR, aOdM, aOdO);

    float cosT   = dot(rd, SUN_DIR);
    atmRad    = SUN_INTENSITY * SUN_COLOR * (
                       aR * BETA_RAYLEIGH * phaseRayleigh(cosT) +
                       aM * BETA_MIE      * phaseMie(MIE_G, cosT)) + aMS;
}

// == Cloud stuff == 
// cloud radiance along the ray, see sampleClouds for the compositing transmittance
vec3 cloudRadiance(vec3 ro, vec3 rd, inout uint rng) {
    float t0, t1;
    if (!cloudInterval(ro, rd, t0, t1)) {
        return vec3(0.0);
    }

    vec3 x = ro + t0 * rd; // entry position
    vec3 col = vec3(0.0);
    // instead of recursion on radiance(), we use a loop approach with throughput storing the weights so far
    // basically we always mult the next radiance() call by  mu_s(y) / mu_t(y) so we store those and use them at the end
    vec3 throughput = vec3(1.0);

    float mu_bar = SIGMA_A + SIGMA_S;

    // to avoid stalling thread groups and the like we have a max depth, even though russian roulette early exits
    for (int depth = 0; depth < 256; depth++) {

        // the interval has to be re-derived every bounce, since rd changes after scattering
        float ta, tb;
        if (!cloudInterval(x, rd, ta, tb)) {
            break;
        }

        // tau is our free flight distance, ie how far we travel before an interaction
        // tau max is the maximum distance we can travel while still being in the volume
        float tau_max = tb - ta;
        
        // == delta tracking ==
        // with a constant density volume, finding tau is easy
        // but we need to numerically integrate for non-constant density. 
        // so we pretend we have constant density mu_bar > mu_t(x) for all x
        // but thats innacurate, so we randomly discard some of our collisions using
        // the difference in rates between mu_t and mu_bar, which gives us accurate results
        float tau = 0.0;
        bool collided = false;

        for (int i = 0; i < 4096; i++) { // again, max depth just in case
            tau -= log(1.0 - max(rand(rng), 1e-7)) / mu_bar;
            if (tau >= tau_max) break;
            if (rand(rng) < mu_t(x + (ta + tau) * rd) / mu_bar) {
                collided = true;
                break;
            }
        }

        if (!collided) {// No interaction during the whole distance
            if (depth == 0) break;
            // We escape the cloud, add in light from the atmosphere

            float vOdR; float vOdM; float vOdO;

            integrateOpticalDepthAtmos(x, rd, 0.0, tb, ATM_SAMPLES / 2 + 1, rand(rng), vOdR, vOdM, vOdO);
            vec3 T_atmos_in_cloud = AtmosTransmittance(vOdR, vOdM, vOdO);

            throughput *= T_atmos_in_cloud;

            float tAtmo0, tAtmo1, tGround0, tGround1;
            bool  hitGroundFlag;
            bool  hitAtmo = intersectScene(x, rd,
                                   tAtmo0, tAtmo1,
                                   tGround0, tGround1,
                                   hitGroundFlag, rng);

            float tMax = (hitGroundFlag && tGround0 > 1e-2) ? tGround0 : tAtmo1;

            vec3 T_atm; vec3 atmRad;
            atmosRadTrans(x, rd, tb, tMax, rand(rng), rng, false, 0.0, 0.0, T_atm, atmRad);


            vec3 bg = vec3(0.0);
            if (hitGroundFlag && tGround0 > 1e-2) {
                vec3 gP = x + rd * tGround0;
                vec3 n  = normalize(gP - PLANET_CENTER);
                bg += shadeGround(gP, n, SUN_DIR, rand(rng));
            }

            col += throughput * (atmRad + T_atm * bg);

            break;
        }

        vec3 y = x + (ta + tau) * rd; // interaction point

        // one SDF evaluation at the interaction point gives the scattering coefficient
        float sdf_y = map(y);
        float sigma_t = sdf_y < 0.0 ? (SIGMA_A + SIGMA_S) : 0.0;

        float vOdR; float vOdM; float vOdO;

        integrateOpticalDepthAtmos(x, rd, 0.0, ta + tau, ATM_SAMPLES / 2 + 1, rand(rng), vOdR, vOdM, vOdO);
        vec3 T_atmos_in_cloud = AtmosTransmittance(vOdR, vOdM, vOdO);

        // update throughput for scattering
        throughput *= (sdf_y < 0.0 ? SIGMA_S : 0.0) / max(sigma_t, 1e-6) * T_atmos_in_cloud;


        // == Next Event Estimation ==

        // We treat the sun as a directional light here as it should be basically infinitely far
        vec3 w_i = SUN_DIR;

        // exit point of the cloud along the sun direction, found analytically
        float ts0, ts1;
        vec3 y_exit = y;
        if (intersectSphere(y, w_i, CLOUD_CENTER, CLOUD_RADIUS, ts0, ts1)) {
            y_exit = y + max(ts1, 0.0) * w_i;
        }

        vec3 T_atmos_cloud = sunTransmittance(y, w_i, rand(rng));
        // with NEE we directly find the incoming light instead of waiting to randomly sample it
        col += throughput * SUN_INTENSITY * SUN_COLOR * HG_phase(HG_G, dot(rd, w_i)) * T_atmos_cloud;

        // here we do russian roulette with p = max(throughput)
        // this is because we dont want to skip paths with high "energy"
        // same reason we only do RR with depth > 3
        float p = clamp(max(throughput.x, max(throughput.y, throughput.z)), 0.1, 1.0);
        if (depth > 3) {
            if (rand(rng) > p) {
                break;
            } else {
                throughput /= p;
            }
        }

        // our path survived, now we move on to the next integral
        // since we sample the phase function for the next rd, we cancel out the phase term
        // aka L_ins = int(L(y, rd_new) * phase) -> pdf has phase -> int(L(y, rd_new) * phase / phase)

        rd = sample_HG_phase(rd, HG_G, rng);
        x = y;
    }

    // you may have noticed that theres no computation of transmittance between x and y
    // and we divide by mu_t a few times
    // this is because our PDF for the full equation is mu_t * transmittance(x, y)
    // which we divide the integrand by

    return col;
}

vec3 sampleClouds(vec3 ro, vec3 rd, inout uint rng, out float T) {
    vec3 acc = vec3(0.0);
    for (int i = 0; i < CLOUD_SAMPLES; i++) {
        acc += cloudRadiance(ro, rd, rng);
    }

    // geometric transmittance of the whole cloud interval, used for compositing
    float t0, t1;
    if (cloudInterval(ro, rd, t0, t1)) {
        T = exp(-integrateOpticalDepthCloud(ro + t0 * rd, ro + t1 * rd, rng));
    } else {
        T = 1.0;
    }

    return acc / float(CLOUD_SAMPLES);
}

void main() {
    vec2 fragCoord = gl_FragCoord.xy;
    uint rng = rngSeed(uvec2(fragCoord), uint(frame));

    fragCoord += vec2(rand(rng) - 0.5, rand(rng) - 0.5);
    vec3 ro     = CAMERA_POS;
    vec3 rd     = generateRay(fragCoord);


    float tAtmo0, tAtmo1, tGround0, tGround1;
    bool  hitGroundFlag;
    bool  hitAtmo = intersectScene(ro, rd,
                                   tAtmo0, tAtmo1,
                                   tGround0, tGround1,
                                   hitGroundFlag, rng);
    if (!hitAtmo) {
        fragColor = vec4(0.0, 0.0, 0.0, 1.0);
        return;
    }

    float tMin = max(tAtmo0, 0.0);
    float tMax = (hitGroundFlag && tGround0 > 1e-2) ? tGround0 : tAtmo1;


    // cloud interval along this ray
    float tCloud0 = 0.0; float tCloud1 = 0.0;
    bool  hasCloud = cloudInterval(ro, rd, tCloud0, tCloud1)
                  && tCloud1 > tMin && tCloud0 < tMax;

    // Calculate transmittance and radiance for the atmosphere
    vec3 atmRad; vec3 T_atm;
    atmosRadTrans(ro, rd, tMin, tMax, rand(rng), rng, hasCloud, tCloud0, tCloud1, T_atm, atmRad);


    // --- cloud ---
    float T_cloud = 1.0;
    vec3  cloud   = vec3(0.0);
    vec3 T_front = vec3(1.0);

    if (hasCloud) {
        cloud = sampleClouds(ro, rd, rng, T_cloud);
        
        // atmosphere between the camera and the cloud, used to attenuate the cloud's in-scatter
        float vOdR, vOdM, vOdO;
        // we dont need the full integrateOpticalDepth func since the cloud is guaranteed not to exist here
        integrateOpticalDepthAtmos(ro, rd, tMin, tCloud0, ATM_SAMPLES, rand(rng), vOdR, vOdM, vOdO);
        T_front = AtmosTransmittance(vOdR, vOdM, vOdO);
    }


    // --- background: ground or sun disk, attenuated by the cloud ---
    vec3 bg = vec3(0.0);
    if (hitGroundFlag && tGround0 > 1e-2) {
        vec3 gP = ro + rd * tGround0;
        vec3 n  = normalize(gP - PLANET_CENTER);
        bg += shadeGround(gP, n, SUN_DIR, rand(rng));
    } else {
        bg += shadeSunDisk(dot(rd, SUN_DIR));
    }


    // color is atmosphere color + transmittance between cam and front of cloud * cloud color + 
    // full atmosphere transmittance * cloud transmittance * background color
    vec3 color = atmRad + T_front * cloud + T_atm * vec3(T_cloud) * bg;

    if (postprocess == 1) {
        fragColor = vec4(tonemap(color), 1.0);
    } else {
        fragColor = vec4(color, 1.0);
    }
    
}

/*

Problems and in progress stuff


different (biased) quadrature for atmosphere depending on if the cloud exists (seams)
    - current fix is that they both converge to the same val with higher ATMOS_SAMPLES


chapman layer function for ozone
*/