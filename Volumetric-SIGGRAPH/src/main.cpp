#include <glad/glad.h>
#include <GLFW/glfw3.h>
#include <iostream>
#include <fstream>
#include <sstream>
#include <string>

const char* VS = R"(
#version 330 core
void main() {
    vec2 p = vec2((gl_VertexID << 1) & 2, gl_VertexID & 2);
    gl_Position = vec4(p * 2.0 - 1.0, 0.0, 1.0);
}
)";

const char* FS = R"(
#version 330 core
out vec4 FragColor;
uniform vec2 uRes;

void main() {
    vec2 uv = (gl_FragCoord.xy * 2.0 - uRes) / uRes.y;

    vec3 ray_origin = vec3(0.0,0.0,3.0);
    vec3 ray_direction = normalize(vec3(uv,-1.5));

    vec3 col = vec3(0.05);
    float T = 1.0;
    float dt = 0.04;

    for (int i =0; i < 128; i++) {
        vec3 p = ray_origin + ray_direction * (float(i)*dt);
        float rho = 1.0 - smoothstep(0.0,1.0,length(p));
        float a = 1.0 - exp(-4.0*rho*dt);
        col += T * a * vec3(0.85,0.45,0.25);
        T *= (1.0-a);
        if (T<0.01) break;
    }
    FragColor = vec4(col,1.0);
}

)";

static unsigned int compile(const char* src, GLenum type) {
    unsigned int s = glCreateShader(type);
    glShaderSource(s,1,&src, nullptr);
    glCompileShader(s);
    int ok; glGetShaderiv(s,GL_COMPILE_STATUS, &ok);

    if (!ok) { char log[1024]; glGetShaderInfoLog(s,1024,0,log); std::cerr << log; }

    return s;
}




int main() {
    glfwInit();
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
    GLFWwindow* w = glfwCreateWindow(800, 600, "Volume", 0, 0);
    glfwMakeContextCurrent(w);
    gladLoadGLLoader((GLADloadproc)glfwGetProcAddress);

    unsigned int prog = glCreateProgram();
    glAttachShader(prog, compile(VS, GL_VERTEX_SHADER));
    glAttachShader(prog, compile(FS, GL_FRAGMENT_SHADER));
    glLinkProgram(prog);

    int resLoc = glGetUniformLocation(prog, "uRes");
    unsigned int vao; glGenVertexArrays(1, &vao);   // empty, but required

    while (!glfwWindowShouldClose(w)) {
        int fw, fh; glfwGetFramebufferSize(w, &fw, &fh);
        glViewport(0, 0, fw, fh);
        glUseProgram(prog);
        glUniform2f(resLoc, (float)fw, (float)fh);
        glBindVertexArray(vao);
        glDrawArrays(GL_TRIANGLES, 0, 3);           // 3 verts, no buffer
        glfwSwapBuffers(w);
        glfwPollEvents();
    }
    glfwTerminate();
}