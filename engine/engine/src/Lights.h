#pragma once

#include <cstdint>

#include "glm/glm.hpp"

#include "SceneComponent.h"
#include "shaders/ShaderTypes.h"

enum class LightType : uint8_t
{
    Point,
    Directional
};

class Light : public SceneComponent
{
protected:
    Light() {}

    float m_Intensity = 1.f;
    glm::vec3 m_Color = {1.f, 1.f, 1.f};

public:
    float& GetIntensity() { return m_Intensity; }
    glm::vec3& GetColor() { return m_Color; }

    void SetIntensity(float intensity) { m_Intensity = intensity; }
    void SetColor(glm::vec3 color) { m_Color = color; }
};

class PointLight : public Light
{
public:
    PointLight() {}
    PointLight(glm::vec3 pos) { m_Transform.Position = pos; }

    glm::vec3& GetPosition()
    {
        return m_Transform.Position;
    } // TODO: this should accumulate from parent components

    void SetPosition(glm::vec3 pos) { m_Transform.Position = pos; }

    /**
     * Shared with the shaders, so it is declared once in ShaderTypes.h and
     * aliased here. The name stays Data because that is what callers spell.
     */
    using Data = PointLightData;

    Data GetData() const
    {
        return Data{.Color = m_Color, .Intensity = m_Intensity, .Pos = m_Transform.Position};
    }
};

class DirectionalLight : public Light
{
public:
    DirectionalLight() { SetDirection({0.5f, -1.f, 0.5f}); }
    DirectionalLight(glm::vec3 dir) { SetDirection(dir); }

    glm::vec3 GetDirection() { return m_Dir; }

    void SetDirection(glm::vec3 dir) { m_Dir = glm::normalize(dir); }

    /** Shared with the shaders; declared once in ShaderTypes.h. */
    using Data = DirLightData;

    Data GetData() const { return Data{.Color = m_Color, .Intensity = m_Intensity, .Dir = m_Dir}; }

private:
    glm::vec3 m_Dir;
};
