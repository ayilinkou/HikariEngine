#pragma once

#include "vulkan/vulkan.hpp"
#include "vulkan/vulkan_raii.hpp"

class Material
{
public:
	virtual ~Material() = 0;

	vk::DescriptorSet GetDescriptorSet() const { return *m_DescriptorSet; }

protected:
    vk::raii::DescriptorSet m_DescriptorSet = nullptr;
};

inline Material::~Material() = default;
