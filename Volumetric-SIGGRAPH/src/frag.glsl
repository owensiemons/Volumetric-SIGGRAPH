#version 330 core
#define PI 3.14159265359

out vec4 fragColor;

uniform vec2 iResolution;
uniform int frame;

const vec3 SUN_POS = vec3(2.0, 1.5, 2.0);
const float SUN_RADIUS = 0.2;
const vec3 SUN_RADIANCE = vec3(500.0);
const float HG_G = 0.1;
const float dt = 0.01;
const int NUM_SAMPLES = 100;
const float SIGMA_S = 0.8;
const float SIGMA_A = 0.4;

// RNG stuff
uint pcg_hash(uint v) {
    uint state = v * 747796405u + 2891336453u;
    uint word  = ((state >> ((state >> 28u) + 4u)) ^ state) * 277803737u;
    return (word >> 22u) ^ word;
}

uint init_rng(uvec2 pixel, uint frame_num, uint width) {
    uint pixelIndex = pixel.y * width + pixel.x;
    return pcg_hash(pixelIndex ^ pcg_hash(frame_num));
}

float rand(inout uint rng) {
    rng = pcg_hash(rng);
    return float(rng >> 8u) * (1.0 / 16777216.0);
}

// SDF stuff
float sdf_sphere(vec3 p, vec3 center, float rad) {
    return length(center - p) - rad;
}

float smin(float a, float b, float k) {
    k *= 6.0;
    float h = max( k-abs(a-b), 0.0 )/k;
    return min( a, b ) - h*h*h*k*(1.0/6.0);
}

float map(vec3 p) {
    float s1 = sdf_sphere(p, vec3(-0.5, 0.0, 2.0), 1.0);
    float s2 = sdf_sphere(p, vec3(0.5, 0.0, 2.0), 1.0);
    return smin(s1, s2, 0.05);
}

float sphere_trace(vec3 ro, vec3 rd, out vec3 pos) {
    float t = 0.0;
    vec3 p = ro;
    for (int i = 0; i < 256; i++) {        
        p = ro + t * rd;
        pos = p;
        float d = map(p);
        if (d < 0.00001) {
            return t;
        }

        if (t > 50.0) {
            return -1.0;
        }
        
        t += max(d, 0.001);
    }

    return -1.0;
}

// coeffecients

float mu_s(vec3 pos) {
    if (map(pos) < 0.0) return SIGMA_S;
    return 0.0;
}

float mu_a(vec3 pos) {
    if (map(pos) < 0.0) return SIGMA_A;
    return 0.0;
}

float mu_t(vec3 pos) {
    if (map(pos) < 0.0) return SIGMA_A + SIGMA_S;
    return 0.0;
}

float HG_phase(float g, float cos_t) {
    float denom = 1.0 + g * g - 2.0 * g * cos_t;
    return (1.0 - g * g) / (4.0 * PI * denom * sqrt(denom));
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

vec3 sample_sun_cone(vec3 y, vec3 center, float rad, out float pdf, inout uint rng) { // Claude generated
    vec3 to_c = center - y;
    float d2 = dot(to_c, to_c);
    vec3 w = to_c * inversesqrt(d2);

    float sin2_max = rad * rad / d2;
    float cos_max = sqrt(max(0.0, 1.0 - sin2_max));
    float one_minus_cos = sin2_max / (1.0 + cos_max);

    float cos_t = 1.0 - rand(rng) * one_minus_cos;
    float sin_t = sqrt(max(0.0, 1.0 - cos_t * cos_t));
    float phi = 2.0 * PI * rand(rng);

    vec3 t, b;
    basis(w, t, b);
    pdf = 1.0 / (2.0 * PI * one_minus_cos);
    return sin_t * cos(phi) * t + sin_t * sin(phi) * b + cos_t * w;
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

vec3 boundary_radiance(vec3 x_s, vec3 rd, bool first) {
    if (first) {// check if we intersect the sun
        vec3 oc = x_s - SUN_POS;
        float b = dot(oc, rd);
        float c = dot(oc, oc) - SUN_RADIUS * SUN_RADIUS;
        if (c > 0.0 && b < 0.0 && b * b - c > 0.0) return SUN_RADIANCE;
    }

    // add in atmosphere stuff here
    return vec3(0.0);
    //return mix(vec3(1.0), vec3(0.3, 0.5, 1.0), (rd.y + 1.0) / 2.0);
}


vec3 emitted_radiance(vec3 y, vec3 rd) {
    return vec3(0.0);
}

vec3 radiance(vec3 ro, vec3 rd, inout uint rng) {
    vec3 x = vec3(-1.0); // entry position
    vec3 x_s = vec3(-1.0); // boundary position

    if (sphere_trace(ro, rd, x) < 0.0) {
        return boundary_radiance(ro, rd, true);
    }

    vec3 col = vec3(0.0);
    // instead of recursion on radiance(), we use a loop approach with throughput storing the weights so far
    // basically we always mult the next radiance() call by  mu_s(y) / mu_t(y) so we store those and use them at the end
    vec3 throughput = vec3(1.0);

    // to avoid stalling thread groups and the like we have a max depth, even though russian roulette early exits
    for (int depth = 0; depth < 1000; depth++) {
        sphere_trace(x + 32.0 * rd, -rd, x_s); // hacky way of finding x_s

        // == free flight stuff ==

        float tau_max = length(x - x_s);
    
        // tau is our free flight distance, ie how far we travel before an interaction
        // we find it by inversion sampling the PDF p_lambda
        // which is just the integral of our mu_t over the total path
        // we currently solve this with ray marching (riemman sum), delta tracking is better
        float target = -log(1.0 - rand(rng));
        float opt_depth = 0.0;
        float tau = 0.0;

        while (tau < tau_max) {
            opt_depth += mu_t(x + tau * rd) * dt;
            if (opt_depth > target) {
                break;
            }
            tau += dt;
        } // additionally, we are basically calculating transmittance(x, x_s) here

        if (tau >= tau_max) { // No interaction during the whole distance, return just boundary rad
            // we dont add sunlight after first depth because we are already doing that with NEE
            col += throughput * boundary_radiance(x_s, rd, depth == 0);
            break;
        }

        vec3 y = x + tau * rd; // interaction point

        // add in emitted radiance
        col += throughput * mu_a(y) / mu_t(y) * emitted_radiance(y, rd);
        // update throughput for scattering
        throughput *= mu_s(y) / mu_t(y);


        // == Next Event Estimation Stuff ==

        // the PDF for sampling on the sun (direct pdf)
        float dpdf = -1.0;
        // random point facing us on sun
        vec3 w_i = sample_sun_cone(y, SUN_POS, SUN_RADIUS, dpdf, rng);

        vec3 y_exit;
        sphere_trace(y + 32.0 * w_i, -w_i, y_exit);
        // transmittance from current pos to edge of cloud towards sun
        float T_cloud = transmittance(y, y_exit);
        // transmittance from edge of cloud to edge of atmosphere towards sun (temp)
        float T_atmos = 1.0;
        // with NEE we directly find the incoming light instead of waiting to randomly sample it
        col += throughput * SUN_RADIANCE * HG_phase(HG_G, dot(rd, w_i)) * T_cloud * T_atmos / dpdf;


        // here we do russian roulette with p = max(throughput)
        // this is because we dont want to skip paths with high "energy"
        // same reason we only do RR with depth > 3
        float p = max(throughput.x, max(throughput.y, throughput.z));
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

void main() {
    // Built-in pixel coordinates
    vec2 fragCoord = gl_FragCoord.xy;

    // Normalize coordinates from -1 to 1
    vec2 uv = (fragCoord / iResolution) * 2.0 - 1.0;

    // Correct aspect ratio
    uv.x *= iResolution.x / iResolution.y;

    uint rng = init_rng(uvec2(fragCoord), uint(frame), uint(iResolution.x));

    vec3 ro = vec3(0.0, 0.0, -1.8);
    const float FOV = radians(50.0);
    vec3 rd = normalize(vec3(uv * tan(0.5 * FOV), 1.0));

    vec3 sum = vec3(0.0);
    for (int i = 0; i < NUM_SAMPLES; i++) {
        sum += radiance(ro, rd, rng);
    }
    sum /= float(NUM_SAMPLES);

    fragColor = vec4(sum, 1.0);
}