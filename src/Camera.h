#pragma once

#include "glm/glm.hpp"

#include "SceneComponent.h"

class Camera : public SceneComponent
{
public:
    Camera();

    glm::vec3 GetPosition() const { return m_Transform.Position; }
    glm::mat4 GetViewMatrix() const { return m_View; }
    float GetMoveSpeed() const { return m_MoveSpeed; }
    glm::vec3 GetForwardVector() const { return m_ForwardVector; }
    glm::vec3 GetRightVector() const { return m_RightVector; }

    void Tick();

    void Rotate(float x, float y);

private:
    void CalcViewMatrix();

private:
    glm::mat4 m_View;
    glm::mat4 m_RotationMatrix;

    glm::vec3 m_ForwardVector;
    glm::vec3 m_UpVector;
    glm::vec3 m_RightVector;
    
	float m_MoveSpeed;
    float m_LookSens;
};
