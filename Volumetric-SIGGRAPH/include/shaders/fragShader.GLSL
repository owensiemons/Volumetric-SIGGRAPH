#version 330 core
out vec4 FragColor;

uniform vec2 u_resolution;
uniform vec3 u_sphere_center;
uniform float u_radius;
uniform float u_sigma_a;
uniform float u_sigma_s;
uniform float u_density;
uniform float u_g;
uniform vec3 u_scatter;
uniform vec3 u_background_color;
uniform vec3 u_light_color;
uniform vec3 u_light_position;

const float PI = 3.14159265359;

float hash(vec3 p) {
    p = fract(p * vec3(443.897, 441.423, 437.195));
    p += dot(p, p.yzx + 19.19);
    return fract((p.x + p.y) * p.z);
}

float noise(vec3 p) {
    vec3 i = floor(p);
    vec3 f = fract(p);
    f = f * f * (3.0 - 2.0 * f); 
    
    float n000 = hash(i + vec3(0.0, 0.0, 0.0));
    float n100 = hash(i + vec3(1.0, 0.0, 0.0));
    float n010 = hash(i + vec3(0.0, 1.0, 0.0));
    float n110 = hash(i + vec3(1.0, 1.0, 0.0));
    float n001 = hash(i + vec3(0.0, 0.0, 1.0));
    float n101 = hash(i + vec3(1.0, 0.0, 1.0));
    float n011 = hash(i + vec3(0.0, 1.0, 1.0));
    float n111 = hash(i + vec3(1.0, 1.0, 1.0));
    
    float nx00 = mix(n000, n100, f.x);
    float nx10 = mix(n010, n110, f.x);
    float nx01 = mix(n001, n101, f.x);
    float nx11 = mix(n011, n111, f.x);
    
    float nxy0 = mix(nx00, nx10, f.y);
    float nxy1 = mix(nx01, nx11, f.y);
    
    return mix(nxy0, nxy1, f.z) * 2.0 - 1.0;
}

float eval_density(vec3 p) {
    float freq = 8.0; 
    return (1.0 + noise(p * freq)) * 0.5;
}

float phaseHG(float g, float cos_theta) {
    float denom = 1.0 + g * g - 2.0 * g * cos_theta;
    return (1.0 / (4.0 * PI)) * (1.0 - g * g) / (denom * sqrt(denom));
}

bool intersectSphere(vec3 ro, vec3 rd, vec3 sphere_center, float radius, out float t0, out float t1) {
    vec3 oc = ro - sphere_center; 
    float a = dot(rd, rd);
    float b = 2.0 * dot(rd, oc); 
    float c = dot(oc, oc) - radius * radius;
    float disc = b * b - 4.0 * a * c; 
    if (disc < 0.0) return false; 
    float sqrtDisc = sqrt(disc); 
    t0 = (-b - sqrtDisc) / (2.0 * a);
    t1 = (-b + sqrtDisc) / (2.0 * a);
    if (t0 < 0.0) t0 = t1; 
    return t0 >= 0.0; 
}

vec3 traceScene(vec3 ro, vec3 rd, vec2 fragCoord) {  
    float t0, t1;
    if (!intersectSphere(ro, rd, u_sphere_center, u_radius, t0, t1)) { 
        return u_background_color; 
    }

    int nSteps = 24;
    float stepSize = (t1 - t0) / float(nSteps);

    float transparency = 1.0;
    vec3 result = vec3(0.0); 

    float random_offset = fract(sin(dot(fragCoord, vec2(12.9898, 78.233))) * 43758.5453);
    float sigma_t = u_sigma_a + u_sigma_s;

    for (int i = 0; i < nSteps; ++i) {
        float t = t0 + (float(i) + random_offset) * stepSize; 
        vec3 samplePos = ro + rd * t;

        float local_density = eval_density(samplePos) * u_density;

        float sample_attenuation = exp(-stepSize * local_density * sigma_t);
        transparency *= sample_attenuation;

        vec3 light_dir = normalize(u_light_position - samplePos);
    
        float lt0, lt1;
        if (intersectSphere(samplePos, light_dir, u_sphere_center, u_radius, lt0, lt1)) {
            float light_dist = (lt0 < 0.0) ? lt1 : (lt1 - lt0);
            int num_steps_light = 16; 
            float stride_light = light_dist / float(num_steps_light);
            float tau = 0.0;

            for (int nl = 0; nl < num_steps_light; ++nl) {
                float t_light = stride_light * (float(nl) + 0.5);
                vec3 light_sample_pos = samplePos + light_dir * t_light;
                tau += eval_density(light_sample_pos) * u_density;
            }

            float light_ray_att = exp(-tau * stride_light * sigma_t);

            float cos_theta = dot(rd, light_dir);
            float phase = phaseHG(u_g, cos_theta);

            result += transparency * u_light_color * light_ray_att * local_density * u_sigma_s * stepSize * phase;
        }

        if (transparency < 0.001) {
            float rr_prob = 0.5; 
            if (hash(vec3(fragCoord, float(i))) > rr_prob) {
                break;
            } else {
                transparency /= rr_prob;
            }
        }
    }
    return u_background_color * transparency + result;
}

void main() {
    vec2 uv = (gl_FragCoord.xy / u_resolution.xy) * 2.0 - 1.0;
    uv.x *= u_resolution.x / u_resolution.y;

    vec3 ro = vec3(0.0, 0.0, 0.0);
    vec3 rd = normalize(vec3(uv, -1.0));

    vec3 color = traceScene(ro, rd, gl_FragCoord.xy);
    FragColor = vec4(color, 1.0);
}
