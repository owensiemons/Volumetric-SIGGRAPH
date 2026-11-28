#include "common.hpp"
#include "shader_class.hpp"
#include "camera_class.hpp"

void key_press_callback(GLFWwindow* window, int key, int scancode, int action, int mods);
void cursor_move_callback(GLFWwindow* window, double xpos, double ypos);
void cursor_press_callback(GLFWwindow* window, int button, int action, int mods);
void process_input(GLFWwindow* window, Camera& camera);

const bool kIsOffline = false; 
const std::string kOutputFilename = "render_output.ppm";
const int kOfflineSamples = 100;

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
    glfwSetMouseButtonCallback(window, cursor_press_callback);
    glfwSetCursorPosCallback(window, cursor_move_callback);

    if (!gladLoadGLLoader((GLADloadproc)glfwGetProcAddress)) {
        std::cout << "Failed to init GLAD\n";
        return -1;
    }

    Camera camera(glm::vec3(0.0, 0.0, 0.0), scene.theta, scene.phi, glm::vec3(0.0, 1.0, 0.0));
    scene.window = window;
    
    // until GUI, set snapshot dim to window dim
    glfwGetFramebufferSize(window, &scene.snapshot_width, &scene.snapshot_height);


    const char* vert_path = "src/shaders/vert.glsl";
    const char* realtime_frag_path = "src/shaders/realtime_frag.glsl";
    const char* realtime_post_path = "src/shaders/realtime_post.glsl";
    const char* snapshot_frag_path = "src/shaders/snapshot_frag.glsl";
    const char* snapshot_post_path = "src/shaders/snapshot_post.glsl";

    Shader realtime_shaders(realtime_frag_path, vert_path);
    Shader snapshot_shaders(snapshot_frag_path, vert_path);
    Shader rp_shaders(realtime_post_path, vert_path);
    Shader sp_shaders(snapshot_post_path, vert_path);

    // == INIT DATA ==

    int resLoc = glGetUniformLocation(realtime_shaders.getProgram(), "iResolution");
    int frameLoc = glGetUniformLocation(realtime_shaders.getProgram(), "frame");

    int camPosLoc = glGetUniformLocation(realtime_shaders.getProgram(), "CAMERA_POS");
    int camDirLoc = glGetUniformLocation(realtime_shaders.getProgram(), "CAMERA_DIR");
    int camUpLoc = glGetUniformLocation(realtime_shaders.getProgram(), "CAMERA_UP");
    int camRightLoc = glGetUniformLocation(realtime_shaders.getProgram(), "CAMERA_RIGHT");

    int resLocs = glGetUniformLocation(snapshot_shaders.getProgram(), "iResolution");
    int frameLocs = glGetUniformLocation(snapshot_shaders.getProgram(), "frame");

    int camPosLocs = glGetUniformLocation(snapshot_shaders.getProgram(), "CAMERA_POS");
    int camDirLocs = glGetUniformLocation(snapshot_shaders.getProgram(), "CAMERA_DIR");
    int camUpLocs = glGetUniformLocation(snapshot_shaders.getProgram(), "CAMERA_UP");
    int camRightLocs = glGetUniformLocation(snapshot_shaders.getProgram(), "CAMERA_RIGHT");

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
    unsigned int snapshotFBO;
    unsigned int snapshotTex;
    
    {
        glGenFramebuffers(1, &accumFBO);
        glBindFramebuffer(GL_FRAMEBUFFER, accumFBO);

        glGenTextures(1, &accumTex);
        glBindTexture(GL_TEXTURE_2D, accumTex);

        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA32F, scene.snapshot_width, scene.snapshot_height, 0, GL_RGBA, GL_FLOAT, 0);

        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);

        glFramebufferTexture(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, accumTex, 0);

        glGenFramebuffers(1, &snapshotFBO);
        glBindFramebuffer(GL_FRAMEBUFFER, snapshotFBO);
        
        glGenTextures(1, &snapshotTex);
        glBindTexture(GL_TEXTURE_2D, snapshotTex);

        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA16, scene.snapshot_width, scene.snapshot_height, 0, GL_RGBA, GL_UNSIGNED_SHORT, 0);

        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);

        glFramebufferTexture(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, snapshotTex, 0);
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

            realtime_shaders.useProgram();

            int fbW, fbH;
            glfwGetFramebufferSize(window, &fbW, &fbH);
            glViewport(0, 0, fbW, fbH);

            {
                glUniform2f(resLoc, (float)fbW, (float)fbH);
                glUniform1i(frameLoc, frame);
                glUniform3f(camPosLoc, camera.pos[0], camera.pos[1], camera.pos[2]);
                glUniform3f(camDirLoc, camera.forward[0], camera.forward[1], camera.forward[2]);
                glUniform3f(camUpLoc, camera.up[0], camera.up[1], camera.up[2]);
                glUniform3f(camRightLoc, camera.right[0], camera.right[1], camera.right[2]);
            }
            

            glBindVertexArray(VAO);
            glDrawElements(GL_TRIANGLES, 6, GL_UNSIGNED_INT, 0);

            glfwSwapBuffers(window);
            glfwPollEvents();
            process_input(window, camera);
            camera.cam_update(scene.theta, scene.phi);

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
                if (glfwWindowShouldClose(window)) {
                    goto end;
                }
                // draw to accumulate texture
                glBindFramebuffer(GL_FRAMEBUFFER, accumFBO);
                glViewport(0, 0, scene.snapshot_width, scene.snapshot_height);
                glEnable(GL_BLEND);
                glBlendFunc(GL_CONSTANT_ALPHA, GL_ONE_MINUS_CONSTANT_ALPHA);

                snapshot_shaders.useProgram();
                {
                    glUniform2f(resLocs, (float)scene.snapshot_width, (float)scene.snapshot_height);
                    glUniform1i(frameLocs, frame++);
                        
                    glUniform3f(camPosLocs, camera.pos[0], camera.pos[1], camera.pos[2]);
                    glUniform3f(camDirLocs, camera.forward[0], camera.forward[1], camera.forward[2]);
                    glUniform3f(camUpLocs, camera.up[0], camera.up[1], camera.up[2]);
                    glUniform3f(camRightLocs, camera.right[0], camera.right[1], camera.right[2]);
                }


                glBlendColor(0, 0, 0, 1.0f / (i + 1));
                glDrawElements(GL_TRIANGLES, 6, GL_UNSIGNED_INT, 0);
                glDisable(GL_BLEND);
                
                // draw to screen with tonemap and stuff
                glBindFramebuffer(GL_FRAMEBUFFER, 0);

                int fbW, fbH;
                glfwGetFramebufferSize(window, &fbW, &fbH);
                glViewport(0, 0, fbW, fbH);

                rp_shaders.useProgram();
                glActiveTexture(GL_TEXTURE0);
                glBindTexture(GL_TEXTURE_2D, accumTex);
                glDrawElements(GL_TRIANGLES, 6, GL_UNSIGNED_INT, 0);
                
                glfwSwapBuffers(window);
                glfwPollEvents();
                process_input(window, camera);
                glFinish();

                std::cout << "Sample " << i << " Complete" << std::endl;
            }

            // final post-process pass for png
            glBindFramebuffer(GL_FRAMEBUFFER, snapshotFBO);
            glViewport(0, 0, scene.snapshot_width, scene.snapshot_height);
            sp_shaders.useProgram();
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
    end:
    glfwTerminate();
    return 0;
}

void process_input(GLFWwindow* window, Camera& camera) {
    if (glfwGetKey(window, GLFW_KEY_ESCAPE) == GLFW_PRESS) {
        glfwSetWindowShouldClose(window, true);
    }

    // camera movement
    if (!scene.accumulate_requested && !scene.accumulating) {
        if (glfwGetKey(window, GLFW_KEY_W) == GLFW_PRESS) {
            camera.move_forward();
        }

        if (glfwGetKey(window, GLFW_KEY_S) == GLFW_PRESS) {
            camera.move_backward();
        }

        if (glfwGetKey(window, GLFW_KEY_D) == GLFW_PRESS) {
            camera.move_right();
        }

        if (glfwGetKey(window, GLFW_KEY_A) == GLFW_PRESS) {
            camera.move_left();
        }

        if (glfwGetKey(window, GLFW_KEY_SPACE) == GLFW_PRESS) {
            camera.move_up();
        }

        if (glfwGetKey(window, GLFW_KEY_LEFT_SHIFT) == GLFW_PRESS) {
            camera.move_down();
        }

        if (glfwGetKey(window, GLFW_KEY_Q) == GLFW_PRESS) {
            camera.rotate_left();
        }

        if (glfwGetKey(window, GLFW_KEY_E) == GLFW_PRESS) {
            camera.rotate_right();
        }
    }
}

void key_press_callback(GLFWwindow* window, int key, int scancode, int action, int mods) {
    if (key == GLFW_KEY_C && action == GLFW_PRESS) {
        if(scene.realtime && !scene.accumulating) {
            scene.accumulate_requested = true;
        }
    }
}

void cursor_press_callback(GLFWwindow* window, int button, int action, int mods) {
    if (button == GLFW_MOUSE_BUTTON_LEFT) {
        if (action == GLFW_PRESS) {
            scene.lmb_held = true;
            scene.first_mouse = true;
        } else if (action == GLFW_RELEASE) {
            scene.lmb_held = false;
            scene.first_mouse = true;
        }
    }
}

void cursor_move_callback(GLFWwindow* window, double xpos, double ypos) {
    float x_pos = static_cast<float>(xpos);
    float y_pos = static_cast<float>(ypos);

    if (!scene.lmb_held || scene.accumulate_requested || scene.accumulating || scene.first_mouse) {
        scene.last_mouse_x = x_pos;
        scene.last_mouse_y = y_pos;
        scene.first_mouse = false;
        return;
    }

    float dx = x_pos - scene.last_mouse_x;
    float dy = y_pos - scene.last_mouse_y;
    dx *= scene.mouse_sens;
    dy *= scene.mouse_sens;

    if (std::abs(dx) < 0.001 && std::abs(dy) < 0.001) {
        dx = 0;
        dy = 0;
    }

    scene.phi -= dy;
    scene.theta += dx;
    
    const float limit = glm::half_pi<float>() - 0.0001f;
    scene.phi = glm::clamp(scene.phi, -limit, limit);

    scene.last_mouse_x = x_pos;
    scene.last_mouse_y = y_pos;
}