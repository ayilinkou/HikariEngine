#pragma once

#include "glm/glm.hpp"

struct PointLight
{
    glm::vec3 Pos;
    float Intensity;
    glm::vec3 Color;
    float Padding;
};


