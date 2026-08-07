#pragma once

#include "glm/glm.hpp"

#include "SceneComponent.h"

class Camera : public SceneComponent
{
public:
    Camera();

    glm::vec3 GetPosition() const { return m_Transform.Position; }
    glm::mat4 GetViewMatrix() const { return m_View; }
    glm::mat4 GetProjMatrix() const { return m_Proj; }
    float GetMoveSpeed() const { return m_MoveSpeed; }
    glm::vec3 GetForwardVector() const { return m_ForwardVector; }
    glm::vec3 GetRightVector() const { return m_RightVector; }
	float GetNearPlane() const { return m_NearPlane; }
	float GetFarPlane() const { return m_FarPlane; }

    void Tick();

    void Rotate(float x, float y);
    void SetProjection(const glm::mat4& newProj) { m_Proj = newProj; }
    
	// FOV in degrees
	void SetProjection(float fov, float aspect, float near, float far);

private:
    void CalcViewMatrix();

private:
    glm::mat4 m_View;
    glm::mat4 m_Proj;
    glm::mat4 m_RotationMatrix;
	float m_NearPlane;
	float m_FarPlane;

    glm::vec3 m_ForwardVector;
    glm::vec3 m_UpVector;
    glm::vec3 m_RightVector;

    float m_MoveSpeed;
    float m_LookSens;
};
