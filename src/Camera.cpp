#include "Camera.h"

#include "glm/ext/matrix_transform.hpp"
#include <glm/ext/vector_float3.hpp>
#include <glm/trigonometric.hpp>

void Camera::Tick() { CalcViewMatrix(); }

void Camera::CalcViewMatrix()
{
    float pitch = glm::radians(m_Transform.Rotation.x);
    float yaw = glm::radians(m_Transform.Rotation.y);

    m_RotationMatrix = glm::mat4(1.f);
    m_RotationMatrix =
        glm::rotate(m_RotationMatrix, yaw, glm::vec3(0.f, 1.f, 0.f));
    m_RotationMatrix =
        glm::rotate(m_RotationMatrix, pitch, glm::vec3(1.f, 0.f, 0.f));

    m_ForwardVector =
        glm::vec3(m_RotationMatrix * glm::vec4(0.f, 0.f, -1.f, 0.f));
    m_UpVector = glm::vec3(m_RotationMatrix * glm::vec4(0.f, 1.f, 0.f, 0.f));
    m_RightVector = glm::cross(m_ForwardVector, m_UpVector);

    glm::vec3 lookAtVector = m_Transform.Position + m_ForwardVector;

    m_View = glm::lookAt(m_Transform.Position, lookAtVector, m_UpVector);
}

void Camera::Rotate(float dx, float dy)
{
    m_Transform.Rotation.x -= dy * m_LookSens;
    m_Transform.Rotation.y -= dx * m_LookSens;

    m_Transform.Rotation.x = glm::clamp(m_Transform.Rotation.x, -89.f, 89.f);

    if (m_Transform.Rotation.y >= 360.f)
        m_Transform.Rotation.y -= 360.f;
    if (m_Transform.Rotation.y < 0.f)
        m_Transform.Rotation.y += 360.f;
}
