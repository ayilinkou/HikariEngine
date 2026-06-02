#pragma once

#include "glm/glm.hpp"
#include <glm/gtc/quaternion.hpp>

struct Transform
{
    glm::vec3 Position = {0.f, 0.f, 0.f};
    glm::vec3 Rotation = {0.f, 0.f, 0.f};
    glm::vec3 Scale = {1.f, 1.f, 1.f};

    // Order: Scale -> Rotation -> Translation. Translation is NOT affected by
    // scale.
    glm::mat4 ToWorldMatrix() const
    {
        glm::mat4 mat(1.f);
        mat = glm::scale(mat, Scale);
        mat = glm::mat4_cast(glm::quat(glm::radians(Rotation))) * mat;
        mat[3] = glm::vec4(Position, 1.f);
        return mat;
    };

    // Order: Scale -> Rotation -> Translation. Translation IS affected by
    // scale.
    glm::mat4 ToLocalMatrix() const
    {
        glm::mat4 mat(1.f);
        mat = glm::scale(mat, Scale);
        mat = glm::mat4_cast(glm::quat(glm::radians(Rotation))) * mat;
        mat = glm::translate(glm::mat4(1.f), Position) * mat;
        return mat;
    };

    glm::mat4 ToRotationMatrix() const
    {
        return glm::mat4_cast(glm::quat(glm::radians(Rotation)));
    }
};
