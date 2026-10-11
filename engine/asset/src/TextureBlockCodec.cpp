#include "TextureBlockCodec.h"

#include <algorithm>
#include <array>
#include <bit>
#include <cmath>
#include <cstring>
#include <format>
#include <fstream>
#include <limits>

#include <asset/DdsReader.h>
#include <asset/ImageWriter.h>

#include "CookProcess.h"

namespace Hikari::Asset
{
namespace
{
uint32_t U32(std::span<const uint8_t> bytes, size_t offset)
{
    return uint32_t(bytes[offset]) | (uint32_t(bytes[offset + 1]) << 8) |
           (uint32_t(bytes[offset + 2]) << 16) | (uint32_t(bytes[offset + 3]) << 24);
}

void SetU32(std::span<uint8_t> bytes, size_t offset, uint32_t value)
{
    for (size_t i = 0; i < 4; ++i)
        bytes[offset + i] = static_cast<uint8_t>(value >> (i * 8));
}

uint32_t DxgiFormat(TextureCompression format)
{
    switch (format)
    {
        case TextureCompression::BC1:
            return 71;
        case TextureCompression::BC2:
            return 74;
        case TextureCompression::BC3:
            return 77;
        case TextureCompression::BC4:
            return 80;
        case TextureCompression::BC4Signed:
            return 81;
        case TextureCompression::BC5:
            return 83;
        case TextureCompression::BC5Signed:
            return 84;
        case TextureCompression::BC7:
            return 98;
        case TextureCompression::Auto:
            return 0;
    }
    return 0;
}

uint64_t BlockSize(Core::Extent2D extent, TextureCompression format)
{
    const uint64_t bytes = format == TextureCompression::BC1 || format == TextureCompression::BC4 ||
                                   format == TextureCompression::BC4Signed
                               ? 8
                               : 16;
    return ((uint64_t(extent.Width) + 3) / 4) * ((uint64_t(extent.Height) + 3) / 4) * bytes;
}

bool CreateDirectory(const std::filesystem::path& path, std::string& error)
{
    std::error_code ec;
    std::filesystem::create_directories(path, ec);
    if (ec)
        error =
            std::format("{}: Cannot create block codec directory: {}", path.string(), ec.message());
    return !ec;
}

bool RemovePreviousOutput(const std::filesystem::path& path, std::string& error)
{
    std::error_code ec;
    std::filesystem::remove(path, ec);
    if (ec)
        error =
            std::format("{}: Cannot clear previous codec output: {}", path.string(), ec.message());
    return !ec;
}

std::optional<Image> ReadDecodedPixels(const std::filesystem::path& path, Core::Extent2D extent,
                                       TextureCompression format, std::string& error)
{
    const auto fail = [&](std::string_view reason) -> std::optional<Image>
    {
        error = std::format("{}: CPU decoder output: {}", path.string(), reason);
        return std::nullopt;
    };
    std::ifstream file(path, std::ios::binary | std::ios::ate);
    if (!file || file.tellg() < 0)
        return fail("Cannot read decoded DDS");
    const uint64_t pixelBytes = uint64_t(extent.Width) * extent.Height * 4;
    if (static_cast<uint64_t>(file.tellg()) != 128 + pixelBytes)
        return fail("Unexpected decoded pixel footprint");
    file.seekg(0);
    std::array<uint8_t, 128> header{};
    file.read(reinterpret_cast<char*>(header.data()), header.size());
    if (!file || U32(header, 0) != 0x20534444 || U32(header, 4) != 124 || U32(header, 76) != 32 ||
        U32(header, 88) != 32 || U32(header, 84) != 0 || U32(header, 16) != extent.Width ||
        U32(header, 12) != extent.Height || std::max(U32(header, 28), 1u) != 1 ||
        U32(header, 112) != 0)
        return fail("Expected one tightly packed 32-bit 2D DDS image without resizing");
    const bool bSigned =
        format == TextureCompression::BC4Signed || format == TextureCompression::BC5Signed;
    // Compressonator emits signed ARGB bytes with DDPF_BUMPDUDV. Do not
    // reinterpret those bytes as unsigned colours or lose SNORM zero/sign.
    if (((U32(header, 80) & 0x80000u) != 0) != bSigned)
        return fail("Decoder changed signed-channel interpretation");
    std::array<uint32_t, 4> masks{}, shifts{};
    uint32_t used = 0;
    for (size_t channel = 0; channel < 4; ++channel)
    {
        masks[channel] = U32(header, 92 + channel * 4);
        if (!masks[channel] || (used & masks[channel]))
            return fail("Invalid decoded channel masks");
        shifts[channel] = static_cast<uint32_t>(std::countr_zero(masks[channel]));
        if ((masks[channel] >> shifts[channel]) != 255)
            return fail("Expected 8-bit decoded channels");
        used |= masks[channel];
    }
    Image image{{},
                extent,
                format == TextureCompression::BC4 || format == TextureCompression::BC4Signed ? 1u
                : format == TextureCompression::BC5 || format == TextureCompression::BC5Signed
                    ? 2u
                    : 4u};
    image.Pixels.resize(static_cast<size_t>(pixelBytes));
    std::vector<uint8_t> row(size_t(extent.Width) * 4);
    for (uint32_t y = 0; y < extent.Height; ++y)
    {
        file.read(reinterpret_cast<char*>(row.data()), static_cast<std::streamsize>(row.size()));
        if (!file)
            return fail("Truncated decoded pixel rows");
        for (uint32_t x = 0; x < extent.Width; ++x)
        {
            const auto packed = U32(row, size_t(x) * 4);
            for (size_t channel = 0; channel < 4; ++channel)
            {
                int value = static_cast<int>((packed & masks[channel]) >> shifts[channel]);
                if (bSigned && channel < image.SourceChannels)
                {
                    if (value >= 128)
                        value -= 256;
                    value = static_cast<int>(
                        std::lround((std::max(value / 127.0, -1.0) * 0.5 + 0.5) * 255));
                }
                image.Pixels[(size_t(y) * extent.Width + x) * 4 + channel] =
                    static_cast<uint8_t>(value);
            }
            if (image.SourceChannels < 4)
                image.Pixels[(size_t(y) * extent.Width + x) * 4 + 3] = 255;
        }
    }
    return image;
}
} // namespace

std::optional<std::vector<uint8_t>> ReadTextureBlocks(const std::filesystem::path& path,
                                                      const DdsTextureInfo& info, uint32_t level,
                                                      uint32_t face, std::string& error)
{
    error.clear();
    const auto current = ReadDdsInfo(path, error);
    if (!current)
        return std::nullopt;
    if (*current != info || !info.IsCompressed())
    {
        error = std::format("{}: Native DDS metadata changed; retry cooking", path.string());
        return std::nullopt;
    }
    const auto sub =
        std::find_if(info.Subresources.begin(), info.Subresources.end(),
                     [&](const auto& entry) { return entry.Level == level && entry.Face == face; });
    if (sub == info.Subresources.end() ||
        sub->Size > static_cast<uint64_t>(std::numeric_limits<std::streamsize>::max()))
    {
        error = "Native DDS face/mip is out of range or too large";
        return std::nullopt;
    }
    std::vector<uint8_t> blocks(static_cast<size_t>(sub->Size));
    std::ifstream file(path, std::ios::binary);
    file.seekg(static_cast<std::streamoff>(sub->Offset));
    file.read(reinterpret_cast<char*>(blocks.data()), static_cast<std::streamsize>(blocks.size()));
    if (!file)
    {
        error =
            std::format("{}: Cannot read DDS face {} mip {} blocks", path.string(), face, level);
        return std::nullopt;
    }
    return blocks;
}

std::optional<Image> DecodeTextureBlocks(std::span<const uint8_t> blocks, Core::Extent2D extent,
                                         TextureCompression format,
                                         const std::filesystem::path& directory,
                                         const std::filesystem::path& executable,
                                         std::string& error)
{
    error.clear();
    if (!extent.Width || !extent.Height || extent.Width > 16384 || extent.Height > 16384 ||
        !DxgiFormat(format) || blocks.size() != BlockSize(extent, format))
    {
        error = "Native decoding requires bounded BC blocks and desktop dimensions up to 16384; no "
                "resizing is performed";
        return std::nullopt;
    }
    if (!CreateDirectory(directory, error))
        return std::nullopt;
    std::array<uint8_t, 148> header{};
    // The pinned host codec mishandles row selection in partial edge blocks.
    // Give it a whole-block view; logical dimensions and native block counts
    // remain those of the source, and only visible decoded texels are retained.
    const Core::Extent2D storage{((extent.Width + 3) / 4) * 4, ((extent.Height + 3) / 4) * 4};
    SetU32(header, 0, 0x20534444);
    SetU32(header, 4, 124);
    SetU32(header, 8, 0x81007);
    SetU32(header, 12, storage.Height);
    SetU32(header, 16, storage.Width);
    SetU32(header, 20, static_cast<uint32_t>(blocks.size()));
    SetU32(header, 28, 1);
    SetU32(header, 76, 32);
    SetU32(header, 80, 4);
    SetU32(header, 84, 0x30315844);
    SetU32(header, 108, 0x1000);
    SetU32(header, 128, DxgiFormat(format));
    SetU32(header, 132, 3);
    SetU32(header, 140, 1);
    const auto input = directory / "decode-input.dds", output = directory / "decode-output.dds";
    if (!RemovePreviousOutput(output, error))
        return std::nullopt;
    {
        // A single raw subresource avoids decoder mip/face inference and keeps
        // authored swizzles separate from the block decompression operation.
        std::ofstream file(input, std::ios::binary);
        file.write(reinterpret_cast<const char*>(header.data()), header.size());
        file.write(reinterpret_cast<const char*>(blocks.data()),
                   static_cast<std::streamsize>(blocks.size()));
        file.close();
        if (!file)
        {
            error = std::format("{}: Cannot stage native DDS blocks", input.string());
            return std::nullopt;
        }
    }
    if (!RunCookProcess(
            executable,
            {"-fd", "ARGB_8888", "-nomipmap", CookPathUtf8(input), CookPathUtf8(output)},
            directory / "decoder.log", error))
        return std::nullopt;
    auto image = ReadDecodedPixels(output, storage, format, error);
    if (image && storage != extent)
    {
        for (uint32_t y = 0; y < extent.Height; ++y)
            std::memmove(image->Pixels.data() + size_t(y) * extent.Width * 4,
                         image->Pixels.data() + size_t(y) * storage.Width * 4,
                         size_t(extent.Width) * 4);
        image->Pixels.resize(size_t(extent.Width) * extent.Height * 4);
        image->Extent = extent;
    }
    return image;
}

std::optional<std::vector<uint8_t>>
EncodeTextureBlocks(const Image& image, TextureCompression format, uint32_t threads,
                    double alphaCutoff, const std::filesystem::path& directory,
                    const std::filesystem::path& executable, std::string& error)
{
    error.clear();
    if (!DxgiFormat(format) || !image.Extent.Width || !image.Extent.Height ||
        image.Extent.Width > 16384 || image.Extent.Height > 16384 ||
        uint64_t(image.Extent.Width) * image.Extent.Height * 4 != image.Pixels.size() ||
        !std::isfinite(alphaCutoff) || alphaCutoff < 0 || alphaCutoff > 1)
    {
        error = "Native encoding requires complete bounded RGBA pixels, a BC format and valid "
                "alpha cutoff";
        return std::nullopt;
    }
    if (!CreateDirectory(directory, error))
        return std::nullopt;
    const auto input = directory / "encode-input.png", output = directory / "encode-output.dds";
    if (!RemovePreviousOutput(output, error))
        return std::nullopt;
    const Core::Extent2D storage{((image.Extent.Width + 3) / 4) * 4,
                                 ((image.Extent.Height + 3) / 4) * 4};
    std::optional<Image> padded;
    if (storage != image.Extent)
    {
        padded = Image{{}, storage, image.SourceChannels};
        padded->Pixels.resize(size_t(storage.Width) * storage.Height * 4);
        for (uint32_t y = 0; y < storage.Height; ++y)
            for (uint32_t x = 0; x < storage.Width; ++x)
            {
                const auto source =
                    (size_t(std::min(y, image.Extent.Height - 1)) * image.Extent.Width +
                     std::min(x, image.Extent.Width - 1)) *
                    4;
                std::copy_n(image.Pixels.data() + source, 4,
                            padded->Pixels.data() + (size_t(y) * storage.Width + x) * 4);
            }
    }
    const auto& pixels = padded ? *padded : image;
    if (!WritePng(pixels.Pixels, storage, input.string()))
    {
        error = std::format("{}: Cannot write prepared mip pixels", input.string());
        return std::nullopt;
    }
    std::vector<std::string> arguments{"-fd",         std::string(ToDisplayString(format)),
                                       "-EncodeWith", "HPC",
                                       "-NumThreads", std::to_string(threads),
                                       "-nomipmap"};
    if (format == TextureCompression::BC7)
    {
        // The fast default can collapse a small two-dimensional gradient to
        // one colour line. Offline cooking needs the full BC7 mode search.
        arguments.push_back("-Quality");
        arguments.push_back("1.0");
    }
    if (format == TextureCompression::BC1)
    {
        arguments.push_back("-AlphaThreshold");
        arguments.push_back(std::to_string(std::lround(alphaCutoff * 255)));
    }
    arguments.push_back(CookPathUtf8(input));
    arguments.push_back(CookPathUtf8(output));
    if (!RunCookProcess(executable, arguments, directory / "encoder.log", error))
        return std::nullopt;
    const auto encoded = ReadDdsInfo(output, error);
    if (!encoded)
        return std::nullopt;
    if (!encoded->IsCompressed() || encoded->Extent != storage || encoded->Format != format ||
        encoded->Faces != 1 || encoded->Levels != 1)
    {
        error = "CPU encoder changed the requested format, dimensions or mip count; no resizing is "
                "permitted";
        return std::nullopt;
    }
    return ReadTextureBlocks(output, *encoded, 0, 0, error);
}
} // namespace Hikari::Asset
