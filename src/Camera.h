#pragma once

#include "glm/vec3.hpp"

class Camera
{
public:
    Camera();

    glm::vec3 GetPosition() const { return m_Position; }
	float GetSpeed() const { return m_Speed; }

    void SetPosition(glm::vec3 newPos);
    void AddPositionOffset(glm::vec3 offset);

private:
    glm::vec3 m_Position;
    float m_Speed = 10.f;
};
