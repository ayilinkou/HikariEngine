#include "Camera.h"

Camera::Camera()
{
    m_View = glm::mat4(1.f);
	m_FOV = 90.f;
	m_NearPlane = 0.1f;
	m_FarPlane = 10000.f;
    m_Proj = glm::perspective(glm::radians(m_FOV), 16.f / 9.f, m_NearPlane, m_FarPlane);
    m_RotationMatrix = glm::mat4(1.f);

    m_ForwardVector = {0.f, 0.f, -1.f};
    m_UpVector = {0.f, 1.f, 0.f};
    m_RightVector = {1.f, 0.f, 0.f};

    m_MoveSpeed = 5.f;
    m_LookSens = 0.1f;
}

void Camera::Tick() { CalcViewMatrix(); }

void Camera::CalcViewMatrix()
{
    m_RotationMatrix = m_Transform.ToRotationMatrix();

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

void Camera::SetProjection(float fov, float aspect, float near, float far)
{
	m_FOV = fov;
    m_NearPlane = near;
    m_FarPlane = far;
	m_Proj = glm::perspective(glm::radians(fov), aspect, near, far);
}
