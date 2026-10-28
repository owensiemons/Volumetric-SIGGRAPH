#include <glad/glad.h>
#include <GLFW/glfw3.h>
#include <iostream>

void framebuffer_size_callback(GLFWwindow* window, int width, int height);
void process_input(GLFWwindow* window);

// Vertex shader: just passes the screen-covering quad through.
const char* vertexShaderSource = R"(
#version 330 core
layout (location = 0) in vec2 aPos;
void main() {
    gl_Position = vec4(aPos, 0.0, 1.0);
}
)";

// Fragment shader: for each pixel, step a ray through a fuzzy ball of fog
// and add up how much light it picks up along the way.
const char* fragmentShaderSource = R"(
#version 330 core
out vec4 FragColor;

uniform vec2 uResolution;
uniform float uTime;

// Random number from a 3D point
float hash(vec3 p) {
    return fract(sin(dot(p, vec3(12.9898, 78.233, 37.719))) * 43758.5453);
}

// Smooth noise: blend the random values at the 8 corners of a cube
float noise(vec3 p) {
    vec3 i = floor(p);
    vec3 f = fract(p);
    f = f * f * (3.0 - 2.0 * f);
    return mix(mix(mix(hash(i + vec3(0,0,0)), hash(i + vec3(1,0,0)), f.x),
                   mix(hash(i + vec3(0,1,0)), hash(i + vec3(1,1,0)), f.x), f.y),
               mix(mix(hash(i + vec3(0,0,1)), hash(i + vec3(1,0,1)), f.x),
                   mix(hash(i + vec3(0,1,1)), hash(i + vec3(1,1,1)), f.x), f.y), f.z);
}

// How thick the cloud is at point p: a ball of radius 1 with gentle lumps
float density(vec3 p) {
    float ball = 1.0 - length(p);
    float lumps = noise(p * 2.0 + uTime * 0.2) * 0.15 + noise(p * 4.0) * 0.06;
    return clamp(ball + lumps - 0.1, 0.0, 1.0) * 4.0;
}

// How much sunlight reaches point p: step toward the sun and add up density
float sunlight(vec3 p, vec3 sunDir) {
    float total = 0.0;
    for (int i = 0; i < 6; i++) {
        p += sunDir * 0.15;
        total += density(p);
    }
    return exp(-total * 0.15 * 1.5);
}

void main() {
    // Camera at z = 5 looking toward the ball at the origin
    vec2 uv = (gl_FragCoord.xy - 0.5 * uResolution) / uResolution.y;
    vec3 rayOrigin = vec3(0.0, 0.0, 5.0);
    vec3 rayDir = normalize(vec3(uv, -1.5));

    vec3 sunDir = normalize(vec3(0.6, 0.7, 0.4));
    vec3 sky = vec3(0.53, 0.81, 0.98);

    vec3 color = vec3(0.0);
    float transparency = 1.0;
    float stepSize = 0.05;

    // March the ray from just in front of the ball to just behind it
    for (float t = 3.0; t < 7.0; t += stepSize) {
        vec3 p = rayOrigin + rayDir * t;
        float d = density(p);
        if (d > 0.0) {
            float light = sunlight(p, sunDir);
            vec3 cloudColor = vec3(0.85) * light + vec3(0.45, 0.55, 0.7) * 0.4;

            float absorbed = 1.0 - exp(-d * stepSize * 3.0);
            color += transparency * cloudColor * absorbed;
            transparency *= 1.0 - absorbed;
        }
    }

    // Whatever light got through shows the sky behind
    FragColor = vec4(color + sky * transparency, 1.0);
}
)";

unsigned int compileShader(GLenum type, const char* source) {
    unsigned int shader = glCreateShader(type);
    glShaderSource(shader, 1, &source, nullptr);
    glCompileShader(shader);

    int success;
    glGetShaderiv(shader, GL_COMPILE_STATUS, &success);
    if (!success) {
        char log[1024];
        glGetShaderInfoLog(shader, 1024, nullptr, log);
        std::cout << "Shader compile error:\n" << log << "\n";
    }
    return shader;
}

int main() {
    if (!glfwInit()) return -1;
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);

    GLFWwindow* window = glfwCreateWindow(800, 600, "Volumetric Cloud", nullptr, nullptr);
    if (!window) { glfwTerminate(); return -1; }
    glfwMakeContextCurrent(window);
    glfwSetFramebufferSizeCallback(window, framebuffer_size_callback);

    if (!gladLoadGLLoader((GLADloadproc)glfwGetProcAddress)) {
        std::cout << "Failed to init GLAD\n";
        return -1;
    }

    // --- Build the shader program ---
    unsigned int vertexShader = compileShader(GL_VERTEX_SHADER, vertexShaderSource);
    unsigned int fragmentShader = compileShader(GL_FRAGMENT_SHADER, fragmentShaderSource);

    unsigned int shaderProgram = glCreateProgram();
    glAttachShader(shaderProgram, vertexShader);
    glAttachShader(shaderProgram, fragmentShader);
    glLinkProgram(shaderProgram);
    glDeleteShader(vertexShader);
    glDeleteShader(fragmentShader);

    // --- A rectangle covering the whole screen (two triangles) ---
    float quad[] = {
        -1.0f, -1.0f,   1.0f, -1.0f,   1.0f,  1.0f,
        -1.0f, -1.0f,   1.0f,  1.0f,  -1.0f,  1.0f,
    };

    unsigned int VAO, VBO;
    glGenVertexArrays(1, &VAO);
    glGenBuffers(1, &VBO);
    glBindVertexArray(VAO);
    glBindBuffer(GL_ARRAY_BUFFER, VBO);
    glBufferData(GL_ARRAY_BUFFER, sizeof(quad), quad, GL_STATIC_DRAW);
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 2 * sizeof(float), (void*)0);
    glEnableVertexAttribArray(0);

    int resolutionLoc = glGetUniformLocation(shaderProgram, "uResolution");
    int timeLoc = glGetUniformLocation(shaderProgram, "uTime");

    while (!glfwWindowShouldClose(window)) {
        process_input(window);

        int width, height;
        glfwGetFramebufferSize(window, &width, &height);

        glUseProgram(shaderProgram);
        glUniform2f(resolutionLoc, (float)width, (float)height);
        glUniform1f(timeLoc, (float)glfwGetTime());

        glBindVertexArray(VAO);
        glDrawArrays(GL_TRIANGLES, 0, 6);

        glfwSwapBuffers(window);
        glfwPollEvents();
    }

    glDeleteVertexArrays(1, &VAO);
    glDeleteBuffers(1, &VBO);
    glDeleteProgram(shaderProgram);
    glfwTerminate();
}


void framebuffer_size_callback(GLFWwindow* window, int width, int height) {
    glViewport(0, 0, width, height);
}

void process_input(GLFWwindow* window) {
    if (glfwGetKey(window, GLFW_KEY_ESCAPE) == GLFW_PRESS) {
        glfwSetWindowShouldClose(window, true);
    }
}
