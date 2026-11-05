#version 330 core
#define PI 3.14159265359

out vec4 fragColor;

uniform vec2 iResolution;
uniform int frame;

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

vec3 boundary_radiance(vec3 x_s, vec3 rd) {
    return mix(vec3(1.0), vec3(0.3, 0.5, 1.0), (rd.y + 1.0) / 2.0);
}

// coeffecients

float mu_s(vec3 pos) {
    return 0.8;
}

float mu_a(vec3 pos) {
    return 0.8;
}

float mu_t(vec3 pos) {
    return mu_s(pos) + mu_a(pos);
}

float HG_phase(float g, float theta) {
    return (1.0 - g * g) / (4.0 * PI * pow(1.0 + g * g - 2.0 * g * cos(theta), 3.0/2.0));
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


vec3 emitted_radiance(vec3 y, vec3 rd) {
    return vec3(0.0);
}


vec3 radiance(vec3 ro, vec3 rd, inout uint rng) {
    vec3 x = vec3(-1.0); // entry position
    vec3 x_s = vec3(-1.0); // boundary position

    if (sphere_trace(ro, rd, x) < 1.0) {
        return boundary_radiance(ro, rd);
    }

    vec3 col = vec3(0.0);
    // instead of recursion on radiance(), we use a loop approach with throughput storing the weights so far
    // basically we always mult the next radiance() call by  mu_s(y) / mu_t(y) so we store those and use them at the end
    vec3 throughput = vec3(1.0);

    // to avoid stalling thread groups and the like we have a max depth, even though russian roulette early exits
    for (int depth = 0; depth < 1000; depth++) {
        sphere_trace(x + 32.0 * rd, -rd, x_s); // hacky way of finding x_s

        float tau_max = length(x - x_s);
    
        // tau is our free flight distance, ie how far we travel before an interaction
        // we find it by inversion sampling the PDF p_lambda
        // which is just the integral of our mu_t over the total path
        // we currently solve this with ray marching (riemman sum), delta tracking is better
        float target = -log(1.0 - rand(rng));
        float opt_depth = 0.0;
        float tau = 0.0;

        while (tau < tau_max) {
            opt_depth += mu_t(x + tau * rd) * 0.01;
            if (opt_depth >= target) {
                break;
            }
            tau += 0.01; // dt = 0.01
        } // additionally, we are basically calculating transmittance(x, x_s) here

        if (tau >= tau_max) { // No interaction during the whole distance, return just boundary rad
            col += throughput * boundary_radiance(x_s, rd);
            break;
        }

        vec3 y = x + tau * rd; // interaction point

        col += throughput * mu_a(y) / mu_t(y) * emitted_radiance(y, rd);
        throughput *= mu_s(y) / mu_t(y);

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

        rd = sample_HG_phase(rd, 0.6, rng);
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

    vec3 ro = vec3(0.0, 0.0, 0.0);
    vec3 rd = normalize(vec3(uv, 1.0));

    vec3 sum = vec3(0.0);
    for (int i = 0; i < 100; i++) {
        sum += radiance(ro, rd, rng);
    }
    sum /= float(100);

    fragColor = vec4(sum, 1.0);
}