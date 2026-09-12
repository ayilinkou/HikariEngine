#pragma once

#include <memory>
#include <string>

#include "glm/glm.hpp"

#include <rhi/BindGroup.h>
#include <rhi/Handles.h>
#include <rhi/IDevice.h>
#include <rhi/UniqueHandle.h>

#include "Material.h"
#include "Texture.h"
#include "shaders/ShaderTypes.h"

struct aiMaterial;

class AssetRegistry;

class PBRMaterial : public Material
{
public:
    PBRMaterial(Hikari::Rhi::IDevice& rhiDevice, Hikari::Rhi::BindGroupLayoutHandle materialLayout,
                Hikari::Rhi::SamplerHandle sampler, aiMaterial* mat,
                const std::string& texturesParentFolder, AssetRegistry& assets);

    virtual void* GetPushConstantData() override { return &m_MatData; }

private:
    void LoadTextures(aiMaterial* mat, const std::string& texturesParentFolder,
                      AssetRegistry& assets);
    void CreateBindGroup(Hikari::Rhi::IDevice& rhiDevice,
                         Hikari::Rhi::BindGroupLayoutHandle materialLayout,
                         Hikari::Rhi::SamplerHandle sampler);

public:
    /**
     * The block pushed to the surface shaders, declared once in ShaderTypes.h
     * so that the two languages cannot drift. Opacity might be redundant and
     * could pack into Albedo.
     */
    using MaterialData = MaterialPushConstant;

private:
    std::shared_ptr<Texture> m_Albedo = nullptr;
    std::shared_ptr<Texture> m_Normal = nullptr;
    std::shared_ptr<Texture> m_MetallicRoughness = nullptr;

    MaterialData m_MatData{};
};
