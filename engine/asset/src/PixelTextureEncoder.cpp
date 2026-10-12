#include "PixelTextureEncoder.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <format>
#include <memory>

#include <ktx.h>

#include <asset/DdsReader.h>

#include "CookKtxMetadata.h"
#include "KtxFormat.h"
#include "TextureBlockCodec.h"
#include "TexturePixelFilter.h"

namespace Hikari::Asset
{
namespace
{
void DecodeNormalChannels(Image& image, const DdsTextureInfo& source)
{
    const bool bXY = source.Format == TextureCompression::BC5 ||
                     source.Format == TextureCompression::BC5Signed ||
                     source.ChannelSwizzle != DdsChannelSwizzle::None;
    for (size_t pixel = 0; pixel < image.Pixels.size(); pixel += 4)
    {
        if (source.ChannelSwizzle == DdsChannelSwizzle::SwapRedGreen)
            std::swap(image.Pixels[pixel], image.Pixels[pixel + 1]);
        else if (source.ChannelSwizzle == DdsChannelSwizzle::SwapRedAlpha)
            std::swap(image.Pixels[pixel], image.Pixels[pixel + 3]);
        if (bXY)
        {
            const double x = image.Pixels[pixel] / 127.5 - 1;
            const double y = image.Pixels[pixel + 1] / 127.5 - 1;
            const double z = std::sqrt(std::max(0.0, 1 - x * x - y * y));
            image.Pixels[pixel + 2] = static_cast<uint8_t>(std::lround((z * 0.5 + 0.5) * 255));
            image.Pixels[pixel + 3] = 255;
        }
    }
    image.SourceChannels = bXY ? 3u : 4u;
}

bool PrepareSourceNormal(Image& image, bool bFlip, std::string& error)
{
    for (size_t pixel = 0; pixel < image.Pixels.size(); pixel += 4)
    {
        if (image.Pixels[pixel + 2] < 127)
        {
            error = "Normal pixel inputs require positive-Z tangent-space XYZ; negative-Z normals "
                    "are unsupported";
            return false;
        }
        if (bFlip)
            image.Pixels[pixel + 1] = static_cast<uint8_t>(255 - image.Pixels[pixel + 1]);
    }
    return true;
}
} // namespace

std::filesystem::path ConfiguredTextureEncoder()
{
    return std::filesystem::path(u8"" HIKARI_COOK_ENCODER_PATH);
}

bool EncodeTexturePixels(const PreparedTextureCook& prepared, const std::filesystem::path& output,
                         std::string& error, const std::filesystem::path& executable)
{
    error.clear();
    const auto& settings = prepared.Settings;
    const bool bNormal = settings.Authored.Usage == TextureUsage::Normal;
    const bool bCompressed = prepared.Dds && prepared.Dds->IsCompressed();
    const bool bRetain = bCompressed && !settings.bRecompressExistingMips;
    const auto extent = prepared.ImageMetadata.front().Extent;
    const uint32_t faces = prepared.Dds ? prepared.Dds->Faces : prepared.Descriptor ? 6 : 1;
    const uint32_t levels = settings.Authored.bMipmaps ? FullTextureMipCount(extent) : 1;
    const uint32_t supplied = prepared.Dds ? prepared.Dds->Levels : 1;
    if (settings.AlphaMode == TextureAlphaMode::Mask && levels > supplied)
    {
        error = "Generated masked mips are not supported; use --no-mips or supplied DDS levels";
        return false;
    }
    const auto format = NativeKtxFormat(settings.Format, settings.ColorSpace);
    if (!format)
    {
        error = "Pixel encoding requires a supported native BC format and colour space";
        return false;
    }
    const auto check = [&](KTX_error_code status)
    {
        if (status != KTX_SUCCESS)
            error = std::format("Cannot encode KTX2: {}", ktxErrorString(status));
        return status == KTX_SUCCESS;
    };
    ktxTextureCreateInfo create{};
    create.vkFormat = static_cast<uint32_t>(*format);
    create.baseWidth = extent.Width;
    create.baseHeight = extent.Height;
    create.baseDepth = 1;
    create.numDimensions = 2;
    create.numLevels = levels;
    create.numLayers = 1;
    create.numFaces = faces;
    ktxTexture2* texture = nullptr;
    if (!check(ktxTexture2_Create(&create, KTX_TEXTURE_CREATE_ALLOC_STORAGE, &texture)))
        return false;
    const auto destroy = [](ktxTexture2* value) { ktxTexture_Destroy(ktxTexture(value)); };
    const std::unique_ptr<ktxTexture2, decltype(destroy)> owner(texture, destroy);
    const auto scratch = output.parent_path() / "encoding";
    std::array<Image, 6> images;
    for (uint32_t level = 0; level < levels; ++level)
    {
        const bool bSourceLevel = level < supplied;
        if (!bSourceLevel)
        {
            if (faces == 6)
            {
                auto filtered =
                    FilterCubemapMip(images, settings.ColorSpace, settings.Authored.Usage, error);
                if (!filtered)
                    return false;
                images = std::move(*filtered);
            }
            else
            {
                auto filtered = FilterTextureMip(images[0], settings.ColorSpace, error,
                                                 settings.Authored.Usage);
                if (!filtered)
                    return false;
                images[0] = std::move(*filtered);
            }
        }
        const Core::Extent2D expected{std::max(extent.Width >> level, 1u),
                                      std::max(extent.Height >> level, 1u)};
        for (uint32_t face = 0; face < faces; ++face)
        {
            const auto directory = scratch / std::format("face-{}-mip-{}", face, level);
            auto& image = images[face];
            if (bSourceLevel)
            {
                std::optional<Image> decoded;
                if (bCompressed)
                {
                    const auto blocks = ReadTextureBlocks(prepared.Paths.SourcePath, *prepared.Dds,
                                                          level, face, error);
                    if (!blocks)
                        return false;
                    if (bRetain)
                    {
                        if (!check(ktxTexture_SetImageFromMemory(ktxTexture(texture), level, 0,
                                                                 face, blocks->data(),
                                                                 blocks->size())))
                            return false;
                        // Only the last supplied level feeds missing mips. Every
                        // retained block bypasses encoding, including cubemap faces.
                        if (level + 1 != supplied || levels <= supplied)
                            continue;
                    }
                    decoded = DecodeTextureBlocks(*blocks, expected, prepared.Dds->Format,
                                                  directory, executable, error);
                    if (decoded && bNormal)
                        DecodeNormalChannels(*decoded, *prepared.Dds);
                }
                else if (prepared.Dds)
                    decoded =
                        ReadDdsPixels(prepared.Paths.SourcePath, *prepared.Dds, level, face, error);
                else
                    decoded = ReadTextureImage(prepared.Inputs[face].string(), error);
                if (!decoded)
                    return false;
                image = std::move(*decoded);
                if (bNormal &&
                    !PrepareSourceNormal(image, !bRetain && settings.Authored.bFlipNormalY, error))
                    return false;
            }
            if (image.Extent != expected)
            {
                error = "Source dimensions changed during pixel cooking; retry cooking";
                return false;
            }
            if (bRetain && bSourceLevel)
                continue;
            std::optional<Image> normalized;
            const auto swizzle = bRetain ? prepared.Dds->ChannelSwizzle : DdsChannelSwizzle::None;
            if (bNormal && (settings.Format == TextureCompression::BC5 ||
                            settings.Format == TextureCompression::BC5Signed ||
                            swizzle != DdsChannelSwizzle::None))
            {
                normalized = PrepareNormalTextureXY(image, error);
                if (!normalized)
                    return false;
            }
            // Packing must not alter the working vectors used by the next mip.
            Image packed = normalized ? std::move(*normalized) : image;
            if (bNormal && swizzle != DdsChannelSwizzle::None)
                for (size_t pixel = 0; pixel < packed.Pixels.size(); pixel += 4)
                {
                    if (swizzle == DdsChannelSwizzle::SwapRedGreen)
                        std::swap(packed.Pixels[pixel], packed.Pixels[pixel + 1]);
                    else
                    {
                        packed.Pixels[pixel + 3] = packed.Pixels[pixel];
                        packed.Pixels[pixel] = packed.Pixels[pixel + 2] = 0;
                    }
                }
            // Unknown DDS alpha needs ordinary filtering with its coverage-risk
            // warning. Explicit opaque BC1 and new opaque images discard alpha.
            if (settings.Format == TextureCompression::BC1 &&
                settings.AlphaMode == TextureAlphaMode::Opaque &&
                (!bCompressed || settings.bAlphaUsageKnown))
                for (size_t pixel = 3; pixel < packed.Pixels.size(); pixel += 4)
                    packed.Pixels[pixel] = 255;
            const auto blocks =
                EncodeTextureBlocks(packed, settings.Format, prepared.Request.EncoderThreads,
                                    prepared.Request.EncodingQuality, settings.Authored.AlphaCutoff,
                                    directory, executable, error);
            if (!blocks || !check(ktxTexture_SetImageFromMemory(ktxTexture(texture), level, 0, face,
                                                                blocks->data(), blocks->size())))
                return false;
        }
    }
    auto sourceMetadata = prepared.Dds;
    if (sourceMetadata && settings.bRecompressExistingMips)
    {
        // Conversion produces canonical output channels; the old native swizzle
        // must not describe newly encoded blocks or influence shader decoding.
        sourceMetadata->ChannelSwizzle = DdsChannelSwizzle::None;
        sourceMetadata->Format = settings.Format;
    }
    if (!SetCookKtxMetadata(texture, settings,
                            sourceMetadata ? sourceMetadata->AlphaMode : DdsAlphaMode::Unknown,
                            error, sourceMetadata ? &*sourceMetadata : nullptr))
        return false;
    return check(ktxTexture2_WriteToNamedFile(texture, output.string().c_str()));
}
} // namespace Hikari::Asset
