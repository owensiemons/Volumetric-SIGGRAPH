#include <glad/glad.h>
#include <GLFW/glfw3.h>

#include <iostream>
#include <fstream>
#include <sstream>
#include <string>

void process_input(GLFWwindow* window);
void init_shaders(unsigned int vert_shader, unsigned int frag_shader, unsigned int shader_program,
                  const char* vert_src, const char* frag_src);

const int kWindowWidth  = 1280;
const int kWindowHeight = 720;

int main() {
    if (!glfwInit()) return -1;
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
    glfwWindowHint(GLFW_OPENGL_FORWARD_COMPAT, GL_TRUE);

    GLFWwindow* window = glfwCreateWindow(kWindowWidth, kWindowHeight, "Renderer", NULL, NULL);
    if (!window) {
        std::cout << "Failed to create GLFW window" << std::endl;
        glfwTerminate();
        return -1;
    }
    glfwMakeContextCurrent(window);

    if (!gladLoadGLLoader((GLADloadproc)glfwGetProcAddress)) {
        std::cout << "Failed to initialize GLAD" << std::endl;
        return -1;
    }

    const char* vert_path = "src/vert.glsl";
    const char* frag_path = "src/frag.glsl";

    std::string vertexCode, fragmentCode;
    {
        std::ifstream vShaderFile(vert_path);
        std::ifstream fShaderFile(frag_path);
        if (!vShaderFile.is_open() || !fShaderFile.is_open()) {
            std::cout << "ERROR::SHADER::FILE_OPEN_FAILED\n";
            return -1;
        }
        std::stringstream vs, fs;
        vs << vShaderFile.rdbuf();
        fs << fShaderFile.rdbuf();
        vertexCode   = vs.str();
        fragmentCode = fs.str();
    }

    unsigned int vertexShader   = glCreateShader(GL_VERTEX_SHADER);
    unsigned int fragmentShader = glCreateShader(GL_FRAGMENT_SHADER);
    unsigned int shaderProgram  = glCreateProgram();
    init_shaders(vertexShader, fragmentShader, shaderProgram,
                 vertexCode.c_str(), fragmentCode.c_str());

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

    GLint loc_resolution = glGetUniformLocation(shaderProgram, "u_resolution");

    while (!glfwWindowShouldClose(window)) {
        glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT);

        glUseProgram(shaderProgram);
        int fbW, fbH;
        glfwGetFramebufferSize(window, &fbW, &fbH);
        glViewport(0, 0, fbW, fbH);
        glUniform2f(loc_resolution, (float)fbW, (float)fbH);

        glBindVertexArray(VAO);
        glDrawElements(GL_TRIANGLES, 6, GL_UNSIGNED_INT, 0);

        glfwSwapBuffers(window);
        glfwPollEvents();
        process_input(window);
    }

    glfwTerminate();
    return 0;
}

void init_shaders(unsigned int vert_shader, unsigned int frag_shader, unsigned int shader_program,
                  const char* vert_src, const char* frag_src) {
    int success;
    char infoLog[512];

    glShaderSource(vert_shader, 1, &vert_src, NULL);
    glCompileShader(vert_shader);
    glGetShaderiv(vert_shader, GL_COMPILE_STATUS, &success);
    if (!success) {
        glGetShaderInfoLog(vert_shader, 512, NULL, infoLog);
        std::cout << "ERROR::VERTEX::COMPILATION_FAILED\n" << infoLog << std::endl;
    }

    glShaderSource(frag_shader, 1, &frag_src, NULL);
    glCompileShader(frag_shader);
    glGetShaderiv(frag_shader, GL_COMPILE_STATUS, &success);
    if (!success) {
        glGetShaderInfoLog(frag_shader, 512, NULL, infoLog);
        std::cout << "ERROR::FRAGMENT::COMPILATION_FAILED\n" << infoLog << std::endl;
    }

    glAttachShader(shader_program, vert_shader);
    glAttachShader(shader_program, frag_shader);
    glLinkProgram(shader_program);
    glGetProgramiv(shader_program, GL_LINK_STATUS, &success);
    if (!success) {
        glGetProgramInfoLog(shader_program, 512, NULL, infoLog);
        std::cout << "ERROR::PROGRAM::LINKING_FAILED\n" << infoLog << std::endl;
    }
    glDeleteShader(vert_shader);
    glDeleteShader(frag_shader);
}

void process_input(GLFWwindow* window) {
    if (glfwGetKey(window, GLFW_KEY_ESCAPE) == GLFW_PRESS)
        glfwSetWindowShouldClose(window, true);
}
