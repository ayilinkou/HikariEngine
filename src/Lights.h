#pragma once

class Light
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
    PointLight() : m_Pos({0.f, 0.f, 0.f}) {}
    PointLight(glm::vec3 pos) : m_Pos(pos) {}

	glm::vec3& GetPosition() { return m_Pos; }

    struct Data
    {
        glm::vec3 Color;
        float Intensity;
        glm::vec3 Pos;
        float Padding{};
    };

    Data GetData() const
    {
        return Data{.Color = m_Color, .Intensity = m_Intensity, .Pos = m_Pos};
    }

private:
    glm::vec3 m_Pos;
};

class DirectionalLight : public Light
{
public:
    DirectionalLight() { SetDirection({0.5f, -1.f, 0.5f}); }
    DirectionalLight(glm::vec3 dir) { SetDirection(dir); }
	
	glm::vec3 GetDirection() { return m_Dir; }

    void SetDirection(glm::vec3 dir) { m_Dir = glm::normalize(dir); }

    struct Data
    {
        glm::vec3 Color;
        float Intensity;
        glm::vec3 Dir;
        float Padding{};
    };

    Data GetData() const
    {
        return Data{.Color = m_Color, .Intensity = m_Intensity, .Dir = m_Dir};
    }

private:
    glm::vec3 m_Dir;
};
