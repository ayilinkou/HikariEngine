#pragma once

#include "glm/glm.hpp"

#include "Transform.h"

class Camera
{
public:
    Camera() = default;

    glm::vec3 GetPosition() const { return m_Transform.Position; }
    glm::mat4 GetViewMatrix() const { return m_View; }
    float GetMoveSpeed() const { return m_MoveSpeed; }
    glm::vec3 GetForwardVector() const { return m_ForwardVector; }
    glm::vec3 GetRightVector() const { return m_RightVector; }

    void Tick();

    void SetPosition(glm::vec3 newPos) { m_Transform.Position = newPos; }
    void AddPositionOffset(glm::vec3 offset) { m_Transform.Position += offset; }
    void Rotate(float x, float y);

private:
    void CalcViewMatrix();

private:
	Transform m_Transform;

	glm::mat4 m_View = glm::mat4(1.f);
    glm::mat4 m_RotationMatrix = glm::mat4(1.f);

    glm::vec3 m_ForwardVector = {0.f, 0.f, -1.f};
    glm::vec3 m_UpVector = {0.f, 1.f, 0.f};
    glm::vec3 m_RightVector = {1.f, 0.f, 0.f};
    float m_MoveSpeed = 10.f;
    float m_LookSens = 0.1f;
};
