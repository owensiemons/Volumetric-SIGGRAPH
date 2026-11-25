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
};

void framebuffer_size_callback(GLFWwindow* window, int width, int height);

void save_image(const char* filename, int width, int height, std::vector<unsigned short>& pixels);