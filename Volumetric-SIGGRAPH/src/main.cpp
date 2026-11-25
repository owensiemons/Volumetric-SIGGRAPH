#include "common.hpp"
#include "shader_class.hpp"

void key_press_callback(GLFWwindow* window, int key, int scancode, int action, int mods);

const bool kIsOffline = false; 
const std::string kOutputFilename = "render_output.ppm";
const int kOfflineSamples = 50;

Scene scene;

int main() {
    if (!glfwInit()) return -1;
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
    glfwWindowHint(GLFW_OPENGL_FORWARD_COMPAT, GL_TRUE);

    GLFWwindow* window = glfwCreateWindow(1200, 900, "OpenGL Setup", nullptr, nullptr);
    if (!window) { glfwTerminate(); return -1; }
    glfwMakeContextCurrent(window);
    glfwSetFramebufferSizeCallback(window, framebuffer_size_callback);

    glfwSetKeyCallback(window, key_press_callback);

    if (!gladLoadGLLoader((GLADloadproc)glfwGetProcAddress)) {
        std::cout << "Failed to init GLAD\n";
        return -1;
    }


    scene.window = window;
    
    glfwGetFramebufferSize(window, &scene.snapshot_width, &scene.snapshot_height);

    const char* vert_path = "src/shaders/vert.glsl";
    const char* frag_path = "src/shaders/frag.glsl";

    Shader main_shaders(frag_path, vert_path);

    const char* realtime_post_path = "src/shaders/realtime_post.glsl";

    Shader rp_shaders(realtime_post_path, vert_path);

    const char* final_post_path = "src/shaders/final_post.glsl";

    Shader fp_shaders(final_post_path, vert_path);

    // == INIT DATA ==

    int resLoc = glGetUniformLocation(main_shaders.getProgram(), "iResolution");
    int frameLoc = glGetUniformLocation(main_shaders.getProgram(), "frame");
    int postLoc = glGetUniformLocation(main_shaders.getProgram(), "postprocess");

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

    // generate textures and FBOS
    unsigned int accumFBO;
    unsigned int accumTex;
    unsigned int finalFBO;
    unsigned int finalTex;
    
    {
        glGenFramebuffers(1, &accumFBO);
        glBindFramebuffer(GL_FRAMEBUFFER, accumFBO);

        glGenTextures(1, &accumTex);
        glBindTexture(GL_TEXTURE_2D, accumTex);

        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA32F, scene.snapshot_width, scene.snapshot_height, 0, GL_RGBA, GL_FLOAT, 0);

        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);

        glFramebufferTexture(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, accumTex, 0);

        glGenFramebuffers(1, &finalFBO);
        glBindFramebuffer(GL_FRAMEBUFFER, finalFBO);
        
        glGenTextures(1, &finalTex);
        glBindTexture(GL_TEXTURE_2D, finalTex);

        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA16, scene.snapshot_width, scene.snapshot_height, 0, GL_RGBA, GL_UNSIGNED_SHORT, 0);

        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);

        glFramebufferTexture(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, finalTex, 0);
    }

    int frame = 0;
    using Clock = std::chrono::steady_clock;
    auto fps_start = Clock::now();
    int fps_frame_cnt = 0;

    while (!glfwWindowShouldClose(window)) {
        if (!scene.accumulate_requested) { // realtime / preview path
            scene.realtime = true;

            glBindFramebuffer(GL_FRAMEBUFFER, 0);

            glClearColor(0.2f, 0.3f, 0.3f, 1.0f);
            glClear(GL_COLOR_BUFFER_BIT);

            glUseProgram(main_shaders.getProgram());

            int fbW, fbH;
            glfwGetFramebufferSize(window, &fbW, &fbH);

            glUniform2f(resLoc, (float)fbW, (float)fbH);
            glUniform1i(frameLoc, frame);
            glUniform1i(postLoc, 1);

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

        } else { // accumulate / snapshot path
            scene.realtime = false;
            scene.accumulate_requested = false;
            scene.accumulating = true;

            glBindFramebuffer(GL_FRAMEBUFFER, accumFBO);
            glViewport(0, 0, scene.snapshot_width, scene.snapshot_height);
            glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
            glClear(GL_COLOR_BUFFER_BIT);

            glBindVertexArray(VAO);
            
            for (int i = 0; i < kOfflineSamples; i++) {
                // draw to accumulate texture
                glBindFramebuffer(GL_FRAMEBUFFER, accumFBO);
                glViewport(0, 0, scene.snapshot_width, scene.snapshot_height);
                glEnable(GL_BLEND);
                glBlendFunc(GL_CONSTANT_ALPHA, GL_ONE_MINUS_CONSTANT_ALPHA);

                glUseProgram(main_shaders.getProgram());
                glUniform2f(resLoc, (float)scene.snapshot_width, (float)scene.snapshot_height);
                glUniform1i(frameLoc, frame++);
                glUniform1i(postLoc, 0);


                glBlendColor(0, 0, 0, 1.0f / (i + 1));
                glDrawElements(GL_TRIANGLES, 6, GL_UNSIGNED_INT, 0);
                glDisable(GL_BLEND);
                
                // draw to screen with tonemap and stuff
                glBindFramebuffer(GL_FRAMEBUFFER, 0);

                int fbW, fbH;
                glfwGetFramebufferSize(window, &fbW, &fbH);
                glViewport(0, 0, fbW, fbH);

                glUseProgram(rp_shaders.getProgram());
                glActiveTexture(GL_TEXTURE0);
                glBindTexture(GL_TEXTURE_2D, accumTex);
                glDrawElements(GL_TRIANGLES, 6, GL_UNSIGNED_INT, 0);
                
                glfwSwapBuffers(window);
                glfwPollEvents();
                glFinish();
            }

            // final post-process pass for png
            glBindFramebuffer(GL_FRAMEBUFFER, finalFBO);
            glViewport(0, 0, scene.snapshot_width, scene.snapshot_height);
            glUseProgram(fp_shaders.getProgram());
            glActiveTexture(GL_TEXTURE0);
            glBindTexture(GL_TEXTURE_2D, accumTex);
            glDrawElements(GL_TRIANGLES, 6, GL_UNSIGNED_INT, 0);
            
            // save to file
            std::vector<unsigned short> pixels(scene.snapshot_width * scene.snapshot_height * 4);
            glPixelStorei(GL_PACK_ALIGNMENT, 1);
            glReadPixels(0, 0, scene.snapshot_width, scene.snapshot_height, GL_RGBA, GL_UNSIGNED_SHORT, pixels.data());
            save_image(("renders/render_" + std::to_string(scene.curr_render++) + ".png").c_str(), scene.snapshot_width, scene.snapshot_height, pixels);
            scene.accumulating = false;
        }

    }
    glfwTerminate();
    return 0;
}

void key_press_callback(GLFWwindow* window, int key, int scancode, int action, int mods) {
    if (key == GLFW_KEY_C && action == GLFW_PRESS) {
        if(scene.realtime && !scene.accumulating) {
            scene.accumulate_requested = true;
        }
    }
}
