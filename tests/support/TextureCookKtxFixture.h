#pragma once

#include <algorithm>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <string>
#include <vector>

#include <ktx.h>
#include <vulkan/vulkan_core.h>

#include <asset/TextureCookEncoder.h>

// A real native-block KTX2 producer for testing the TC4 transaction service.
// These deliberately synthetic blocks test publication, not image encoding.
inline bool WriteTextureCookKtxFixture(const Hikari::Asset::PreparedTextureCook& prepared,
                                       const std::filesystem::path& output, std::string& error,
                                       uint8_t seed = 0)
{
    using Hikari::Asset::TextureColorSpace;
    using Hikari::Asset::TextureCompression;
    const bool srgb = prepared.Settings.ColorSpace == TextureColorSpace::Srgb;
    uint32_t format = VK_FORMAT_UNDEFINED;
    switch (prepared.Settings.Format)
    {
        case TextureCompression::BC1:
            format = srgb ? VK_FORMAT_BC1_RGBA_SRGB_BLOCK : VK_FORMAT_BC1_RGBA_UNORM_BLOCK;
            break;
        case TextureCompression::BC2:
            format = srgb ? VK_FORMAT_BC2_SRGB_BLOCK : VK_FORMAT_BC2_UNORM_BLOCK;
            break;
        case TextureCompression::BC3:
            format = srgb ? VK_FORMAT_BC3_SRGB_BLOCK : VK_FORMAT_BC3_UNORM_BLOCK;
            break;
        case TextureCompression::BC4:
            format = VK_FORMAT_BC4_UNORM_BLOCK;
            break;
        case TextureCompression::BC4Signed:
            format = VK_FORMAT_BC4_SNORM_BLOCK;
            break;
        case TextureCompression::BC5:
            format = VK_FORMAT_BC5_UNORM_BLOCK;
            break;
        case TextureCompression::BC5Signed:
            format = VK_FORMAT_BC5_SNORM_BLOCK;
            break;
        case TextureCompression::BC7:
            format = srgb ? VK_FORMAT_BC7_SRGB_BLOCK : VK_FORMAT_BC7_UNORM_BLOCK;
            break;
        case TextureCompression::Auto:
            break;
    }
    const auto extent = prepared.ImageMetadata.front().Extent;
    ktxTextureCreateInfo info{};
    info.vkFormat = format;
    info.baseWidth = extent.Width;
    info.baseHeight = extent.Height;
    info.baseDepth = 1;
    info.numDimensions = 2;
    info.numLevels = 1;
    if (prepared.Settings.Authored.bMipmaps)
        for (auto dimension = std::max(extent.Width, extent.Height); dimension > 1; dimension >>= 1)
            ++info.numLevels;
    info.numLayers = 1;
    info.numFaces = prepared.Descriptor ? 6 : 1;
    ktxTexture2* texture = nullptr;
    auto status = ktxTexture2_Create(&info, KTX_TEXTURE_CREATE_ALLOC_STORAGE, &texture);
    if (status != KTX_SUCCESS)
    {
        error = ktxErrorString(status);
        return false;
    }
    const auto destroy = [](ktxTexture2* value) { ktxTexture_Destroy(ktxTexture(value)); };
    const std::unique_ptr<ktxTexture2, decltype(destroy)> owner(texture, destroy);
    for (uint32_t level = 0; level < info.numLevels; ++level)
    {
        std::vector<uint8_t> blocks(ktxTexture_GetImageSize(ktxTexture(texture), level), seed);
        for (uint32_t face = 0; face < info.numFaces; ++face)
        {
            status = ktxTexture_SetImageFromMemory(ktxTexture(texture), level, 0, face,
                                                   blocks.data(), blocks.size());
            if (status != KTX_SUCCESS)
            {
                error = ktxErrorString(status);
                return false;
            }
        }
    }
    status = ktxTexture_WriteToNamedFile(ktxTexture(texture), output.string().c_str());
    if (status != KTX_SUCCESS)
        error = ktxErrorString(status);
    return status == KTX_SUCCESS;
}

inline Hikari::Asset::TextureCookEncoder TextureCookFixtureEncoder(uint8_t seed = 0)
{
    Hikari::Asset::TextureCookEncoder encoder;
    encoder.Encode = [seed](const auto& prepared, const auto& output, auto& error)
    { return WriteTextureCookKtxFixture(prepared, output, error, seed); };
    return encoder;
}
