#include "AssetRegistry.h"

#include <core/Log.h>
#include <core/MyMacros.h>

#include "Cubemap.h"
#include "ModelData.h"
#include "Texture.h"

using namespace Hikari;
using namespace Hikari::Core;
using namespace Hikari::Platform;

constexpr std::string_view fallbackTexturePrefix = "FallbackTexture";

AssetRegistry::AssetRegistry(Rhi::IDevice& rhiDevice, Rhi::IUploadContext& uploadContext,
                             const Paths& paths, MaterialFactory& materialFactory,
                             std::filesystem::path cookedRoot,
                             std::filesystem::path cookerExecutable, double textureEncodingQuality)
    : m_UploadContext(uploadContext), m_Paths(paths),
      m_TextureSource(paths.ContentRoot(), std::move(cookedRoot),
                      cookerExecutable.empty() ? std::filesystem::path(HIKARI_RUNTIME_COOKER_PATH)
                                               : std::move(cookerExecutable),
                      textureEncodingQuality),
      m_TextureLoader(rhiDevice, uploadContext, m_TextureSource),
      m_CubemapLoader(rhiDevice, uploadContext, m_TextureSource),
      m_ModelLoader(rhiDevice, uploadContext, *this, materialFactory)
{
    LogMsg(LogSeverity::Info, LogAssetRegistry, "Constructed");
}

AssetRegistry::~AssetRegistry()
{
    LogMsg(LogSeverity::Info, LogAssetRegistry, "Destroyed");

    // Every entry is weak, so a live one means a resource outliving the registry
    // that loaded it — and its GPU handles outliving the device that owns them.
    assert(m_TextureCache.LiveCount() == 0);
    assert(m_CubemapCache.LiveCount() == 0);
    assert(m_ModelCache.LiveCount() == 0);
}

void AssetRegistry::PurgeCaches()
{
    uint32_t count = 0u;
    count += m_TextureCache.Purge();
    count += m_CubemapCache.Purge();
    count += m_ModelCache.Purge();

    if (count > 0u)
        LogMsg(LogSeverity::Info, LogAssetRegistry, "Purged {} expired resource entries.", count);
}

std::shared_ptr<Texture> AssetRegistry::LoadTexture(const std::string& filepath,
                                                    Asset::TextureUsage expectedUsage)
{
    LoadScope uploads(*this);

    // Keyed on the resolved path so that the same file requested relatively
    // (from a scene) and absolutely (from a model's own texture references)
    // shares one cache entry.
    std::shared_ptr<Texture> tex;
    try
    {
        const std::string resolved = m_TextureSource.ResolvePath(filepath).string();
        tex = m_TextureCache.Get(resolved, [&] { return m_TextureLoader.Load(resolved); });
    }
    catch (const std::exception& error)
    {
        LogMsg(LogSeverity::Error, LogAssetRegistry, "{}: {}", filepath, error.what());
    }
    if (!tex)
    {
        const auto format = expectedUsage == Asset::TextureUsage::Color ? Rhi::Format::RGBA8Srgb
                                                                        : Rhi::Format::RGBA8Unorm;
        std::string fallbackTextureKey(fallbackTexturePrefix);
        fallbackTextureKey += std::to_string(static_cast<uint32_t>(format));
        return m_TextureCache.Get(fallbackTextureKey,
                                  [&] { return m_TextureLoader.LoadFallbackTexture(format); });
    }
    const auto& metadata = tex->GetMetadata();
    if (metadata.Usage != expectedUsage)
        LogMsg(LogSeverity::Warning, LogAssetRegistry,
               "{}: Material slot expects {} usage; saved usage is {} (settings: {}). "
               "Loading with saved settings. Review the source settings and recook.",
               tex->GetPath(), Asset::ToString(expectedUsage),
               metadata.Usage ? Asset::ToString(*metadata.Usage) : "unspecified",
               tex->GetSettingsPath().empty() ? "cooked metadata only" : tex->GetSettingsPath());
    return tex;
}

std::shared_ptr<Cubemap> AssetRegistry::LoadCubemap(const std::string& filepath,
                                                    const std::string& name)
{
    LoadScope uploads(*this);
    std::shared_ptr<Cubemap> cubemap;
    try
    {
        const std::string resolved = m_TextureSource.ResolvePath(filepath).string();
        cubemap =
            m_CubemapCache.Get(resolved, [&] { return m_CubemapLoader.Load(resolved, name); });
    }
    catch (const std::exception& error)
    {
        LogMsg(LogSeverity::Error, LogAssetRegistry, "{}: {}", filepath, error.what());
    }
    return cubemap ? cubemap
                   : m_CubemapCache.Get("FallbackCubemap",
                                        [&] { return m_CubemapLoader.LoadFallbackCubemap(); });
}

std::shared_ptr<ModelData> AssetRegistry::LoadModel(const std::string& modelPath)
{
    LoadScope uploads(*this);

    // ModelLoader derives its texture directory from the path it is given, so
    // handing it the resolved path is also what makes the model's own texture
    // references resolve.
    const std::string resolved = m_Paths.Content(modelPath).string();
    return m_ModelCache.Get(resolved, [&] { return m_ModelLoader.Load(resolved); });
}
