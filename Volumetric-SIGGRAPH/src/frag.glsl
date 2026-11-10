#version 330 core
#define PI 3.14159265359

out vec4 fragColor;

uniform vec2 iResolution;
uniform int frame;

//Camera Parameters
const vec3  CAMERA_POS           = vec3(0.0,0.0,0); // Camera position
const vec3  CAMERA_DIR           = vec3(0.0, 0.5, -1.0); // Camera direction
const vec3  CAMERA_UP            = vec3(0.0, 1.0, 0.0); // Up direction
const float TAN_HALF_FOV         = 0.57735026918962576; // tan(30°), half of the fov

//Post Process
const float EXPOSURE              = 2.3;

//Solar Parameters
const vec3  SUN_DIR              = vec3(0.290204, 0.002, -0.955741); // 
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
const float HG_G = 0.1;
const float dt = 0.01;
const float SIGMA_S = 0.7;
const float SIGMA_A = 0.1;
const vec3  CLOUD_CENTER = vec3(0.0, 2.0, -3.0); 
const float CLOUD_RADIUS = 0.55;

//Atmosphere Parameters
const vec3  BETA_RAYLEIGH         = vec3(5.8e-3, 1.35e-2, 3.31e-2);
const float RAYLEIGH_SCALE_HEIGHT = 8.0;
const vec3  BETA_MIE              = vec3(4.0e-3, 4.0e-3, 4.0e-3);
const float MIE_SCALE_HEIGHT      = 1.2;
const float MIE_G                 = 0.758;
const float MIE_EXTINCTION_SCALE  = 1.1;

//Ground Parameters
const vec3  GROUND_ALBEDO         = vec3(0.20, 0.18, 0.15);

//Rendering Samples
const int ATM_SAMPLES   = 4;
const int CLOUD_SAMPLES   = 12;
const int LIGHT_SAMPLES  = 4;
const int MS_STEPS       = 3;
const int MS_INTEGRATION = 3;


// RNG stuff
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

// forward declarations
bool intersectSphere(vec3 ro, vec3 rd, vec3 center, float R, out float t0, out float t1);
vec3 sunTransmittance(vec3 p, vec3 sunDir, float jitter);


// SDF stuff
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
    float s1 = sdf_sphere(p, vec3(-0.2, 2.0, -3.0), 0.3);
    float s2 = sdf_sphere(p, vec3(0.2, 2.0, -3.0), 0.3);
    return smin(s1, s2, 0.05);
}

vec3 AtomTransmittance(float odR, float odM) {
    return exp(-(BETA_RAYLEIGH * odR + BETA_MIE * MIE_EXTINCTION_SCALE * odM));
}

// coeffecients

float mu_t(vec3 pos) {
    if (map(pos) < 0.0) return SIGMA_A + SIGMA_S;
    return 0.0;
}

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

void basis(vec3 n, out vec3 t, out vec3 b) { // Claude generated
    float s = n.z >= 0.0 ? 1.0 : -1.0;
    float a = -1.0 / (s + n.z);
    float c = n.x * n.y * a;
    t = vec3(1.0 + s * n.x * n.x * a, s * c, -s * n.x);
    b = vec3(c, s + n.y * n.y * a, -n.y);
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

// riemman sum / raymarch
float transmittance(vec3 x, vec3 y) {
    vec3 d = y - x;
    float len = length(d);
    if (len < 1e-6) return 1.0;
    vec3 dir = d / len;

    float optical_depth = 0.0;

    for (float s = 0.0; s < len; s += dt) {
        float h = min(dt, len - s);
        optical_depth += mu_t(x + (s + 0.5 * h) * dir) * h;
    }

    return exp(-optical_depth);
}

// analytic entry/exit interval of the cloud volume
bool cloudInterval(vec3 ro, vec3 rd, out float t0, out float t1) {
    if (!intersectSphere(ro, rd, CLOUD_CENTER, CLOUD_RADIUS, t0, t1)) return false;
    t0 = max(t0, 0.0);
    return t1 > t0;
}

// cloud radiance along the ray, see sampleClouds for the compositing transmittance
vec3 radiance(vec3 ro, vec3 rd, inout uint rng) {
    float t0, t1;
    if (!cloudInterval(ro, rd, t0, t1)) {
        return vec3(0.0);
    }

    vec3 x = ro + t0 * rd; // entry position
    vec3 col = vec3(0.0);
    // instead of recursion on radiance(), we use a loop approach with throughput storing the weights so far
    // basically we always mult the next radiance() call by  mu_s(y) / mu_t(y) so we store those and use them at the end
    vec3 throughput = vec3(1.0);

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
        float mu_bar = SIGMA_A + SIGMA_S;
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
            break; // we dont add sunlight after first depth because we are already doing that with NEE
        }

        vec3 y = x + (ta + tau) * rd; // interaction point

        // one SDF evaluation at the interaction point gives the scattering coefficient
        float sdf_y = map(y);
        float sigma_t = sdf_y < 0.0 ? (SIGMA_A + SIGMA_S) : 0.0;
        // update throughput for scattering
        throughput *= (sdf_y < 0.0 ? SIGMA_S : 0.0) / max(sigma_t, 1e-6);


        // == Next Event Estimation ==

        // We treat the sun as a directional light here as it should be basically infinitely far
        vec3 w_i = SUN_DIR;

        // exit point of the cloud along the sun direction, found analytically
        float ts0, ts1;
        vec3 y_exit = y;
        if (intersectSphere(y, w_i, CLOUD_CENTER, CLOUD_RADIUS, ts0, ts1)) {
            y_exit = y + max(ts1, 0.0) * w_i;
        }
        // transmittance from current pos to edge of cloud towards sun
        float T_cloud = transmittance(y, y_exit);
        // transmittance from edge of cloud to edge of atmosphere towards sun
        vec3 T_atmos = sunTransmittance(y, w_i, rand(rng));
        // with NEE we directly find the incoming light instead of waiting to randomly sample it
        col += throughput * SUN_INTENSITY * SUN_COLOR
             * HG_phase(HG_G, dot(rd, w_i)) * T_cloud * T_atmos;


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



void integrateOpticalDepth(vec3 ro, vec3 rd, float t0, float t1, int N, float jitter,
                           out float odR, out float odM) {
    odR = 0.0; odM = 0.0;
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
        if (intersectSphere(q, sunDir, PLANET_CENTER, PLANET_RADIUS, qg0, qg1) && qg0 > 0.0) continue;  

        float s0, s1;
        if (!intersectSphere(q, sunDir, PLANET_CENTER, ATMOSPHERE_RADIUS, s0, s1)) continue;
        float sStart = max(s0, 0.0);
        float ldR, ldM;
        integrateOpticalDepth(q + sunDir * sStart, sunDir, 0.0, s1 - sStart,
                              MS_INTEGRATION, 0.5, ldR, ldM);
        vec3 T_sun = AtomTransmittance(ldR, ldM);

        float pR, pM;
        integrateOpticalDepth(q, -up, 0.0, sk, MS_INTEGRATION, 0.5, pR, pM);
        vec3 T_qp = AtomTransmittance(pR, pM);

        float rhoR = exp(-hq / RAYLEIGH_SCALE_HEIGHT);
        float rhoM = exp(-hq / MIE_SCALE_HEIGHT);
        vec3  betaRho = BETA_RAYLEIGH * rhoR + BETA_MIE * rhoM;   // 不再用 betaR+betaM 配纯瑞利密度

        L += betaRho * T_sun * T_qp * dsk;
    }

    return SUN_INTENSITY * SUN_COLOR * L / (4.0 * PI);
}

vec3 generateRay(vec2 fragCoord) {
    vec3 camD = normalize(CAMERA_DIR);
    vec3 camR = normalize(cross(camD, CAMERA_UP));
    vec3 camU = cross(camR, camD);
    vec2 uv = (fragCoord * 2.0 - iResolution.xy) / iResolution.y;
    return normalize(camD + uv.x * TAN_HALF_FOV * camR
                                + uv.y * TAN_HALF_FOV * camU);
}

bool intersectScene(vec3 ro, vec3 rd,
                    out float tAtmo0,  out float tAtmo1,
                    out float tGround0, out float tGround1,
                    out bool  hitGround)
{
    bool hitAtmo = intersectSphere(ro, rd, PLANET_CENTER,
                                   ATMOSPHERE_RADIUS, tAtmo0, tAtmo1);
    hitGround = hitAtmo &&
                intersectSphere(ro, rd, PLANET_CENTER,
                                PLANET_RADIUS, tGround0, tGround1);
    return hitAtmo;
}

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
    float ldR, ldM;
    integrateOpticalDepth(p + sunDir * sStart, sunDir, 0.0, sLen,
                          LIGHT_SAMPLES, jitter, ldR, ldM);
    return AtomTransmittance(ldR, ldM);
}

void traceAtmosphere(vec3 ro, vec3 rd,
                     float tMin, float tMax,
                     float jitter,
                     inout uint rng,
                     out vec3 inscatterR,
                     out vec3 inscatterM,
                     out vec3 inscatterMS,
                     out float odR,
                     out float odM)
{
    inscatterR  = vec3(0.0);
    inscatterM  = vec3(0.0);
    inscatterMS = vec3(0.0);
    odR = 0.0;
    odM = 0.0;

    float len = max(tMax - tMin, 1e-4);
    vec3  sunDir = normalize(SUN_DIR);

    for (int i = 0; i < ATM_SAMPLES; i++) {
        float u0 = (float(i)     + jitter) / float(ATM_SAMPLES);
        float u1 = min((float(i + 1) + jitter) / float(ATM_SAMPLES), 1.0);
        float s0 = tMin + len * u0 * u0 * u0;
        float s1 = tMin + len * u1 * u1 * u1;
        float ds = s1 - s0;
        float sm = 0.5 * (s0 + s1);

        vec3  p  = ro + rd * sm;
        float h  = max(length(p - PLANET_CENTER) - PLANET_RADIUS, 0.0);

        float dzR = exp(-h / RAYLEIGH_SCALE_HEIGHT) * ds;
        float dzM = exp(-h / MIE_SCALE_HEIGHT)      * ds;

        vec3 viewAtt = AtomTransmittance(odR + 0.5 * dzR, odM + 0.5 * dzM);

        odR += dzR;
        odM += dzM;

        float jitS = rand(rng);
        vec3 sunAtt = sunTransmittance(p, sunDir, jitS);

        inscatterR += dzR * sunAtt * viewAtt;
        inscatterM += dzM * sunAtt * viewAtt;
        
        vec3 L_inc = multiScatterApprox(p, h);
        inscatterMS += (dzR * BETA_RAYLEIGH + dzM * BETA_MIE) * L_inc * viewAtt;
    }
}

vec3 sampleClouds(vec3 ro, vec3 rd, inout uint rng, out float T) {
    vec3 acc = vec3(0.0);
    for (int i = 0; i < CLOUD_SAMPLES; i++) {
        acc += radiance(ro, rd, rng);
    }

    // geometric transmittance of the whole cloud interval, used for compositing
    float t0, t1;
    if (cloudInterval(ro, rd, t0, t1)) {
        T = transmittance(ro + t0 * rd, ro + t1 * rd);
    } else {
        T = 1.0;
    }

    return acc / float(CLOUD_SAMPLES);
}

// returns unattenuated radiance, the path transmittance is applied by the caller
vec3 shadeGround(vec3 gP, vec3 n, vec3 sunDir, float jitter) {
    vec3 sunAtt = sunTransmittance(gP, sunDir, jitter);
    float NdotL = max(dot(n, sunDir), 0.0);
    return GROUND_ALBEDO / PI * SUN_INTENSITY * SUN_COLOR * sunAtt * NdotL;
}

// returns unattenuated radiance of the disk, path transmittance is applied by the caller
vec3 shadeSunDisk(float cosT) {
    // cos is decreasing, so the inner edge is the larger one: keep edge0 < edge1
    float w = smoothstep(cos(SUN_ANGULAR_RADIUS), cos(SUN_ANGULAR_RADIUS * 0.9), cosT);
    if (w <= 0.0) return vec3(0.0);
    return SUN_RADIANCE * SUN_COLOR * w;
}

vec3 tonemap(vec3 color) {
    vec3 m = color * EXPOSURE;
    m = 1.0 - exp(-max(m, vec3(0.0)));
    return pow(clamp(m, 0.0, 1.0), vec3(1.0 / 2.2));
}

void main() {
    vec2 fragCoord = gl_FragCoord.xy;
    vec3 ro     = CAMERA_POS;
    vec3 rd     = generateRay(fragCoord);
    vec3 sunDir = SUN_DIR;

    float tAtmo0, tAtmo1, tGround0, tGround1;
    bool  hitGroundFlag;
    bool  hitAtmo = intersectScene(ro, rd,
                                   tAtmo0, tAtmo1,
                                   tGround0, tGround1,
                                   hitGroundFlag);
    if (!hitAtmo) {
        fragColor = vec4(0.0, 0.0, 0.0, 1.0);
        return;
    }

    float tMin = max(tAtmo0, 0.0);
    float tMax = (hitGroundFlag && tGround0 > 1e-4) ? tGround0 : tAtmo1;

    uint rng = rngSeed(uvec2(fragCoord), uint(frame));
    float jitA = rand(rng);

    // cloud interval along this ray
    float tCloud0, tCloud1;
    bool  hasCloud = cloudInterval(ro, rd, tCloud0, tCloud1)
                  && tCloud1 > tMin && tCloud0 < tMax;

    // --- atmosphere: one march over the full path, identical for every ray ---
    // splitting this by the cloud bound leaves a visible seam on the bound, so it is
    // integrated once here and only the background is attenuated by the cloud
    vec3  aR, aM, aMS;
    float aOdR, aOdM;
    traceAtmosphere(ro, rd, tMin, tMax, jitA, rng,
                    aR, aM, aMS, aOdR, aOdM);
    vec3 T_atm = AtomTransmittance(aOdR, aOdM);

    // --- cloud ---
    float T_cloud = 1.0;
    vec3  cloud   = vec3(0.0);
    if (hasCloud) {
        cloud = sampleClouds(ro, rd, rng, T_cloud);
    }

    // atmosphere between the camera and the cloud, used to attenuate the cloud's in-scatter
    vec3 T_front = vec3(1.0);
    if (hasCloud) {
        float vOdR, vOdM;
        integrateOpticalDepth(ro, rd, tMin, tCloud0, ATM_SAMPLES, jitA, vOdR, vOdM);
        T_front = AtomTransmittance(vOdR, vOdM);
    }

    // --- background: ground or sun disk, attenuated by the cloud ---
    vec3 bg = vec3(0.0);
    if (hitGroundFlag && tGround0 > 1e-4) {
        vec3 gP = ro + rd * tGround0;
        vec3 n  = normalize(gP - PLANET_CENTER);
        bg += shadeGround(gP, n, sunDir, rand(rng));
    } else {
        bg += shadeSunDisk(dot(rd, sunDir));
    }

    float cosT   = dot(rd, sunDir);
    vec3  atm    = SUN_INTENSITY * SUN_COLOR * (
                       aR * BETA_RAYLEIGH * phaseRayleigh(cosT) +
                       aM * BETA_MIE      * phaseMie(MIE_G, cosT))
                 + aMS;

    vec3 color = atm + T_front * cloud + T_atm * vec3(T_cloud) * bg;

    fragColor = vec4(tonemap(color), 1.0);
}
