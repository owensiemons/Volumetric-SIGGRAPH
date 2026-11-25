#version 330 core

in vec2 vUV;
out vec4 fragColor;
uniform sampler2D accumTex;

vec3 linearToSRGB(vec3 linearRGB) {
    linearRGB = clamp(linearRGB, 0.0, 1.0);
    vec3 cutoff = vec3(0.0031308);
    vec3 higher = vec3(1.055) * pow(linearRGB, vec3(1.0 / 2.4)) - vec3(0.055);
    vec3 lower  = linearRGB * vec3(12.92);
    return mix(higher, lower, lessThanEqual(linearRGB, cutoff));
}

vec3 tonemap(vec3 color, float exposure) {
    vec3 m = color * exposure;
    return 1.0 - exp(-max(m, vec3(0.0)));
}

void main() {
    vec4 col = texture(accumTex, vUV);
    vec3 tonemapped = tonemap(col.xyz, 2.3);
    fragColor = vec4(linearToSRGB(tonemapped), 1.0);
}