#include "camera_class.hpp"


Camera::Camera(glm::vec3 new_pos, glm::vec3 new_forward, glm::vec3 world_up) {
    pos = new_pos;
    forward = glm::normalize(new_forward);
    right = glm::normalize(glm::cross(forward, glm::normalize(world_up)));
    up = glm::normalize(glm::cross(right, forward));

    _is_initialized = true;
}