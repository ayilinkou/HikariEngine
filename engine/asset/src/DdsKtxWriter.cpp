#include "DdsKtxWriter.h"

#include <format>
#include <fstream>
#include <limits>
#include <memory>
#include <vector>

#include <ktx.h>

#include <asset/DdsReader.h>

#include "CookKtxMetadata.h"
#include "KtxFormat.h"

namespace Hikari::Asset
{

bool RepackageDds(const std::filesystem::path& source, const DdsTextureInfo& info,
                  const ResolvedTextureSettings& settings, const std::filesystem::path& output,
                  std::string& error)
{
    error.clear();
    const auto current = ReadDdsInfo(source, error);
    if (!current)
        return false;
    if (*current != info)
    {
        error = std::format("{}: DDS metadata changed since preparation; retry cooking",
                            source.string());
        return false;
    }
    const auto format = NativeKtxFormat(settings.Format, settings.ColorSpace);
    if (!format || settings.Format != info.Format || settings.bRecompressExistingMips ||
        settings.Authored.bFlipNormalY)
    {
        error = std::format("{}: Lossless DDS repackaging requires the native format and unchanged "
                            "pixels; DDS conversion is not available yet (TC8)",
                            source.string());
        return false;
    }
    ktxTextureCreateInfo create{};
    create.vkFormat = static_cast<uint32_t>(*format);
    create.baseWidth = info.Extent.Width;
    create.baseHeight = info.Extent.Height;
    create.baseDepth = 1;
    create.numDimensions = 2;
    create.numLevels = settings.Authored.bMipmaps ? info.Levels : 1;
    create.numLayers = 1;
    create.numFaces = info.Faces;
    const auto check = [&](KTX_error_code status)
    {
        if (status != KTX_SUCCESS)
            error = std::format("{}: Cannot repackage DDS as KTX2: {}", source.string(),
                                ktxErrorString(status));
        return status == KTX_SUCCESS;
    };
    ktxTexture2* texture = nullptr;
    if (!check(ktxTexture2_Create(&create, KTX_TEXTURE_CREATE_ALLOC_STORAGE, &texture)))
        return false;
    const auto destroy = [](ktxTexture2* value) { ktxTexture_Destroy(ktxTexture(value)); };
    const std::unique_ptr<ktxTexture2, decltype(destroy)> owner(texture, destroy);
    std::ifstream file(source, std::ios::binary);
    for (const auto& subresource : info.Subresources)
    {
        if (subresource.Level >= create.numLevels)
            continue;
        if (subresource.Size != ktxTexture_GetImageSize(ktxTexture(texture), subresource.Level) ||
            subresource.Size > static_cast<uint64_t>(std::numeric_limits<std::streamsize>::max()))
        {
            error = std::format("{}: DDS/KTX2 subresource footprint mismatch", source.string());
            return false;
        }
        std::vector<uint8_t> blocks(static_cast<size_t>(subresource.Size));
        file.seekg(static_cast<std::streamoff>(subresource.Offset));
        file.read(reinterpret_cast<char*>(blocks.data()),
                  static_cast<std::streamsize>(blocks.size()));
        if (!file || static_cast<uint64_t>(file.gcount()) != subresource.Size)
        {
            error = std::format("{}: Cannot read DDS face {} mip {} payload", source.string(),
                                subresource.Face, subresource.Level);
            return false;
        }
        if (!check(ktxTexture_SetImageFromMemory(ktxTexture(texture), subresource.Level, 0,
                                                 subresource.Face, blocks.data(), blocks.size())))
            return false;
    }
    if (!SetCookKtxMetadata(texture, settings, info.AlphaMode, error))
        return false;
    return check(ktxTexture2_WriteToNamedFile(texture, output.string().c_str()));
}

} // namespace Hikari::Asset
