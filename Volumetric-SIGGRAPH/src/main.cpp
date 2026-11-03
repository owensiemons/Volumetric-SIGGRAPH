#include <glad/glad.h>
#include <GLFW/glfw3.h>
#include <iostream>
#include <fstream>
#include <sstream>
#include <string>

#include <vector>
#include <cmath>

void process_input(GLFWwindow* window);
void init_shaders(unsigned int vert_shader, unsigned int frag_shader, unsigned int shader_program, const char* vert_src, const char* frag_src);

const bool kIsOffline = true; 
const int  kRenderWidth = 1600;
const int  kRenderHeight = 1200;
const std::string kOutputFilename = "render_output.ppm";

void saveFrameToImage(const std::string& filename, int width, int height) {
    std::vector<unsigned char> pixels(width * height * 3);
    glPixelStorei(GL_PACK_ALIGNMENT, 1);
    glReadPixels(0, 0, width, height, GL_RGB, GL_UNSIGNED_BYTE, pixels.data());

    std::ofstream file(filename, std::ios::binary);
    file << "P6\n" << width << " " << height << "\n255\n";
    for (int y = height - 1; y >= 0; --y) {
        file.write(reinterpret_cast<char*>(&pixels[y * width * 3]), width * 3);
    }
    file.close();
    std::cout << "[Offline Rendering] Image successfully saved to " << filename << std::endl;
}

int main() {
    if (!glfwInit()) return -1;
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);

    if (kIsOffline) {
        glfwWindowHint(GLFW_VISIBLE, GLFW_FALSE);
    }

    GLFWwindow* window = glfwCreateWindow(kRenderWidth, kRenderHeight, "Realistic Atmosphere Renderer", nullptr, nullptr);
    if (!window) { glfwTerminate(); return -1; }
    glfwMakeContextCurrent(window);

    if (!gladLoadGLLoader((GLADloadproc)glfwGetProcAddress)) {
        std::cout << "Failed to init GLAD\n";
        return -1;
    }

    const char* vert_path = "src/vert.glsl";
    const char* frag_path = "src/frag.glsl";
    std::string vertexCode, fragmentCode;
    try {
        std::ifstream vShaderFile(vert_path);
        std::ifstream fShaderFile(frag_path);
        std::stringstream vShaderStream, fShaderStream;
        vShaderStream << vShaderFile.rdbuf();
        fShaderStream << fShaderFile.rdbuf();
        vShaderFile.close();
        fShaderFile.close();
        vertexCode = vShaderStream.str();
        fragmentCode = fShaderStream.str();
    } catch (std::ifstream::failure& e) {
        std::cout << "ERROR::SHADER::FILE_NOT_SUCCESSFULLY_READ: " << e.what() << std::endl;
    }

    unsigned int vertexShader = glCreateShader(GL_VERTEX_SHADER);
    unsigned int fragmentShader = glCreateShader(GL_FRAGMENT_SHADER);
    unsigned int shaderProgram = glCreateProgram();
    
    init_shaders(vertexShader, fragmentShader, shaderProgram, vertexCode.c_str(), fragmentCode.c_str());

    float vertices[] = { 
         1.0f,  1.0f, 0.0f,
         1.0f, -1.0f, 0.0f,
        -1.0f, -1.0f, 0.0f,
        -1.0f,  1.0f, 0.0f
    };

    unsigned int indices[] = { 0, 1, 3, 1, 2, 3 };

    unsigned int VBO, VAO, EBO;
    glGenVertexArrays(1, &VAO);
    glGenBuffers(1, &VBO);
    glGenBuffers(1, &EBO);
    glBindVertexArray(VAO);

    glBindBuffer(GL_ARRAY_BUFFER, VBO);
    glBufferData(GL_ARRAY_BUFFER, sizeof(vertices), vertices, GL_STATIC_DRAW);

    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, EBO);
    glBufferData(GL_ELEMENT_ARRAY_BUFFER, sizeof(indices), indices, GL_STATIC_DRAW);

    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 3 * sizeof(float), (void*)0);
    glEnableVertexAttribArray(0);
    glBindBuffer(GL_ARRAY_BUFFER, 0);
    glBindVertexArray(0);


    GLint loc_resolution      = glGetUniformLocation(shaderProgram, "u_resolution");
    GLint loc_cameraPos       = glGetUniformLocation(shaderProgram, "u_cameraPos");
    GLint loc_cameraDir       = glGetUniformLocation(shaderProgram, "u_cameraDir");
    GLint loc_cameraUp        = glGetUniformLocation(shaderProgram, "u_cameraUp");
    GLint loc_tanHalfFov      = glGetUniformLocation(shaderProgram, "u_tanHalfFov");
    GLint loc_sunDir          = glGetUniformLocation(shaderProgram, "u_sunDir");
    GLint loc_sunColor        = glGetUniformLocation(shaderProgram, "u_sunColor");
    GLint loc_sunIntensity    = glGetUniformLocation(shaderProgram, "u_sunIntensity");
    GLint loc_planetRadius    = glGetUniformLocation(shaderProgram, "u_planetRadius");
    GLint loc_atmosphereRadius= glGetUniformLocation(shaderProgram, "u_atmosphereRadius");
    GLint loc_betaRayleigh    = glGetUniformLocation(shaderProgram, "u_betaRayleigh");
    GLint loc_rayleighScaleH  = glGetUniformLocation(shaderProgram, "u_rayleighScaleHeight");
    GLint loc_betaMie         = glGetUniformLocation(shaderProgram, "u_betaMie");
    GLint loc_mieScaleH       = glGetUniformLocation(shaderProgram, "u_mieScaleHeight");
    GLint loc_mieG            = glGetUniformLocation(shaderProgram, "u_mieG");
    GLint loc_exposure        = glGetUniformLocation(shaderProgram, "u_exposure");


    float fovDegrees = 60.0f;
    float tanHalfFov = tan(fovDegrees * 0.5f * 3.14159265359f / 180.0f);


    float planetRadius = 6375.0f; 
    float atmosphereRadius = 6560.0f;
    float cameraPos[3] = { 0.0f, planetRadius + 0.1f, 0.0f }; 
    float cameraDir[3] = { 0.0f, 0.5f, -1.0f };               
    float cameraUp[3]  = { 0.0f, 1.0f, 0.0f };

    float sunDir[3]    = { 0.0f, -0.05f, -0.988f }; 
    float sunColor[3]  = { 1.0f, 1.0f, 1.0f };
    float sunIntensity = 12.0f;

    float betaRayleigh[3] = { 5.8e-3f, 1.35e-2f, 3.31e-2f };
    float rayleighScaleH  = 8.0f;  // 8 km
    float betaMie[3]      = { 4.0e-3f, 4.0e-3f, 4.0e-3f };
    float mieScaleH       = 1.2f;  // 1.2 km
    float mieG            = 0.758f; 
    float exposure        = 1.5f;

    auto setUniforms = [&](int width, int height) {
        glUniform2f(loc_resolution, (float)width, (float)height);
        glUniform3fv(loc_cameraPos, 1, cameraPos);
        glUniform3fv(loc_cameraDir, 1, cameraDir);
        glUniform3fv(loc_cameraUp, 1, cameraUp);
        glUniform1f(loc_tanHalfFov, tanHalfFov);
        glUniform3fv(loc_sunDir, 1, sunDir);
        glUniform3fv(loc_sunColor, 1, sunColor);
        glUniform1f(loc_sunIntensity, sunIntensity);
        glUniform1f(loc_planetRadius, planetRadius);
        glUniform1f(loc_atmosphereRadius, atmosphereRadius);
        glUniform3fv(loc_betaRayleigh, 1, betaRayleigh);
        glUniform1f(loc_rayleighScaleH, rayleighScaleH);
        glUniform3fv(loc_betaMie, 1, betaMie);
        glUniform1f(loc_mieScaleH, mieScaleH);
        glUniform1f(loc_mieG, mieG);
        glUniform1f(loc_exposure, exposure);
    };

    if (kIsOffline) {
        std::cout << "[Mode]Offline\n";
        glViewport(0, 0, kRenderWidth, kRenderHeight);
        glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT);

        glUseProgram(shaderProgram);
        setUniforms(kRenderWidth, kRenderHeight);

        glBindVertexArray(VAO);
        glDrawElements(GL_TRIANGLES, 6, GL_UNSIGNED_INT, 0);

        saveFrameToImage(kOutputFilename, kRenderWidth, kRenderHeight);

        glfwTerminate();
        return 0;
    }

    std::cout << "[Mode]Realtime\n";
    while (!glfwWindowShouldClose(window)) {
        glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT);

        glUseProgram(shaderProgram);

        int fbW, fbH;
        glfwGetFramebufferSize(window, &fbW, &fbH);
        glViewport(0, 0, fbW, fbH);
        setUniforms(fbW, fbH);

        glBindVertexArray(VAO);
        glDrawElements(GL_TRIANGLES, 6, GL_UNSIGNED_INT, 0);

        glfwSwapBuffers(window);
        glfwPollEvents();
        process_input(window);
    }
    
    glfwTerminate();
    return 0;
}

void init_shaders(unsigned int vert_shader, unsigned int frag_shader, unsigned int shader_program, const char* vert_src, const char* frag_src) {
    int success;
    char infoLog[512];
    glShaderSource(vert_shader, 1, &vert_src, NULL);
    glCompileShader(vert_shader);
    glGetShaderiv(vert_shader, GL_COMPILE_STATUS, &success);
    if (!success) {
        glGetShaderInfoLog(vert_shader, 512, NULL, infoLog);
        std::cout << "ERROR::SHADER::VERTEX::COMPILATION_FAILED\n" << infoLog << std::endl;
    }
    glShaderSource(frag_shader, 1, &frag_src, NULL);
    glCompileShader(frag_shader);
    glGetShaderiv(frag_shader, GL_COMPILE_STATUS, &success);
    if (!success) {
        glGetShaderInfoLog(frag_shader, 512, NULL, infoLog);
        std::cout << "ERROR::SHADER::FRAGMENT::COMPILATION_FAILED\n" << infoLog << std::endl;
    }
    glAttachShader(shader_program, vert_shader);
    glAttachShader(shader_program, frag_shader);
    glLinkProgram(shader_program);
    glGetProgramiv(shader_program, GL_LINK_STATUS, &success);
    if (!success) {
        glGetProgramInfoLog(shader_program, 512, NULL, infoLog);
        std::cout << "ERROR::SHADER::PROGRAM::LINKING_FAILED\n" << infoLog << std::endl;
    }
    glDeleteShader(vert_shader);
    glDeleteShader(frag_shader);
}

void process_input(GLFWwindow* window) {
    if (glfwGetKey(window, GLFW_KEY_ESCAPE) == GLFW_PRESS) {
        glfwSetWindowShouldClose(window, true);
    }
}
