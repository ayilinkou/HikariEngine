#include <asset/ImageReader.h>

#include <format>
#include <memory>

#include <core/Log.h>

// The single translation unit that compiles stb_image. Its functions are extern
// and may therefore be defined exactly once in a program, so the decoding
// callers elsewhere include this header for its declarations only and reach the
// definitions through this module. Keeping the definition here is what makes
// ImageWriter.h's claim — that this module owns the image library — true.
#define STB_IMAGE_IMPLEMENTATION
#include "stb_image.h"

namespace Hikari::Asset
{

namespace
{
constexpr Core::LogCategory LogImageReader("Image Reader");

// What a capture is: four 8-bit channels, no padding between rows.
constexpr int kBytesPerPixel = 4;
} // namespace

std::optional<Image> ReadPng(const std::string& path)
{
    int width = 0;
    int height = 0;
    int channelsInFile = 0;
    stbi_uc* pixels = stbi_load(path.c_str(), &width, &height, &channelsInFile, STBI_rgb_alpha);
    if (pixels == nullptr)
    {
        // stbi_failure_reason() is documented as valid only after a failure, and
        // is null if stb was built without failure strings.
        const char* reason = stbi_failure_reason();
        Core::LogMsg(Core::LogSeverity::Error, LogImageReader, "Failed to read {}: {}", path,
                     reason != nullptr ? reason : "no reason given");
        return std::nullopt;
    }

    const size_t byteCount =
        static_cast<size_t>(width) * static_cast<size_t>(height) * kBytesPerPixel;

    Image image{.Pixels = std::vector<uint8_t>(pixels, pixels + byteCount),
                .Extent = {static_cast<uint32_t>(width), static_cast<uint32_t>(height)},
                .SourceChannels = static_cast<uint32_t>(channelsInFile)};

    stbi_image_free(pixels);
    return image;
}

std::optional<ImageInfo> ReadImageInfo(const std::string& path, std::string& error)
{
    error.clear();
    if (stbi_is_16_bit(path.c_str()) || stbi_is_hdr(path.c_str()))
    {
        error = std::format(
            "{}: Cooking supports 8-bit images; higher-precision/HDR inputs are unsupported", path);
        return std::nullopt;
    }
    int width = 0, height = 0, channels = 0;
    if (!stbi_info(path.c_str(), &width, &height, &channels) || width <= 0 || height <= 0 ||
        channels < 1 || channels > 4)
    {
        error = std::format("{}: Failed to read supported 8-bit image metadata", path);
        return std::nullopt;
    }
    return ImageInfo{{static_cast<uint32_t>(width), static_cast<uint32_t>(height)},
                     static_cast<uint32_t>(channels)};
}

std::optional<Image> ReadTextureImage(const std::string& path, std::string& error)
{
    const auto info = ReadImageInfo(path, error);
    if (!info)
        return std::nullopt;
    int width = 0, height = 0, channels = 0;
    auto* pixels = stbi_load(path.c_str(), &width, &height, &channels, STBI_rgb_alpha);
    const auto freePixels = [](stbi_uc* value) { stbi_image_free(value); };
    const std::unique_ptr<stbi_uc, decltype(freePixels)> owner(pixels, freePixels);
    if (!pixels)
    {
        error = std::format("{}: Cannot decode texture pixels: {}", path,
                            stbi_failure_reason() ? stbi_failure_reason() : "unknown image error");
        return std::nullopt;
    }
    const size_t size = static_cast<size_t>(width) * height * 4;
    return Image{std::vector<uint8_t>(pixels, pixels + size),
                 {static_cast<uint32_t>(width), static_cast<uint32_t>(height)},
                 static_cast<uint32_t>(channels)};
}

} // namespace Hikari::Asset
