#pragma once

#include <cstdint>
#include <string>

#include <rhi/Handles.h>
#include <rhi/IDevice.h>

#include "Material.h"
#include "PBRMaterial.h"

struct aiMaterial;

class MaterialFactory
{
public:
    static void Init(Rhi::IDevice& rhiDevice, Rhi::SamplerHandle sampler);
    static void Shutdown();

    static MaterialFactory* Get() { return s_Instance; }

    [[nodiscard]] PBRMaterial* CreatePBRMaterial(aiMaterial* mat,
                                                 const std::string& texturesParentFolder);

    vk::DescriptorSetLayout GetDescriptorSetLayout() const { return *m_SetLayout; }

private:
    MaterialFactory(Rhi::IDevice& rhiDevice, Rhi::SamplerHandle sampler);

    void CreateDescriptorPool();
    void CreateDescriptorSetLayout();

private:
    static MaterialFactory* s_Instance;

    vk::raii::DescriptorSetLayout m_SetLayout = nullptr;
    vk::raii::DescriptorPool m_DescriptorPool = nullptr;

    Rhi::IDevice& m_RhiDevice;

    // Descriptor pools and set layouts stay Vulkan objects for the whole of
    // Stage 5 — the binding model is deliberately not abstracted (plan D7) — so
    // the factory keeps a device reference to build them from.
    vk::raii::Device& m_Device;

    Rhi::SamplerHandle m_Sampler;

    static const uint8_t s_MAX_TEXTURE_COUNT_PER_MAT;
    static const uint16_t s_MAX_MATERIAL_SET_COUNT;
};
