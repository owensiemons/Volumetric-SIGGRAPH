#include <glad/glad.h>
#include <GLFW/glfw3.h>

#include <shaders/shader.h>

#include <glm/glm.hpp>
#include <glm/gtc/type_ptr.hpp>

#include <iostream>

void framebuffer_size_callback(GLFWwindow* window, int width, int height);
void process_input(GLFWwindow* window);

int main() {

    glfwInit();
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);

    glfwWindowHint(GLFW_OPENGL_FORWARD_COMPAT, GL_TRUE);

    GLFWwindow* window = glfwCreateWindow(800, 600, "Volumetric Rendering", NULL, NULL);
    if (window == NULL)
    {
        std::cout << "Failed to create GLFW window" << std::endl;
        glfwTerminate();
        return -1;
    }
    glfwMakeContextCurrent(window);
    glfwSetFramebufferSizeCallback(window, framebuffer_size_callback);

    if (!gladLoadGLLoader((GLADloadproc)glfwGetProcAddress)) {
        std::cout << "Failed to initialize GLAD" << std::endl;
        return -1;
    }

    Shader shadertri("include/shaders/vertShader.GLSL", "include/shaders/fragShader.GLSL");

    float quadVertices[] = {
        -1.0f,  1.0f,  0.0f, 1.0f,
        -1.0f, -1.0f,  0.0f, 0.0f,
         1.0f, -1.0f,  1.0f, 0.0f,

        -1.0f,  1.0f,  0.0f, 1.0f,
         1.0f, -1.0f,  1.0f, 0.0f,
         1.0f,  1.0f,  1.0f, 1.0f
    };

    unsigned int VBO, VAO;
    glGenVertexArrays(1, &VAO);
    glGenBuffers(1, &VBO);

    glBindVertexArray(VAO);

    glBindBuffer(GL_ARRAY_BUFFER, VBO);
    glBufferData(GL_ARRAY_BUFFER, sizeof(quadVertices), &quadVertices, GL_STATIC_DRAW);
    
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(float), (void*)0);

    glEnableVertexAttribArray(1);
    glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(float), (void*)(2 * sizeof(float)));

    while (!glfwWindowShouldClose(window)) {
        process_input(window);

        int width, height;
        glfwGetFramebufferSize(window, &width, &height);
        glViewport(0, 0, width, height);

        glClearColor(1.0f, 1.0f, 1.0f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT);

        shadertri.use();

        glUniform2f(glGetUniformLocation(shadertri.ID, "u_resolution"), (float)width, (float)height);
        glUniform3f(glGetUniformLocation(shadertri.ID, "u_sphere_center"), 0.0f, 0.0f, -2.0f);
        glUniform1f(glGetUniformLocation(shadertri.ID, "u_radius"), 1.0f);
        glUniform1f(glGetUniformLocation(shadertri.ID, "u_sigma_a"), 0.5f);
        glUniform1f(glGetUniformLocation(shadertri.ID, "u_sigma_s"), 0.5f);
        glUniform1f(glGetUniformLocation(shadertri.ID, "u_g"), 0.0f);
        glUniform1f(glGetUniformLocation(shadertri.ID, "u_density"), 0.6f);
        glUniform3f(glGetUniformLocation(shadertri.ID, "u_scatter"), 0.8f, 0.2f, 0.3f);
        glUniform3f(glGetUniformLocation(shadertri.ID, "u_background_color"), 0.3f, 0.5f, 0.47f);
        glUniform3f(glGetUniformLocation(shadertri.ID, "u_light_color"), 20.0f, 20.0f, 20.0f);
        glUniform3f(glGetUniformLocation(shadertri.ID, "u_light_position"), 0.0f, 2.0f, -2.0f);  
        

        glBindVertexArray(VAO);
        glDrawArrays(GL_TRIANGLES, 0, 6);

        glfwSwapBuffers(window);
        glfwPollEvents();
    }

    glDeleteVertexArrays(1, &VAO);
    glDeleteBuffers(1, &VBO);

    glfwTerminate();
    return 0;
}

void framebuffer_size_callback(GLFWwindow* window, int width, int height) {
    glViewport(0, 0, width, height);
}

void process_input(GLFWwindow* window) {
    if (glfwGetKey(window, GLFW_KEY_ESCAPE) == GLFW_PRESS) {
        glfwSetWindowShouldClose(window, true);
    }
}
