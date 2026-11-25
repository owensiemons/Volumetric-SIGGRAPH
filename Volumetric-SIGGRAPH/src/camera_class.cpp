#include "camera_class.hpp"


Camera::Camera(glm::vec3 new_pos, float theta, float phi, glm::vec3 new_world_up) {
    pos = new_pos;
    world_up = new_world_up;

    forward = glm::vec3(cos(theta) * cos(phi), sin(phi), cos(phi) * sin(theta));
    forward = glm::normalize(forward);
    right = glm::normalize(glm::cross(forward, glm::normalize(world_up)));
    up = glm::normalize(glm::cross(right, forward));

    _is_initialized = true;
}

void Camera::cam_update(float theta, float phi) {
    forward = glm::vec3(cos(theta) * cos(phi), sin(phi), cos(phi) * sin(theta));
    forward = glm::normalize(forward);
    right = glm::normalize(glm::cross(forward, glm::normalize(world_up)));
    up = glm::normalize(glm::cross(right, forward));

    glm::mat4 roll_rot = glm::rotate(glm::mat4(1.0), roll, forward);
    right = glm::vec3(roll_rot * glm::vec4(right, 0.0));
    up = glm::vec3(roll_rot * glm::vec4(up, 0.0));
}