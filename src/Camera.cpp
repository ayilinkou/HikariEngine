#include "Camera.h"

Camera::Camera() { m_Position = {0.f, 0.f, 0.f}; }

void Camera::SetPosition(glm::vec3 newPos) { m_Position = newPos; }

void Camera::AddPositionOffset(glm::vec3 offset) { m_Position += offset; }
