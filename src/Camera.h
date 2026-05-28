#pragma once

#include "glm/glm.hpp"

class Camera
{
public:
    Camera();

    glm::vec3 GetPosition() const { return m_Position; }
    glm::mat4 GetViewMatrix() const { return m_View; }
    float GetSpeed() const { return m_Speed; }

	void CalcViewMatrix();

    void SetPosition(glm::vec3 newPos);
    void AddPositionOffset(glm::vec3 offset);

private:
    glm::vec3 m_Position;
    glm::mat4 m_View = glm::mat4(1.f);
    float m_Speed = 10.f;
};
