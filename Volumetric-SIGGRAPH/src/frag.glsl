#version 330 core

out vec4 fragColor;

uniform vec2 iResolution;

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

vec3 integrate(vec3 ro, vec3 rd) {
    vec3 bg_col = vec3(0, 0, 0);
    vec3 pos;

    if (sphere_trace(ro, rd, pos) < 0.0) { // check if we hit anything w/ sphere trace, store pos if we do
        return bg_col;
    }

    float step_size = 0.005;
    float sigma_a = 1.2;

    vec3 light_pos = vec3(0, 5, 0);
    vec3 light_color = vec3(1.0, 1.0, 1.0);
    
    float transparency = 1.0;
    vec3 res = vec3(0.0, 0.0, 0.0);

    for (int i = 0; i < 1000; i++) {
        // length(pos) = distance from camera to hit, need to change if camera ever moves from 0,0,0
        float t = length(pos) + (i + 0.5) * step_size;
        vec3 curr_pos = ro + t * rd;

        if (map(curr_pos) > 0.0) {
            break;
        }

        float sample_attenuation = exp(-step_size * sigma_a);
        transparency *= sample_attenuation;

        vec3 curr_light_pos;
        float l = sphere_trace(light_pos, normalize(curr_pos - light_pos), curr_light_pos);
        if (l >= 0.0) {
            float light_attenuation = exp(-length(curr_light_pos - curr_pos) * sigma_a);
            res += transparency * light_color * light_attenuation * step_size;
        }

    }

    return bg_col * transparency + res;
}

void main() {
    // Built-in pixel coordinates
    vec2 fragCoord = gl_FragCoord.xy;

    // Normalize coordinates from -1 to 1
    vec2 uv = (fragCoord / iResolution) * 2.0 - 1.0;

    // Correct aspect ratio
    uv.x *= iResolution.x / iResolution.y;

    vec3 ro = vec3(0.0, 0.0, 0.0);
    vec3 rd = normalize(vec3(uv, 1.0));


    vec3 color = integrate(ro, rd);

    fragColor = vec4(color, 1.0);
}