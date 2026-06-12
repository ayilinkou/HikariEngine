#pragma once

#include "vulkan/vulkan_raii.hpp"

#include <string>

class Material
{
public:
    Material() = delete;
    Material(const std::string& name);
    virtual ~Material() = 0;

	virtual void* GetPushConstantData() = 0;
    vk::DescriptorSet GetDescriptorSet() const { return *m_DescriptorSet; }
    const std::string& GetName() const { return m_Name; }
	bool IsTwoSided() const { return m_bTwoSided; }
	bool IsOpaque() const { return m_Opacity == 1.f; }

protected:
    vk::raii::DescriptorSet m_DescriptorSet = nullptr;

    const std::string m_Name;

	bool m_bTwoSided = false;
	float m_Opacity = 1.f;
};

inline Material::Material(const std::string& name) : m_Name(name) {}
inline Material::~Material() = default;
