#include <asset/TextureCooker.h>

#include <algorithm>
#include <cctype>
#include <format>
#include <system_error>
#include <utility>

#include <asset/ImageReader.h>

#include "CookFileUtil.h"

namespace Hikari::Asset
{

TextureCooker::TextureCooker(std::filesystem::path sourceRoot, std::filesystem::path cookedRoot)
    : m_SourceRoot(sourceRoot.empty() ? std::filesystem::path{}
                                      : std::filesystem::absolute(sourceRoot)),
      m_CookedRoot(cookedRoot.empty() ? std::filesystem::path{}
                                      : std::filesystem::absolute(cookedRoot))
{
}

std::optional<PreparedTextureCook> TextureCooker::Prepare(const TextureCookRequest& request,
                                                          std::string& error,
                                                          std::vector<std::string>& warnings) const
{
    namespace fs = std::filesystem;
    error.clear();
    warnings.clear();
    if (request.EncoderThreads < 1 || request.EncoderThreads > 128)
    {
        error = "Encoder threads must be in [1, 128]";
        return std::nullopt;
    }
    const auto paths = MapTextureCookPaths(m_SourceRoot, m_CookedRoot, request.Source, error);
    if (!paths)
        return std::nullopt;
    PreparedTextureCook prepared;
    prepared.Request = request;
    prepared.Paths = *paths;
    TextureSettings saved;
    std::error_code ec;
    const bool bSettingsExist = fs::exists(paths->SettingsPath, ec);
    if (ec)
    {
        error = std::format("{}: Cannot inspect settings: {}", paths->SettingsPath.string(),
                            ec.message());
        return std::nullopt;
    }
    if (bSettingsExist)
    {
        prepared.OriginalSettings = ReadCookText(paths->SettingsPath, error);
        if (!prepared.OriginalSettings)
            return std::nullopt;
        if (paths->Kind == TextureCookSourceKind::CubemapDescriptor)
        {
            prepared.Descriptor = ParseCubemapDescriptor(*prepared.OriginalSettings,
                                                         paths->SettingsPath.string(), error);
            if (!prepared.Descriptor)
                return std::nullopt;
            saved = prepared.Descriptor->Settings;
        }
        else
        {
            const auto settings = ParseTextureSettings(*prepared.OriginalSettings,
                                                       paths->SettingsPath.string(), error);
            if (!settings)
                return std::nullopt;
            saved = *settings;
        }
    }
    if (paths->Kind == TextureCookSourceKind::Dds)
    {
        // Resolving auto to image defaults here would silently convert native
        // DDS blocks before the DDS metadata reader has supplied their format.
        error = std::format("{}: DDS metadata reading is not available yet",
                            paths->SourcePath.string());
        return std::nullopt;
    }
    if (prepared.Descriptor)
    {
        for (size_t i = 0; i < prepared.Descriptor->Faces.size(); ++i)
        {
            const auto face =
                fs::canonical(paths->SourcePath.parent_path() / prepared.Descriptor->Faces[i], ec);
            if (ec || !fs::is_regular_file(face, ec) ||
                !RelativeCookPath(face, paths->SourceRoot) ||
                RelativeCookPath(face, paths->CookedRoot))
            {
                error = std::format("{}: Field 'faces.{}': Face must be an existing image inside "
                                    "source root and outside cooked root",
                                    paths->SourcePath.string(), kCubemapFaceNames[i]);
                return std::nullopt;
            }
            std::string extension = face.extension().string();
            std::transform(extension.begin(), extension.end(), extension.begin(),
                           [](unsigned char ch) { return static_cast<char>(std::tolower(ch)); });
            if (extension != ".png" && extension != ".jpg" && extension != ".jpeg")
            {
                error = std::format(
                    "{}: Field 'faces.{}': {}", paths->SourcePath.string(), kCubemapFaceNames[i],
                    extension == ".dds" ? "DDS face metadata reading is not available yet"
                                        : "Expected a PNG/JPG face image");
                return std::nullopt;
            }
            prepared.Inputs.push_back(face);
        }
    }
    else
        prepared.Inputs.push_back(paths->SourcePath);

    std::vector<std::string> collectedWarnings;
    for (size_t i = 0; i < prepared.Inputs.size(); ++i)
    {
        const auto metadata = ReadImageInfo(prepared.Inputs[i].string(), error);
        if (!metadata)
        {
            if (prepared.Descriptor)
                error = std::format("{}: Field 'faces.{}': {}", paths->SourcePath.string(),
                                    kCubemapFaceNames[i], error);
            return std::nullopt;
        }
        if (prepared.Descriptor && (metadata->Extent.Width != metadata->Extent.Height ||
                                    (!prepared.ImageMetadata.empty() &&
                                     metadata->Extent != prepared.ImageMetadata.front().Extent)))
        {
            error = std::format(
                "{}: Field 'faces.{}': Cubemap faces must be square with matching dimensions",
                paths->SourcePath.string(), kCubemapFaceNames[i]);
            return std::nullopt;
        }
        prepared.ImageMetadata.push_back(*metadata);
        TextureSettingsSource source;
        source.Channels = metadata->Channels;
        source.bSettingsMissing = !bSettingsExist;
        source.bNeedsMipGeneration = metadata->Extent.Width > 1 || metadata->Extent.Height > 1;
        std::vector<std::string> imageWarnings;
        const auto resolved = ResolveTextureSettings(
            saved, request.Overrides, source, paths->SettingsPath.string(), error, imageWarnings);
        if (!resolved)
            return std::nullopt;
        prepared.Settings = *resolved;
        for (const auto& warning : imageWarnings)
        {
            if (std::find(collectedWarnings.begin(), collectedWarnings.end(), warning) ==
                collectedWarnings.end())
                collectedWarnings.push_back(warning);
        }
    }
    prepared.bWriteSettings = !bSettingsExist || !request.Overrides.IsEmpty();
    if (prepared.Descriptor)
        prepared.Descriptor->Settings = prepared.Settings.Authored;
    warnings = std::move(collectedWarnings);
    return prepared;
}

bool TextureCooker::SaveSettings(const PreparedTextureCook& prepared, std::string& error) const
{
    error.clear();
    if (!prepared.bWriteSettings)
        return true;
    const size_t expectedInputs = prepared.Descriptor ? 6 : 1;
    if (prepared.Inputs.size() != expectedInputs || prepared.ImageMetadata.size() != expectedInputs)
    {
        error = "Settings require a fully validated image request";
        return false;
    }
    const auto paths =
        MapTextureCookPaths(m_SourceRoot, m_CookedRoot, prepared.Paths.SourcePath, error);
    if (!paths || paths->SettingsPath != prepared.Paths.SettingsPath)
    {
        if (error.empty())
            error = "Prepared settings do not belong to this cooker";
        return false;
    }
    if (paths->Kind == TextureCookSourceKind::Dds ||
        (paths->Kind == TextureCookSourceKind::CubemapDescriptor) !=
            prepared.Descriptor.has_value())
    {
        error = "Settings require matching, fully validated source metadata";
        return false;
    }
    for (const auto& image : prepared.ImageMetadata)
    {
        TextureSettingsSource source;
        source.Channels = image.Channels;
        source.bNeedsMipGeneration = image.Extent.Width > 1 || image.Extent.Height > 1;
        std::vector<std::string> ignoredWarnings;
        if (!ResolveTextureSettings(prepared.Settings.Authored, {}, source,
                                    paths->SettingsPath.string(), error, ignoredWarnings))
            return false;
    }
    std::error_code ec;
    const bool bExists = std::filesystem::exists(paths->SettingsPath, ec);
    if (ec || bExists != prepared.OriginalSettings.has_value())
    {
        error = std::format("{}: Settings changed since preparation; retry cooking",
                            paths->SettingsPath.string());
        return false;
    }
    if (bExists)
    {
        const auto current = ReadCookText(paths->SettingsPath, error);
        if (!current)
            return false;
        if (*current != *prepared.OriginalSettings)
        {
            error = std::format("{}: Settings changed since preparation; retry cooking",
                                paths->SettingsPath.string());
            return false;
        }
    }
    auto descriptor = prepared.Descriptor;
    if (descriptor)
        descriptor->Settings = prepared.Settings.Authored;
    const auto json =
        descriptor ? SerializeCubemapDescriptor(*descriptor, paths->SettingsPath.string(), error)
                   : SerializeTextureSettings(prepared.Settings.Authored,
                                              paths->SettingsPath.string(), error);
    return json && WriteCookText(paths->SettingsPath, *json, error);
}

TextureCookResult TextureCooker::Cook(const TextureCookRequest& request) const
{
    TextureCookResult result;
    const auto prepared = Prepare(request, result.Error, result.Warnings);
    if (!prepared)
        return result;
    result.Output = prepared->Paths.CookedPath;
    if (!CheckCookOutputDirectory(result.Output.parent_path(), result.Error) ||
        !SaveSettings(*prepared, result.Error))
        return result;
    // Only encoding/publication may report Cooked or Skipped. Preparing settings
    // does not establish that a usable KTX2 exists or that an old asset is fresh.
    result.Error = "Texture encoding is not available yet; no cooked output was produced";
    return result;
}

} // namespace Hikari::Asset
