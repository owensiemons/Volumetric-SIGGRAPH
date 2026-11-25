#pragma once

#include <glad/glad.h>
#include <GLFW/glfw3.h>
#include <png.h>
#include <glm/glm.hpp>

#include <iostream>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>
#include <chrono>
#include <span>

struct Scene {
    bool realtime = false;
    bool accumulating = false;
    bool accumulate_requested = false;

    GLFWwindow* window;

    int snapshot_width = 0;
    int snapshot_height = 0;

    int curr_render = 1;

    bool first_mouse = true;
    bool lmb_held = false;
    float last_mouse_x = 0.0;
    float last_mouse_y = 0.0;
    float mouse_sens = 0.007;

    float phi = 0.2;
    float theta = -1.5;
};

void framebuffer_size_callback(GLFWwindow* window, int width, int height);

void save_image(const char* filename, int width, int height, std::vector<unsigned short>& pixels);