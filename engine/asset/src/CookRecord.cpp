#include "CookRecord.h"

#include <algorithm>
#include <cstdio>
#include <format>
#include <memory>

#include <ktx.h>
#include <nlohmann/json.hpp>

#include "CookFileUtil.h"
#include "CookHash.h"
#include "KtxFormat.h"
#include "SettingsJson.h"

namespace Hikari::Asset
{

TextureCookVersions DefaultTextureCookVersions()
{
    return {"tc8-1", HIKARI_COOK_COMPRESSONATOR_VERSION, HIKARI_COOK_KTX_VERSION,
            HIKARI_COOK_STB_IDENTITY};
}

namespace
{
using Json = nlohmann::json;

Json VersionJson(const TextureCookVersions& versions)
{
    return {{"pipeline", versions.Pipeline},
            {"compressonator", versions.Compressonator},
            {"ktx", versions.Ktx},
            {"imageDecoder", versions.ImageDecoder}};
}

bool IsDigest(std::string_view value)
{
    return value.size() == 64 &&
           std::all_of(value.begin(), value.end(), [](char ch)
                       { return (ch >= '0' && ch <= '9') || (ch >= 'a' && ch <= 'f'); });
}

bool MatchesFormat(uint32_t format, const ResolvedTextureSettings& settings)
{
    const auto expected = NativeKtxFormat(settings.Format, settings.ColorSpace);
    if (!expected)
        return false;
    const auto native = static_cast<KtxFormat>(format);
    if (native == *expected)
        return true;
    const bool srgb = settings.ColorSpace == TextureColorSpace::Srgb;
    return settings.Format == TextureCompression::BC1 &&
           settings.AlphaMode == TextureAlphaMode::Opaque &&
           native == (srgb ? KtxFormat::BC1RgbSrgb : KtxFormat::BC1RgbUnorm);
}
} // namespace

std::optional<std::string> FingerprintTextureCook(const PreparedTextureCook& prepared,
                                                  const TextureCookVersions& versions,
                                                  std::string& error)
{
    if (versions.Pipeline.empty() || versions.Compressonator.empty() || versions.Ktx.empty() ||
        versions.ImageDecoder.empty())
    {
        error = "Cooking requires nonempty pipeline and dependency identities";
        return std::nullopt;
    }
    const auto& settings = prepared.Settings;
    Json document{
        {"versions", VersionJson(versions)},
        {"kind", prepared.Dds          ? (prepared.Dds->Faces == 6 ? "dds-cubemap" : "dds-2d")
                 : prepared.Descriptor ? "cubemap"
                                       : "2d"},
        {"settings",
         {{"version", settings.Authored.Version},
          {"usage", ToString(settings.Authored.Usage)},
          {"format", ToString(settings.Format)},
          {"colorSpace", ToString(settings.ColorSpace)},
          {"mipmaps", settings.Authored.bMipmaps},
          {"flipNormalY", settings.Authored.bFlipNormalY},
          {"alphaMode", ToString(settings.AlphaMode)},
          {"alphaCutoff", settings.Authored.AlphaCutoff == 0 ? 0.0 : settings.Authored.AlphaCutoff},
          {"alphaUsageKnown", settings.bAlphaUsageKnown},
          {"recompressExistingMips", settings.bRecompressExistingMips}}},
        {"inputs", Json::array()}};
    for (size_t i = 0; i < prepared.Inputs.size(); ++i)
    {
        const auto digest = HashCookFile(prepared.Inputs[i], error);
        if (!digest)
            return std::nullopt;
        document["inputs"].push_back(
            {{"face", prepared.Descriptor ? kCubemapFaceNames[i] : "image"},
             {"path",
              prepared.Inputs[i].lexically_relative(prepared.Paths.SourceRoot).generic_string()},
             {"hash", *digest}});
    }
    return HashCookText(document.dump());
}

std::string SerializeCookRecord(const CookRecord& record)
{
    return Json{{"version", 1},
                {"fingerprint", record.Fingerprint},
                {"outputHash", record.OutputHash},
                {"versions", VersionJson(record.Versions)}}
               .dump(2) +
           "\n";
}

std::optional<CookRecord> ReadCookRecord(const std::filesystem::path& path)
{
    std::string ignored;
    const auto text = ReadCookText(path, ignored);
    if (!text)
        return std::nullopt;
    const auto json = ParseSettingsJson(*text, path.string(), ignored);
    if (!json)
        return std::nullopt;
    try
    {
        if (json->size() != 4 || !json->at("version").is_number_integer() ||
            json->at("version") != 1)
            return std::nullopt;
        const auto& versions = json->at("versions");
        if (!versions.is_object() || versions.size() != 4)
            return std::nullopt;
        CookRecord record{json->at("fingerprint").get<std::string>(),
                          json->at("outputHash").get<std::string>(),
                          {versions.at("pipeline").get<std::string>(),
                           versions.at("compressonator").get<std::string>(),
                           versions.at("ktx").get<std::string>(),
                           versions.at("imageDecoder").get<std::string>()}};
        if (!IsDigest(record.Fingerprint) || !IsDigest(record.OutputHash) ||
            record.Versions.Pipeline.empty() || record.Versions.Compressonator.empty() ||
            record.Versions.Ktx.empty() || record.Versions.ImageDecoder.empty())
            return std::nullopt;
        return record;
    }
    catch (const Json::exception&)
    {
        return std::nullopt;
    }
}

bool IsCookCurrent(const std::filesystem::path& output, const std::filesystem::path& recordPath,
                   std::string_view fingerprint, const TextureCookVersions& versions)
{
    const auto record = ReadCookRecord(recordPath);
    if (!record || record->Fingerprint != fingerprint || record->Versions != versions)
        return false;
    std::string ignored;
    const auto hash = HashCookFile(output, ignored);
    return hash && *hash == record->OutputHash;
}

bool ValidateCookOutput(const PreparedTextureCook& prepared, const std::filesystem::path& output,
                        std::string& error)
{
    // Own the stream: libktx 4.4.2 leaves named files open when header reads fail.
    std::FILE* rawFile = nullptr;
#ifdef _WIN32
    _wfopen_s(&rawFile, output.c_str(), L"rb");
#else
    rawFile = std::fopen(output.c_str(), "rb");
#endif
    const auto closeFile = [](std::FILE* value) { std::fclose(value); };
    const std::unique_ptr<std::FILE, decltype(closeFile)> file(rawFile, closeFile);
    ktxTexture2* texture = nullptr;
    const auto status = file ? ktxTexture2_CreateFromStdioStream(
                                   file.get(), KTX_TEXTURE_CREATE_LOAD_IMAGE_DATA_BIT, &texture)
                             : KTX_FILE_OPEN_FAILED;
    if (status != KTX_SUCCESS)
    {
        error =
            std::format("{}: Invalid temporary KTX2: {}", output.string(), ktxErrorString(status));
        return false;
    }
    const auto destroy = [](ktxTexture2* value) { ktxTexture_Destroy(ktxTexture(value)); };
    const std::unique_ptr<ktxTexture2, decltype(destroy)> owner(texture, destroy);
    const auto extent = prepared.ImageMetadata.front().Extent;
    uint32_t levels = 1;
    if (prepared.Settings.Authored.bMipmaps)
        for (auto dimension = std::max(extent.Width, extent.Height); dimension > 1; dimension >>= 1)
            ++levels;
    if (!MatchesFormat(texture->vkFormat, prepared.Settings) || texture->numDimensions != 2 ||
        texture->baseWidth != extent.Width || texture->baseHeight != extent.Height ||
        texture->baseDepth != 1 || texture->numLevels != levels || texture->numLayers != 1 ||
        texture->isArray ||
        texture->numFaces != (prepared.Dds          ? prepared.Dds->Faces
                              : prepared.Descriptor ? 6u
                                                    : 1u) ||
        texture->supercompressionScheme != KTX_SS_NONE || !texture->pData)
    {
        error = std::format("{}: Temporary KTX2 format, shape or mip chain does not match the "
                            "validated request",
                            output.string());
        return false;
    }
    const uint64_t blockBytes = prepared.Settings.Format == TextureCompression::BC1 ||
                                        prepared.Settings.Format == TextureCompression::BC4 ||
                                        prepared.Settings.Format == TextureCompression::BC4Signed
                                    ? 8
                                    : 16;
    for (uint32_t level = 0; level < levels; ++level)
    {
        const uint64_t width = std::max(extent.Width >> level, 1u);
        const uint64_t height = std::max(extent.Height >> level, 1u);
        const uint64_t size = ((width + 3) / 4) * ((height + 3) / 4) * blockBytes;
        if (ktxTexture_GetImageSize(ktxTexture(texture), level) != size)
        {
            error = std::format("{}: Invalid temporary KTX2 block footprint", output.string());
            return false;
        }
        for (uint32_t face = 0; face < texture->numFaces; ++face)
        {
            ktx_size_t offset = 0;
            if (ktxTexture_GetImageOffset(ktxTexture(texture), level, 0, face, &offset) !=
                    KTX_SUCCESS ||
                offset > texture->dataSize || size > texture->dataSize - offset)
            {
                error = std::format("{}: Temporary KTX2 payload is truncated", output.string());
                return false;
            }
        }
    }
    return true;
}

} // namespace Hikari::Asset
