#include "PixelTextureEncoder.h"

#include <algorithm>
#include <format>
#include <fstream>
#include <memory>
#include <system_error>
#include <vector>

#include <ktx.h>

#include <asset/DdsReader.h>
#include <asset/ImageWriter.h>

#include "CookKtxMetadata.h"
#include "CookProcess.h"
#include "KtxFormat.h"
#include "TexturePixelFilter.h"

namespace Hikari::Asset
{

std::filesystem::path ConfiguredTextureEncoder()
{
    return std::filesystem::path(u8"" HIKARI_COOK_ENCODER_PATH);
}

bool EncodeTexturePixels(const PreparedTextureCook& prepared, const std::filesystem::path& output,
                         std::string& error, const std::filesystem::path& executable)
{
    error.clear();
    const auto& settings = prepared.Settings;
    if (settings.Authored.Usage == TextureUsage::Normal)
    {
        error = "Texture encoding is not available yet for normal usage (TC7); no cooked output "
                "was replaced";
        return false;
    }
    if (settings.AlphaMode == TextureAlphaMode::Mask)
    {
        error = "Masked pixel encoding is not available yet (TC9); native DDS repackaging remains "
                "supported";
        return false;
    }
    const auto extent = prepared.ImageMetadata.front().Extent;
    const uint32_t faces = prepared.Dds ? prepared.Dds->Faces : prepared.Descriptor ? 6 : 1;
    const uint32_t levels = settings.Authored.bMipmaps ? FullTextureMipCount(extent) : 1;
    const uint32_t supplied = prepared.Dds ? prepared.Dds->Levels : 1;
    if (faces == 6 && levels > supplied)
    {
        error = "Generated cubemap mips require cross-face filtering (TC8); use --no-mips or "
                "supplied DDS levels";
        return false;
    }
    const auto format = NativeKtxFormat(settings.Format, settings.ColorSpace);
    if (!format ||
        (settings.Format != TextureCompression::BC1 && settings.Format != TextureCompression::BC4 &&
         settings.Format != TextureCompression::BC5 && settings.Format != TextureCompression::BC7))
    {
        error = "Pixel encoding supports BC1, BC4, BC5 and BC7";
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
    std::error_code ec;
    std::filesystem::create_directories(scratch, ec);
    if (ec)
    {
        error =
            std::format("{}: Cannot create encoding directory: {}", scratch.string(), ec.message());
        return false;
    }
    for (uint32_t face = 0; face < faces; ++face)
    {
        std::optional<Image> image;
        for (uint32_t level = 0; level < levels; ++level)
        {
            if (prepared.Dds && level < supplied)
                image = ReadDdsPixels(prepared.Paths.SourcePath, *prepared.Dds, level, face, error);
            else if (!level)
                image = ReadTextureImage(prepared.Inputs[face].string(), error);
            else
                image = FilterTextureMip(*image, settings.ColorSpace, error);
            if (!image)
                return false;
            const Core::Extent2D expected{std::max(extent.Width >> level, 1u),
                                          std::max(extent.Height >> level, 1u)};
            if (image->Extent != expected)
            {
                error = "Source dimensions changed during pixel cooking; retry cooking";
                return false;
            }
            // BC1 opaque storage must never accidentally select binary alpha.
            // BC4 maps canonical R, BC5 maps R/G, BC7 retains all RGBA channels.
            if (settings.Format == TextureCompression::BC1)
                for (size_t i = 3; i < image->Pixels.size(); i += 4)
                    image->Pixels[i] = 255;
            const auto stem = std::format("face-{}-mip-{}", face, level);
            const auto png = scratch / (stem + ".png"), dds = scratch / (stem + ".dds");
            if (!WritePng(image->Pixels, image->Extent, png.string()))
            {
                error = std::format("{}: Cannot write prepared mip pixels", png.string());
                return false;
            }
            if (!RunCookProcess(executable,
                                {"-fd", std::string(ToDisplayString(settings.Format)),
                                 "-EncodeWith", "HPC", "-NumThreads",
                                 std::to_string(prepared.Request.EncoderThreads), "-nomipmap",
                                 CookPathUtf8(png), CookPathUtf8(dds)},
                                scratch / "encoder.log", error))
                return false;
            const auto encoded = ReadDdsInfo(dds, error);
            if (!encoded)
                return false;
            if (!encoded->IsCompressed() || encoded->Extent != expected ||
                encoded->Format != settings.Format || encoded->Faces != 1 || encoded->Levels != 1)
            {
                error = "CPU encoder changed the requested format, dimensions or mip count; no "
                        "resizing is permitted";
                return false;
            }
            const auto& sub = encoded->Subresources.front();
            std::vector<uint8_t> blocks(static_cast<size_t>(sub.Size));
            std::ifstream file(dds, std::ios::binary);
            file.seekg(static_cast<std::streamoff>(sub.Offset));
            file.read(reinterpret_cast<char*>(blocks.data()),
                      static_cast<std::streamsize>(blocks.size()));
            if (!file || !check(ktxTexture_SetImageFromMemory(ktxTexture(texture), level, 0, face,
                                                              blocks.data(), blocks.size())))
            {
                if (error.empty())
                    error = "Cannot read encoded mip blocks";
                return false;
            }
        }
    }
    if (!SetCookKtxMetadata(texture, settings,
                            prepared.Dds ? prepared.Dds->AlphaMode : DdsAlphaMode::Unknown, error))
        return false;
    return check(ktxTexture2_WriteToNamedFile(texture, output.string().c_str()));
}

} // namespace Hikari::Asset
