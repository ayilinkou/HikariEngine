#pragma once

#include <cstdint>
#include <exception>
#include <memory>
#include <string>

#include <rhi/IDevice.h>
#include <rhi/RhiTypes.h>
#include <rhi/UploadContext.h>

#include <platform/Paths.h>

#include <core/Log.h>

#include "ResourceCache.h"

inline constexpr LogCategory LogResourceManager{"Resource Manager"};

struct CubemapCreateInfo;

class Texture;
class Cubemap;
class ModelData;

class ResourceManager
{
private:
    ResourceManager(Rhi::IUploadContext& uploadContext, const Paths& paths)
        : m_UploadContext(uploadContext), m_Paths(paths)
    {
    }

public:
    static void Init(Rhi::IDevice& rhiDevice, Rhi::IUploadContext& uploadContext,
                     const Paths& paths);
    static ResourceManager* Get() { return s_Instance; }

private:
    inline static ResourceManager* s_Instance = nullptr;

public:
    static void PurgeCaches();
    static void Shutdown();

    std::shared_ptr<Texture> LoadTexture(const std::string& filepath, const Rhi::Format format);
    std::shared_ptr<Cubemap> LoadCubemap(const CubemapCreateInfo& createInfo);
    std::shared_ptr<ModelData> LoadModel(const std::string& modelPath);

private:
    // Flushes the upload context when the outermost load finishes.
    //
    // The nesting matters both ways. Loading a model loads its textures through
    // this same class, so flushing on every call would put each texture back in
    // its own submission and undo the batching entirely — Sponza's 77 became a
    // handful precisely because one model is one scope. And flushing when the
    // outermost one ends is what makes "a resource ResourceManager returns is on
    // the GPU" true by construction rather than by remembering.
    class LoadScope
    {
    public:
        explicit LoadScope(ResourceManager& owner) : m_Owner(owner) { ++m_Owner.m_LoadDepth; }

        // Flushing here can fail — it waits on the GPU — and a destructor that
        // throws while an exception from a failed load is already unwinding
        // terminates the process. Reported and swallowed instead, because by
        // this point the load has failed anyway and the useful error is the one
        // already in flight.
        ~LoadScope()
        {
            if (--m_Owner.m_LoadDepth != 0u)
                return;

            try
            {
                m_Owner.m_UploadContext.Flush();
            }
            catch (const std::exception& error)
            {
                LogMsg(LogSeverity::Error, LogResourceManager,
                       "Flushing pending uploads failed: {}", error.what());
            }
        }

        LoadScope(const LoadScope&) = delete;
        LoadScope& operator=(const LoadScope&) = delete;

    private:
        ResourceManager& m_Owner;
    };

    Rhi::IUploadContext& m_UploadContext;
    uint32_t m_LoadDepth = 0u;

    // Asset paths arrive here content-relative (a Model keeps the path it was
    // serialized with) and are resolved against the content root here, at the
    // point of loading.
    const Paths& m_Paths;

    ResourceCache<Texture> m_TextureCache;
    ResourceCache<Cubemap> m_CubemapCache;
    ResourceCache<ModelData> m_ModelCache;
};
