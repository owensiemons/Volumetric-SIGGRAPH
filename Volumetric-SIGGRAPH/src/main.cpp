#include <glad/glad.h>
#include <GLFW/glfw3.h>

#include <iostream>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>
#include <chrono>
#include <cmath>
#include <algorithm>

void framebuffer_size_callback(GLFWwindow* window, int width, int height);
void process_input(GLFWwindow* window);
bool init_shaders(unsigned int vert_shader, unsigned int frag_shader, unsigned int shader_program, const char* vert_src, const char* frag_src);

const bool kIsOffline = true; 
const std::string kOutputFilename = "render_output.ppm";

const int kOfflineSamples = 32;
const float kExposure = 1.3f;

void tonemap(float* rgb) {
    for (int c = 0; c < 3; c++) {
        float m = rgb[c] * kExposure;
        m = 1.0f - std::exp(-std::max(m, 0.0f));
        rgb[c] = std::pow(std::clamp(m, 0.0f, 1.0f), 1.0f / 2.2f);
    }
}

bool saveFrameToImage(const std::string& filename, int width, int height, const std::vector<unsigned char>& pixels) {
    std::ofstream file(filename, std::ios::binary);
    file << "P6\n" << width << " " << height << "\n255\n";
    for (int y = height - 1; y >= 0; --y) {
        file.write(reinterpret_cast<const char*>(&pixels[y * width * 3]), width * 3);
    }
    file.close();
    if (!file) {
        std::cout << "[Offline] FAILED to write: " << filename << std::endl;
        return false;
    }
    std::cout << "[Offline] Completed: " << filename << std::endl;
    return true;
}

int main() {
    if (!glfwInit()) return -1;
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
    glfwWindowHint(GLFW_OPENGL_FORWARD_COMPAT, GL_TRUE);

    if (kIsOffline) glfwWindowHint(GLFW_VISIBLE, GL_FALSE);
    GLFWwindow* window = glfwCreateWindow(800, 450, "OpenGL Setup", nullptr, nullptr);
    if (!window) { glfwTerminate(); return -1; }
    glfwMakeContextCurrent(window);
    glfwSetFramebufferSizeCallback(window, framebuffer_size_callback);

    if (!gladLoadGLLoader((GLADloadproc)glfwGetProcAddress)) {
        std::cout << "Failed to init GLAD\n";
        return -1;
    }

    const char* vert_path = "src/shaders/vert.glsl";
    const char* frag_path = "src/shaders/frag.glsl";

    std::string vertexCode;
    std::string fragmentCode;
    {
        std::ifstream vShaderFile;
        std::ifstream fShaderFile;
        // ensure ifstream objects can throw exceptions:
        vShaderFile.exceptions(std::ifstream::failbit | std::ifstream::badbit);
        fShaderFile.exceptions(std::ifstream::failbit | std::ifstream::badbit);
        try
        {
            // open files
            vShaderFile.open(vert_path);
            fShaderFile.open(frag_path);
            std::stringstream vShaderStream, fShaderStream;
            // read file's buffer contents into streams
            vShaderStream << vShaderFile.rdbuf();
            fShaderStream << fShaderFile.rdbuf();
            // close file handlers
            vShaderFile.close();
            fShaderFile.close();
            // convert stream into string
            vertexCode = vShaderStream.str();
            fragmentCode = fShaderStream.str();
        }
        catch (std::ifstream::failure& e)
        {
            std::cout << "ERROR::SHADER::FILE_NOT_SUCCESSFULLY_READ: " << e.what() << std::endl;
            return -1;
        }
    }

    unsigned int vertexShader   = glCreateShader(GL_VERTEX_SHADER);
    unsigned int fragmentShader = glCreateShader(GL_FRAGMENT_SHADER);
    unsigned int shaderProgram  = glCreateProgram();

    if (!init_shaders(vertexShader, fragmentShader, shaderProgram, vertexCode.c_str(), fragmentCode.c_str())) {
        glfwTerminate();
        return -1;
    }

    // == INIT DATA ==

    int resLoc = glGetUniformLocation(shaderProgram, "iResolution");
    int frameLoc = glGetUniformLocation(shaderProgram, "frame");

    float vertices[] = {
         1.0f,  1.0f, 0.0f,
         1.0f, -1.0f, 0.0f,
        -1.0f, -1.0f, 0.0f,
        -1.0f,  1.0f, 0.0f
    };

    unsigned int indices[] = {
        0, 1, 3,
        1, 2, 3
    };

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

    int frame = 0;
    using Clock = std::chrono::steady_clock;
    auto fps_start = Clock::now();
    int fps_frame_cnt = 0;


    // == RENDER LOOP ==
    if (kIsOffline) {
        std::cout << "[Mode]Offline\n";

        glClearColor(0.0f, 0.0f, 0.0f, 1.0f);

        glUseProgram(shaderProgram);

        int fbW, fbH;
        glfwGetFramebufferSize(window, &fbW, &fbH);
        glViewport(0, 0, fbW, fbH);
        glUniform2f(resLoc, (float)fbW, (float)fbH);
        glUniform1i(frameLoc, frame);

        //example of how to set the camera position
        float cameraPos[3] = { 0.0f, 0.0f, 0.0f };

        //input parameter
        glUniform3fv(glGetUniformLocation(shaderProgram, "CAMERA_POS"), 1, cameraPos);

        glUniform1f(glGetUniformLocation(shaderProgram, "EXPOSURE"), kExposure);
        glUniform1i(glGetUniformLocation(shaderProgram, "rawOutput"), 1);

        glBindVertexArray(VAO);
        glPixelStorei(GL_PACK_ALIGNMENT, 1);

        unsigned int colorTex, fbo;
        glGenTextures(1, &colorTex);
        glBindTexture(GL_TEXTURE_2D, colorTex);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB32F, fbW, fbH, 0, GL_RGB, GL_FLOAT, nullptr);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
        glGenFramebuffers(1, &fbo);
        glBindFramebuffer(GL_FRAMEBUFFER, fbo);
        glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, colorTex, 0);
        if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE) {
            std::cout << "ERROR::OFFLINE::INCOMPLETE_FRAMEBUFFER\n";
            glfwTerminate();
            return -1;
        }

        std::vector<float> accum(fbW * fbH * 3, 0.0f);
        std::vector<float> hdr(fbW * fbH * 3);
        std::vector<unsigned char> pixels(fbW * fbH * 3);
        for (int i = 0; i < kOfflineSamples; i++) {
            glClear(GL_COLOR_BUFFER_BIT);
            glUniform1i(frameLoc, i);
            glDrawElements(GL_TRIANGLES, 6, GL_UNSIGNED_INT, 0);
            glReadPixels(0, 0, fbW, fbH, GL_RGB, GL_FLOAT, hdr.data());
            for (size_t j = 0; j < accum.size(); j++) {
                accum[j] += hdr[j];
            }
            std::cout << "[Mode]Offline " << i << "/" << kOfflineSamples << "\n";
        }

        glBindFramebuffer(GL_FRAMEBUFFER, 0);
        glDeleteFramebuffers(1, &fbo);
        glDeleteTextures(1, &colorTex);

        for (int p = 0; p < fbW * fbH; p++) {
            float rgb[3] = {
                accum[p * 3 + 0] / kOfflineSamples,
                accum[p * 3 + 1] / kOfflineSamples,
                accum[p * 3 + 2] / kOfflineSamples
            };
            tonemap(rgb);
            for (int c = 0; c < 3; c++) {
                pixels[p * 3 + c] = (unsigned char)std::lround(rgb[c] * 255.0f);
            }
        }

        bool saved = saveFrameToImage(kOutputFilename, fbW, fbH, pixels);

        glfwTerminate();
        return saved ? 0 : -1;
    }

    std::cout << "[Mode]Realtime\n";
    while (!glfwWindowShouldClose(window)) {
        glClearColor(0.2f, 0.3f, 0.3f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT);

        glUseProgram(shaderProgram);

        int fbW, fbH;
        glfwGetFramebufferSize(window, &fbW, &fbH);
        glViewport(0, 0, fbW, fbH);
        glUniform2f(resLoc, (float)fbW, (float)fbH);
        glUniform1i(frameLoc, frame);

        float cameraPos[3] = { 0.0f, 0.0f, 0.0f };

        glUniform3fv(glGetUniformLocation(shaderProgram, "CAMERA_POS"), 1, cameraPos);

        glUniform1f(glGetUniformLocation(shaderProgram, "EXPOSURE"), kExposure);
        glUniform1i(glGetUniformLocation(shaderProgram, "rawOutput"), 0);

        glBindVertexArray(VAO);
        glDrawElements(GL_TRIANGLES, 6, GL_UNSIGNED_INT, 0);

        glfwSwapBuffers(window);
        glfwPollEvents();
        process_input(window);

        frame++;
        // FPS tracking
        fps_frame_cnt++;

        auto currentTime = Clock::now();
        float elapsed = std::chrono::duration<float>(currentTime - fps_start).count();

        if (elapsed >= 1.0f) {
            float fps = fps_frame_cnt / elapsed;

            std::cout << "[FPS] " << fps << std::endl;

            fps_frame_cnt = 0;
            fps_start = currentTime;
        }
    }
    glfwTerminate();
    return 0;
}

// LearnOpenGL vertex / frag shader boilerplate
bool init_shaders(unsigned int vert_shader, unsigned int frag_shader, unsigned int shader_program, const char* vert_src, const char* frag_src) {
    int success;
    char infoLog[512];
    glShaderSource(vert_shader, 1, &vert_src, NULL);
    glCompileShader(vert_shader);
    glGetShaderiv(vert_shader, GL_COMPILE_STATUS, &success);
    if (!success)
    {
        glGetShaderInfoLog(vert_shader, 512, NULL, infoLog);
        std::cout << "ERROR::SHADER::VERTEX::COMPILATION_FAILED\n" << infoLog << std::endl;
        glDeleteShader(vert_shader);
        return false;
    }
    // fragment shader
    glShaderSource(frag_shader, 1, &frag_src, NULL);
    glCompileShader(frag_shader);
    // check for shader compile errors
    glGetShaderiv(frag_shader, GL_COMPILE_STATUS, &success);
    if (!success)
    {
        glGetShaderInfoLog(frag_shader, 512, NULL, infoLog);
        std::cout << "ERROR::SHADER::FRAGMENT::COMPILATION_FAILED\n" << infoLog << std::endl;
        glDeleteShader(vert_shader);
        glDeleteShader(frag_shader);
        return false;
    }


    // link shaders
    glAttachShader(shader_program, vert_shader);
    glAttachShader(shader_program, frag_shader);
    glLinkProgram(shader_program);
    // check for linking errors
    glGetProgramiv(shader_program, GL_LINK_STATUS, &success);
    glDeleteShader(vert_shader);
    glDeleteShader(frag_shader);
    if (!success) {
        glGetProgramInfoLog(shader_program, 512, NULL, infoLog);
        std::cout << "ERROR::SHADER::PROGRAM::LINKING_FAILED\n" << infoLog << std::endl;
        return false;
    }
    return true;
}


void framebuffer_size_callback(GLFWwindow* window, int width, int height) {
    glViewport(0, 0, width, height);
}

void process_input(GLFWwindow* window) {
    if (glfwGetKey(window, GLFW_KEY_ESCAPE) == GLFW_PRESS) {
        glfwSetWindowShouldClose(window, true);
    }
}
