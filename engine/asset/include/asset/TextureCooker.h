#pragma once

#include <filesystem>
#include <optional>
#include <string>
#include <vector>

#include <asset/PreparedTextureCook.h>
#include <asset/TextureCookBatchOptions.h>
#include <asset/TextureCookBatchResult.h>
#include <asset/TextureCookEncoder.h>
#include <asset/TextureCookRequest.h>
#include <asset/TextureCookResult.h>
#include <asset/TextureSettingsOverrides.h>

namespace Hikari::Asset
{

// CPU-only asset service; no device, renderer or rendering worker is involved.
class TextureCooker
{
public:
    explicit TextureCooker(std::filesystem::path sourceRoot, std::filesystem::path cookedRoot = {});

    // Append the settings suffix to a cwd-relative or absolute name. The parent
    // must exist; source images, content roots and an encoder are not required.
    static std::optional<std::filesystem::path>
    CreateTextureSettings(const std::filesystem::path& name,
                          const TextureSettingsOverrides& overrides, bool bOverwrite,
                          std::string& error);
    static std::optional<std::filesystem::path>
    CreateCubemapSettings(const std::filesystem::path& name,
                          const TextureSettingsOverrides& overrides, bool bOverwrite,
                          std::string& error);

    // Read-only request validation, path mapping and shared settings resolution.
    std::optional<PreparedTextureCook> Prepare(const TextureCookRequest& request,
                                               std::string& error,
                                               std::vector<std::string>& warnings) const;
    // Check for authored edits since preparation before publishing validated settings.
    bool SaveSettings(const PreparedTextureCook& prepared, std::string& error) const;
    TextureCookResult Cook(const TextureCookRequest& request) const;
    TextureCookResult Cook(const TextureCookRequest& request,
                           const TextureCookEncoder& encoder) const;
    /** Discover source assets and cook serially, retaining failures and later successes. */
    TextureCookBatchResult CookAll(const TextureCookBatchOptions& options = {}) const;
    TextureCookBatchResult CookAll(const TextureCookBatchOptions& options,
                                   const TextureCookEncoder& encoder) const;

private:
    TextureCookBatchResult CookBatch(const TextureCookBatchOptions& options,
                                     const TextureCookEncoder* encoder) const;
    std::filesystem::path m_SourceRoot;
    std::filesystem::path m_CookedRoot;
};

} // namespace Hikari::Asset
