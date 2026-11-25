#pragma once

#include <glm/gtc/matrix_transform.hpp>

#include "common.hpp"

class Camera {
public:

    Camera(glm::vec3 new_pos, float new_theta, float new_phi, glm::vec3 world_up);

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

    void rotate_right() {
        if (!_is_initialized) return;
        roll += rot_speed;
    }

    void rotate_left() {
        if (!_is_initialized) return;
        roll -= rot_speed;
    }

    void cam_update(float theta, float phi);

    glm::vec3 pos;
    
    glm::vec3 forward;
    glm::vec3 right;
    glm::vec3 up;

    float move_speed = 0.10;
    float rot_speed = 0.02;
    float FOV = 90.0;
private:
    bool _is_initialized = false;
    glm::vec3 world_up;
    float roll = 0.0;
};