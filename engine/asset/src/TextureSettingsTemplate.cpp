#include <asset/TextureCooker.h>

#include <format>
#include <string_view>
#include <system_error>

#include <asset/CubemapDescriptor.h>

#include "CookFileUtil.h"

namespace Hikari::Asset
{
namespace
{
bool ValidateTemplateSettings(const TextureSettings& settings, std::string_view path,
                              std::string& error)
{
    const auto fail = [&](std::string_view field, std::string_view message)
    {
        error = std::format("{}: Field '{}': {}", path, field, message);
        return false;
    };
    const bool bOneChannel = settings.Format == TextureCompression::BC4 ||
                             settings.Format == TextureCompression::BC4Signed;
    const bool bTwoChannels = settings.Format == TextureCompression::BC5 ||
                              settings.Format == TextureCompression::BC5Signed;
    if ((bOneChannel || bTwoChannels) && settings.ColorSpace == TextureColorSpace::Srgb)
        return fail("colorSpace", "BC4/BC5 have no sRGB variants; select linear");
    if (settings.Usage == TextureUsage::Normal)
    {
        if (settings.ColorSpace == TextureColorSpace::Srgb)
            return fail("colorSpace", "Normal usage requires linear colour space");
        if (bOneChannel)
            return fail("format", "Normal usage requires at least two stored channels");
    }
    if (settings.bFlipNormalY && settings.Usage != TextureUsage::Normal)
        return fail("flipNormalY", "Enabled normal-Y flipping requires normal usage");
    if (settings.AlphaMode == TextureAlphaMode::Mask && (bOneChannel || bTwoChannels))
        return fail("alphaMode", "Mask usage requires an alpha-capable format");
    // Auto fields and DDS-dependent constraints are resolved when source images
    // exist. In particular, a masked DDS can already contain its mip chain.
    return true;
}

std::optional<std::filesystem::path>
CreateSettingsTemplate(const std::filesystem::path& name, const TextureSettingsOverrides& overrides,
                       bool bOverwrite, bool bCubemap, std::string& error)
{
    namespace fs = std::filesystem;
    error.clear();
    const auto filename = name.filename();
    if (filename.empty() || filename == "." || filename == ".." ||
        name.native().find(fs::path::value_type{}) != fs::path::string_type::npos ||
        (name.has_root_path() && !name.is_absolute()))
    {
        error = "A nonempty cwd-relative or absolute template name is required";
        return std::nullopt;
    }
    std::error_code ec;
    auto path = fs::absolute(name, ec);
    if (ec)
    {
        error = std::format("{}: Cannot resolve template path: {}", name.string(), ec.message());
        return std::nullopt;
    }
    path += bCubemap ? ".cubemap.json" : ".texture.json";
    if (!fs::is_directory(path.parent_path(), ec))
    {
        error = std::format("{}: Parent directory must already exist{}", path.string(),
                            ec ? std::format(": {}", ec.message()) : "");
        return std::nullopt;
    }
    const auto parent = fs::canonical(path.parent_path(), ec);
    if (ec)
    {
        error = std::format("{}: Cannot resolve parent directory: {}", path.string(), ec.message());
        return std::nullopt;
    }
    path = parent / path.filename();
    const auto status = fs::symlink_status(path, ec);
    if (ec == std::errc::no_such_file_or_directory)
        ec.clear();
    if (ec || (fs::exists(status) && !fs::is_regular_file(status)))
    {
        error = std::format("{}: Template destination must be a regular file{}", path.string(),
                            ec ? std::format(": {}", ec.message()) : "");
        return std::nullopt;
    }
    if (fs::exists(status) && !bOverwrite)
    {
        error =
            std::format("{}: Destination already exists; use --overwrite or --force to replace it",
                        path.string());
        return std::nullopt;
    }
    const auto settings = ApplyTextureSettingsOverrides(TextureSettings{}, overrides);
    const auto text = bCubemap ? SerializeCubemapTemplate(settings, path.string(), error)
                               : SerializeTextureSettings(settings, path.string(), error);
    if (!text || !ValidateTemplateSettings(settings, path.string(), error) ||
        !WriteCookText(path, *text, error, bOverwrite))
        return std::nullopt;
    return path;
}
} // namespace

std::optional<std::filesystem::path>
TextureCooker::CreateTextureSettings(const std::filesystem::path& name,
                                     const TextureSettingsOverrides& overrides, bool bOverwrite,
                                     std::string& error)
{
    return CreateSettingsTemplate(name, overrides, bOverwrite, false, error);
}

std::optional<std::filesystem::path>
TextureCooker::CreateCubemapSettings(const std::filesystem::path& name,
                                     const TextureSettingsOverrides& overrides, bool bOverwrite,
                                     std::string& error)
{
    return CreateSettingsTemplate(name, overrides, bOverwrite, true, error);
}
} // namespace Hikari::Asset
