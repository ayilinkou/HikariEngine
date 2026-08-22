#pragma once

#include <memory>
#include <string>
#include <vector>

#include <rhi/IDevice.h>
#include <rhi/UploadContext.h>

#include "Material.h"

struct aiScene;

class ModelData;

class ModelLoader
{
private:
    friend class ResourceManager;

    ModelLoader(Rhi::IDevice& rhiDevice, Rhi::IUploadContext& uploadContext);

    static void Init(Rhi::IDevice& rhiDevice, Rhi::IUploadContext& uploadContext);
    static void Shutdown();

    static ModelLoader* Get() { return s_Instance; }

    [[nodiscard]] std::shared_ptr<ModelData> Load(const std::string& path);

    static std::vector<std::unique_ptr<Material>> LoadMaterials(const aiScene* pScene,
                                                                const std::string& modelRoot);

private:
    inline static ModelLoader* s_Instance = nullptr;

    Rhi::IDevice& m_RhiDevice;
    Rhi::IUploadContext& m_UploadContext;
};
