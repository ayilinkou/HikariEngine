#include <asset/TextureCooker.h>

#include <algorithm>
#include <cctype>
#include <cmath>
#include <exception>
#include <format>
#include <system_error>
#include <utility>

#include <asset/DdsReader.h>
#include <asset/ImageReader.h>

#include "CookFileUtil.h"
#include "CookHash.h"
#include "CookLock.h"
#include "CookPublication.h"
#include "CookRecord.h"
#include "DdsKtxWriter.h"
#include "PixelTextureEncoder.h"

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
    if (request.EncoderThreads > 128)
    {
        error = "Encoder threads must be in [0, 128] (0 selects automatic)";
        return std::nullopt;
    }
    if (!std::isfinite(request.EncodingQuality) || request.EncodingQuality < 0 ||
        request.EncodingQuality > 1)
    {
        error = "Encoding quality must be a finite number in [0, 1]";
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
        prepared.Dds = ReadDdsInfo(paths->SourcePath, error);
        if (!prepared.Dds)
            return std::nullopt;
        const auto resolved = ResolveTextureSettings(
            saved, request.Overrides, DdsSettingsSource(*prepared.Dds, !bSettingsExist),
            paths->SettingsPath.string(), error, warnings);
        if (!resolved)
            return std::nullopt;
        prepared.Settings = *resolved;
        prepared.Inputs.push_back(paths->SourcePath);
        if (!prepared.Dds->IsCompressed() &&
            (prepared.Dds->Extent.Width > 16384 || prepared.Dds->Extent.Height > 16384))
        {
            error = std::format("{}: Pixel cooking supports desktop 2D dimensions up to 16384; no "
                                "resizing is performed",
                                paths->SourcePath.string());
            return std::nullopt;
        }
        prepared.ImageMetadata.push_back(
            {prepared.Dds->Extent, DdsPixelChannels(prepared.Dds->PixelLayout)});
        prepared.bWriteSettings = !bSettingsExist || !request.Overrides.IsEmpty();
        return prepared;
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
        if (metadata->Extent.Width > 16384 || metadata->Extent.Height > 16384)
        {
            error = std::format("{}: Pixel cooking supports desktop 2D dimensions up to 16384; no "
                                "resizing is performed",
                                prepared.Inputs[i].string());
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
    if ((paths->Kind == TextureCookSourceKind::Dds) != prepared.Dds.has_value() ||
        (paths->Kind == TextureCookSourceKind::CubemapDescriptor) !=
            prepared.Descriptor.has_value())
    {
        error = "Settings require matching, fully validated source metadata";
        return false;
    }
    if (prepared.Dds)
    {
        const auto current = ReadDdsInfo(paths->SourcePath, error);
        if (!current)
            return false;
        if (*current != *prepared.Dds || prepared.ImageMetadata.front().Extent != current->Extent)
        {
            error = std::format("{}: DDS metadata changed since preparation; retry cooking",
                                paths->SourcePath.string());
            return false;
        }
    }
    for (const auto& image : prepared.ImageMetadata)
    {
        TextureSettingsSource source =
            prepared.Dds ? DdsSettingsSource(*prepared.Dds) : TextureSettingsSource{};
        if (!prepared.Dds)
        {
            source.Channels = image.Channels;
            source.bNeedsMipGeneration = image.Extent.Width > 1 || image.Extent.Height > 1;
        }
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
    TextureCookEncoder encoder;
    encoder.Encode = [](const PreparedTextureCook& prepared, const std::filesystem::path& output,
                        std::string& error)
    {
        if (!prepared.Dds || !prepared.Dds->IsCompressed() ||
            prepared.Settings.bRecompressExistingMips ||
            (prepared.Settings.Authored.bMipmaps &&
             prepared.Dds->Levels < FullTextureMipCount(prepared.Dds->Extent)))
            return EncodeTexturePixels(prepared, output, error);
        return RepackageDds(prepared.Paths.SourcePath, *prepared.Dds, prepared.Settings, output,
                            error);
    };
    return Cook(request, encoder);
}

TextureCookResult TextureCooker::Cook(const TextureCookRequest& request,
                                      const TextureCookEncoder& encoder) const
{
    TextureCookResult result;
    auto prepared = Prepare(request, result.Error, result.Warnings);
    if (!prepared)
        return result;
    result.Output = prepared->Paths.CookedPath;
    if (!CheckCookOutputDirectory(result.Output.parent_path(), result.Error))
        return result;
    const auto lock = CookLock::Acquire(prepared->Paths, result.Error);
    if (!lock || !CookPublication::Recover(prepared->Paths, result.Error))
        return result;
    // A waiter must reread settings after acquiring the lock: another request
    // may have just persisted overrides and published this very output.
    prepared = Prepare(request, result.Error, result.Warnings);
    if (!prepared || !SaveSettings(*prepared, result.Error))
        return result;
    const auto warnings = result.Warnings;
    auto persistedRequest = request;
    persistedRequest.Overrides = {};
    prepared = Prepare(persistedRequest, result.Error, result.Warnings);
    result.Warnings = warnings;
    if (!prepared)
        return result;
    const auto fingerprint = FingerprintTextureCook(*prepared, encoder.Versions, result.Error);
    if (!fingerprint)
        return result;
    if (!request.bForce && IsCookCurrent(result.Output, prepared->Paths.RecordPath, *fingerprint,
                                         encoder.Versions, request.EncodingQuality))
    {
        result.Status = TextureCookStatus::Skipped;
        return result;
    }
    if (!encoder.Encode)
    {
        result.Error = "Texture encoding is not available yet; no cooked output was produced";
        return result;
    }
    const auto publication = CookPublication::Begin(prepared->Paths, result.Error);
    if (!publication)
        return result;
    try
    {
        if (!encoder.Encode(*prepared, publication->OutputPath(), result.Error))
        {
            if (result.Error.empty())
                result.Error = "Texture encoder failed; previous cooked output was retained";
            return result;
        }
    }
    catch (const std::exception& exception)
    {
        result.Error = std::format("Texture encoder failed: {}", exception.what());
        return result;
    }
    if (!ValidateCookOutput(*prepared, publication->OutputPath(), result.Error))
        return result;
    // Never publish under a fingerprint computed before intervening edits.
    std::vector<std::string> ignoredWarnings;
    const auto latest = Prepare(persistedRequest, result.Error, ignoredWarnings);
    if (!latest)
        return result;
    const auto latestFingerprint = FingerprintTextureCook(*latest, encoder.Versions, result.Error);
    if (!latestFingerprint)
        return result;
    if (*latestFingerprint != *fingerprint)
    {
        result.Error = std::format("{}: Inputs or settings changed during cooking; retry cooking",
                                   prepared->Paths.SourcePath.string());
        return result;
    }
    const auto outputHash = HashCookFile(publication->OutputPath(), result.Error);
    if (!outputHash ||
        !publication->Stage({*fingerprint, *outputHash, encoder.Versions, request.EncodingQuality},
                            result.Error))
        return result;
    if (!publication->PublishOutput(result.Error) || !publication->PublishRecord(result.Error))
    {
        std::string recoveryError;
        if (!CookPublication::Recover(prepared->Paths, recoveryError))
            result.Error += " Recovery pending: " + recoveryError;
        return result;
    }
    // The record is the commit marker. Cleanup failure can be retried on the
    // next request without misreporting an intact committed asset as failed.
    std::string cleanupError;
    if (!CookPublication::Recover(prepared->Paths, cleanupError))
        result.Warnings.push_back(cleanupError);
    result.Status = TextureCookStatus::Cooked;
    return result;
}

} // namespace Hikari::Asset
