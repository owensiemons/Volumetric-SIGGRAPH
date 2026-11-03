#version 330 core
out vec4 FragColor;

uniform vec2  u_resolution;
uniform vec3  u_cameraPos;
uniform vec3  u_cameraDir;
uniform vec3  u_cameraUp;
uniform float u_tanHalfFov;
uniform vec3  u_sunDir;
uniform vec3  u_sunColor;
uniform float u_sunIntensity;
uniform float u_planetRadius;
uniform float u_atmosphereRadius;
uniform vec3  u_betaRayleigh;
uniform float u_rayleighScaleHeight;
uniform vec3  u_betaMie;
uniform float u_mieScaleHeight;
uniform float u_mieG;
uniform float u_exposure;

#define PI 3.14159265359

const int   VIEW_SAMPLES  = 128; 
const int   LIGHT_SAMPLES = 32;

float phaseRayleigh(float cosT) {
    return (3.0 / (16.0 * PI)) * (1.0 + cosT * cosT);
}

float phaseMie(float g, float cosT) {
    float g2 = g * g;
    float denom = 1.0 + g2 - 2.0 * g * cosT;
    return (3.0 / (8.0 * PI)) * ((1.0 - g2) * (1.0 + cosT * cosT)) / ((2.0 + g2) * max(pow(denom, 1.5), 0.0001));
}

bool intersectSphere(vec3 ro, vec3 rd, vec3 center, float radius, out float t0, out float t1) {
    vec3 oc = ro - center;
    float b = dot(rd, oc);
    float c = dot(oc, oc) - radius * radius;
    float disc = b * b - c;
    if (disc < 0.0) return false;
    float s = sqrt(disc);
    t0 = -b - s;
    t1 = -b + s;
    return t1 >= 0.0;
}

void main() {
    vec2 ndc = (gl_FragCoord.xy * 2.0 - u_resolution.xy) / u_resolution.y;
    vec3 cameraRight = normalize(cross(u_cameraDir, u_cameraUp));
    vec3 rayDir = normalize(u_cameraDir + ndc.x * u_tanHalfFov * cameraRight + ndc.y * u_tanHalfFov * u_cameraUp);
    vec3 rayOrigin = u_cameraPos;

    vec3 sunDir = normalize(u_sunDir);

    float tAtmNear, tAtmFar;
    if (!intersectSphere(rayOrigin, rayDir, vec3(0.0), u_atmosphereRadius, tAtmNear, tAtmFar)) {
        FragColor = vec4(0.0, 0.0, 0.0, 1.0); 
        return;
    }
    if (tAtmNear < 0.0) tAtmNear = 0.0; 

    float tGroundNear, tGroundFar;
    bool hitGround = intersectSphere(rayOrigin, rayDir, vec3(0.0), u_planetRadius, tGroundNear, tGroundFar);
    float tMax = hitGround && tGroundNear > 0.0 ? tGroundNear : tAtmFar;

    float rayLength = tMax - tAtmNear;
    float stepSize = rayLength / float(VIEW_SAMPLES);

    vec3 accumRayleigh = vec3(0.0);
    vec3 accumMie = vec3(0.0);
    float opticalDepthRayleigh = 0.0;
    float opticalDepthMie = 0.0;

    float currentDist = tAtmNear;
    for (int i = 0; i < VIEW_SAMPLES; i++) {
        vec3 samplePos = rayOrigin + rayDir * (currentDist + stepSize * 0.5);
        float height = length(samplePos) - u_planetRadius;
        if (height < 0.0) height = 0.0;

        float dzRayleigh = exp(-height / u_rayleighScaleHeight) * stepSize;
        float dzMie = exp(-height / u_mieScaleHeight) * stepSize;

        opticalDepthRayleigh += dzRayleigh;
        opticalDepthMie += dzMie;

        float tSunNear, tSunFar;
        float tGroundSunNear, tGroundSunFar;
        bool blockedByPlanet = intersectSphere(samplePos, sunDir, vec3(0.0), u_planetRadius, tGroundSunNear, tGroundSunFar);
        if (!blockedByPlanet && intersectSphere(samplePos, sunDir, vec3(0.0), u_atmosphereRadius, tSunNear, tSunFar)) {
            float sunStepSize = tSunFar / float(LIGHT_SAMPLES);
            float lightDepthRayleigh = 0.0;
            float lightDepthMie = 0.0;

            for (int j = 0; j < LIGHT_SAMPLES; j++) {
                vec3 lightPos = samplePos + sunDir * (float(j) + 0.5) * sunStepSize;
                float lightHeight = length(lightPos) - u_planetRadius;
                if (lightHeight > 0.0) {
                    lightDepthRayleigh += exp(-lightHeight / u_rayleighScaleHeight) * sunStepSize;
                    lightDepthMie += exp(-lightHeight / u_mieScaleHeight) * sunStepSize;
                }
            }

            vec3 tau = u_betaRayleigh * (opticalDepthRayleigh + lightDepthRayleigh) + 
                       u_betaMie * 1.1 * (opticalDepthMie + lightDepthMie);
            vec3 attenuation = exp(-tau);

            accumRayleigh += dzRayleigh * attenuation;
            accumMie += dzMie * attenuation;
        }

        currentDist += stepSize;
    }

    float cosTheta = dot(rayDir, sunDir);
    float phaseR = phaseRayleigh(cosTheta);
    float phaseM = phaseMie(u_mieG, cosTheta);

    vec3 color = u_sunIntensity * u_sunColor * (accumRayleigh * u_betaRayleigh * phaseR + accumMie * u_betaMie * phaseM);

    color = vec3(1.0) - exp(-color * u_exposure);
    color = pow(clamp(color, 0.0, 1.0), vec3(1.0 / 2.2)); 

    FragColor = vec4(color, 1.0);
}
