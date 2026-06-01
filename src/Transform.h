#pragma once

#include "glm/glm.hpp"

struct Transform
{
    glm::vec3 Position = {0.f, 0.f, 0.f};
    glm::vec3 Rotation = {0.f, 0.f, 0.f};
    glm::vec3 Scale = {1.f, 1.f, 1.f};
};
