#include "Camera.h"

#include "glm/ext/matrix_transform.hpp"

Camera::Camera() { m_Position = {0.f, 0.f, 0.f}; }

void Camera::SetPosition(glm::vec3 newPos) { m_Position = newPos; }

void Camera::AddPositionOffset(glm::vec3 offset) { m_Position += offset; }

void Camera::CalcViewMatrix()
{
    constexpr glm::vec3 upVector = {0.f, 1.f, 0.f};
    glm::vec3 lookAtVector = m_Position + glm::vec3(0.f, 0.f, -1.f);

    m_View = glm::lookAt(m_Position, lookAtVector, upVector);
}
