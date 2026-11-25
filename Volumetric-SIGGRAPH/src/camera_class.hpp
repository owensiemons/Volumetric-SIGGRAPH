#pragma once

#include "common.hpp"

class Camera {
public:

    Camera(glm::vec3 new_pos, glm::vec3 new_forward, glm::vec3 world_up);

    void move_forward() {
        if (!_is_initialized) return;
        pos = pos + forward * move_speed;
    }

    void move_backward() {
        if (!_is_initialized) return;
        pos = pos - forward * move_speed;
    }
    
    void move_right() {
        if (!_is_initialized) return;
        pos = pos + right * move_speed;
    }

    void move_left() {
        if (!_is_initialized) return;
        pos = pos - right * move_speed;
    }

    void move_up() {
        if (!_is_initialized) return;
        pos = pos + up * move_speed;
    }

    void move_down() {
        if (!_is_initialized) return;
        pos = pos - up * move_speed;
    }

    glm::vec3 pos;
    
    glm::vec3 forward;
    glm::vec3 right;
    glm::vec3 up;

    float move_speed = 0.05;
    float rot_speed = 0.01;
    float FOV = 90.0;
private:
    bool _is_initialized = false;
};