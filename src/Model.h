#pragma once

#include <string>
#include <cstdint>

#include "vulkan/vulkan.hpp"

#include <Component.h>

class ModelData;
class Material;

class Model : public Component
{
public:
    Model(const std::string& path);
    ~Model();

	vk::Buffer GetVertexBuffer() const;
    vk::Buffer GetIndexBuffer() const;

    uint32_t GetIndexCount() const;
    Material* GetMaterial() const;

private:
    ModelData* m_pModelData;
};
