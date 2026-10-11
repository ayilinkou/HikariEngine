#include <asset/DdsReader.h>

#include <algorithm>
#include <array>
#include <bit>
#include <format>
#include <fstream>
#include <limits>
#include <span>
#include <string_view>

namespace Hikari::Asset
{

namespace
{
uint32_t U32(std::span<const uint8_t> bytes, size_t offset)
{
    return uint32_t(bytes[offset]) | (uint32_t(bytes[offset + 1]) << 8) |
           (uint32_t(bytes[offset + 2]) << 16) | (uint32_t(bytes[offset + 3]) << 24);
}

std::optional<DdsTextureInfo> ParseHeader(std::span<const uint8_t> bytes, uint64_t fileSize,
                                          std::string_view path, std::string& error)
{
    const auto fail = [&](std::string_view field, std::string_view reason)
    {
        error = std::format("{}: DDS {}: {}", path, field, reason);
        return std::optional<DdsTextureInfo>{};
    };
    // Offsets include the four-byte magic. Read words explicitly: packed C
    // structs would depend on host alignment, endianness and graphics headers.
    if (bytes.size() < 128)
        return fail("header", "Truncated header (expected at least 128 bytes)");
    if (U32(bytes, 0) != 0x20534444)
        return fail("magic", "Expected 'DDS '");
    if (U32(bytes, 4) != 124 || U32(bytes, 76) != 32)
        return fail("header", "Invalid DDS_HEADER or DDS_PIXELFORMAT size");
    DdsTextureInfo info;
    info.Extent = {U32(bytes, 16), U32(bytes, 12)};
    if (!info.Extent.Width || !info.Extent.Height)
        return fail("dimensions", "Width and height must be nonzero; dimensions are never padded");
    const uint32_t flags = U32(bytes, 8), caps2 = U32(bytes, 112);
    if ((flags & 0x800000) || (caps2 & 0x200000) || U32(bytes, 24) > 1)
        return fail("shape", "Volumes are not supported; use a 2D texture or single cubemap");
    info.Levels = std::max(U32(bytes, 28), 1u);
    if (info.Levels > FullTextureMipCount(info.Extent))
        return fail("mip count", "More mip levels than the source dimensions allow");
    const uint32_t pixelFlags = U32(bytes, 80);
    // NVTT's DDPF_NORMAL describes the source; settings still own cooking usage.
    constexpr uint32_t kNormalFlag = 0x80000000u;
    info.bNormalMap = (pixelFlags & kNormalFlag) != 0;
    const bool fourCC = (pixelFlags & 4) != 0;
    if (fourCC && (pixelFlags & ~(4u | 1u | kNormalFlag)))
        return fail("pixel layout", "Unsupported or conflicting DDS_PIXELFORMAT flags");
    if (!fourCC)
    {
        if (!(pixelFlags & 0x40) || (pixelFlags & ~(0x40u | 1u | kNormalFlag)))
            return fail("pixel layout",
                        "Uncompressed DDS requires an 8-bit RGB/BGR/RGBA/BGRA layout");
        const auto bits = U32(bytes, 88), r = U32(bytes, 92), g = U32(bytes, 96);
        const auto b = U32(bytes, 100), a = U32(bytes, 104);
        const bool rgb = r == 0xff && g == 0xff00 && b == 0xff0000;
        const bool bgr = b == 0xff && g == 0xff00 && r == 0xff0000;
        const bool alpha = (pixelFlags & 1) != 0;
        if ((!rgb && !bgr) || (bits != 24 && bits != 32) ||
            (alpha && (bits != 32 || a != 0xff000000)) || (!alpha && a))
            return fail("pixel layout", "Uncompressed DDS has unsupported channel masks/precision; "
                                        "expected 8-bit RGB/BGR/RGBA/BGRA (including RGBX/BGRX)");
        info.PixelLayout = bits == 24 ? (rgb ? DdsPixelLayout::RGB8 : DdsPixelLayout::BGR8)
                           : alpha    ? (rgb ? DdsPixelLayout::RGBA8 : DdsPixelLayout::BGRA8)
                                      : (rgb ? DdsPixelLayout::RGBX8 : DdsPixelLayout::BGRX8);
        info.Format = TextureCompression::Auto;
    }
    const uint32_t code = fourCC ? U32(bytes, 84) : 0;
    uint64_t offset = 128;
    bool cube = (caps2 & 0x200) != 0;
    if (code == 0x30315844) // DX10
    {
        if (bytes.size() < 148)
            return fail("DX10 header", "Truncated extension (expected 20 more bytes)");
        offset = 148;
        const uint32_t dimension = U32(bytes, 132), misc = U32(bytes, 136);
        const uint32_t arraySize = U32(bytes, 140), alpha = U32(bytes, 144);
        if (dimension != 3)
            return fail("shape", "Only 2D textures and single cubemaps are supported; 1D and "
                                 "volume DDS inputs are unsupported");
        if (!arraySize)
            return fail("arraySize", "Must be nonzero");
        if (arraySize != 1)
            return fail("shape", "Texture arrays and cubemap arrays are not supported");
        if (misc & ~4u)
            return fail("miscFlag", "Unsupported DX10 resource flags");
        if (cube && !(misc & 4))
            return fail("shape", "Conflicting legacy and DX10 cubemap flags");
        cube = (misc & 4) != 0;
        if (alpha > 4)
            return fail("miscFlags2", "Unsupported DDS alpha mode or reserved bits");
        info.AlphaMode = static_cast<DdsAlphaMode>(alpha);
        // DDS DXGI identifiers are container values, not backend API types.
        const uint32_t format = U32(bytes, 128);
        switch (format)
        {
            case 71:
            case 72:
                info.Format = TextureCompression::BC1;
                break;
            case 74:
            case 75:
                info.Format = TextureCompression::BC2;
                break;
            case 77:
            case 78:
                info.Format = TextureCompression::BC3;
                break;
            case 80:
                info.Format = TextureCompression::BC4;
                break;
            case 81:
                info.Format = TextureCompression::BC4Signed;
                break;
            case 83:
                info.Format = TextureCompression::BC5;
                break;
            case 84:
                info.Format = TextureCompression::BC5Signed;
                break;
            case 98:
            case 99:
                info.Format = TextureCompression::BC7;
                break;
            case 70:
            case 73:
            case 76:
            case 79:
            case 82:
            case 97:
                return fail("format", "Typeless BC formats have ambiguous interpretation; use an "
                                      "explicit UNORM, SNORM or sRGB DDS format");
            case 94:
            case 95:
            case 96:
                return fail("format", "HDR/BC6H DDS is outside this stage's scope");
            case 28:
            case 29:
                info.PixelLayout = DdsPixelLayout::RGBA8;
                info.Format = TextureCompression::Auto;
                break;
            case 87:
            case 91:
                info.PixelLayout = DdsPixelLayout::BGRA8;
                info.Format = TextureCompression::Auto;
                break;
            case 88:
            case 93:
                info.PixelLayout = DdsPixelLayout::BGRX8;
                info.Format = TextureCompression::Auto;
                break;
            default:
                return fail("format",
                            std::format("Unsupported DX10 format {}; higher-precision/HDR and "
                                        "unusual layouts are unsupported",
                                        format));
        }
        info.ColorSpace = format == 72 || format == 75 || format == 78 || format == 99 ||
                                  format == 29 || format == 91 || format == 93
                              ? TextureColorSpace::Srgb
                              : TextureColorSpace::Linear;
    }
    else if (fourCC)
    {
        switch (code)
        {
            case 0x31545844:
                info.Format = TextureCompression::BC1;
                break;       // DXT1
            case 0x32545844: // DXT2
                info.AlphaMode = DdsAlphaMode::Premultiplied;
                [[fallthrough]];
            case 0x33545844:
                info.Format = TextureCompression::BC2;
                break;       // DXT3
            case 0x34545844: // DXT4
                info.AlphaMode = DdsAlphaMode::Premultiplied;
                [[fallthrough]];
            case 0x35545844:
                info.Format = TextureCompression::BC3;
                break;       // DXT5
            case 0x31495441: // ATI1
            case 0x55344342:
                info.Format = TextureCompression::BC4;
                break; // BC4U
            case 0x53344342:
                info.Format = TextureCompression::BC4Signed;
                break;       // BC4S
            case 0x32495441: // ATI2
            case 0x55354342:
                info.Format = TextureCompression::BC5;
                break; // BC5U
            case 0x53354342:
                info.Format = TextureCompression::BC5Signed;
                break; // BC5S
            default:
                return fail("format", "Unsupported legacy FourCC; expected DXT1–DXT5, "
                                      "ATI1/ATI2, BC4U/BC4S, BC5U/BC5S or DX10");
        }
    }
    // Old NVTT stores swizzle codes in dwRGBBitCount for legacy compressed DDS.
    // https://github.com/nvpro-samples/nvpro_core/blob/master/fileformats/nv_dds.cpp
    if (fourCC && code != 0x30315844)
    {
        const auto swizzle = U32(bytes, 88);
        if (swizzle == 0x59583241u) // A2XY
        {
            if (info.Format != TextureCompression::BC5)
                return fail("swizzle", "NVTT A2XY requires native BC5 UNORM blocks");
            info.ChannelSwizzle = DdsChannelSwizzle::SwapRedGreen;
        }
        else if (swizzle == 0x35443241u) // A2D5
        {
            if (info.Format != TextureCompression::BC3)
                return fail("swizzle", "NVTT A2D5 requires native BC3 blocks");
            info.ChannelSwizzle = DdsChannelSwizzle::SwapRedAlpha;
        }
    }
    const uint32_t faceBits = caps2 & 0xfc00;
    if ((!cube && faceBits) || (cube && code != 0x30315844 && faceBits != 0xfc00) ||
        (cube && code == 0x30315844 && faceBits && faceBits != 0xfc00))
        return fail("faces",
                    "Partial or conflicting cubemap faces are unsupported; supply all six");
    if (cube && info.Extent.Width != info.Extent.Height)
        return fail("dimensions", "Cubemap faces must be square");
    info.Faces = cube ? 6 : 1;
    if (!info.IsCompressed())
    {
        const uint64_t baseRow = uint64_t(info.Extent.Width) * DdsPixelBytes(info.PixelLayout);
        const bool explicitPitch = (flags & 8) != 0;
        const uint64_t pitch = U32(bytes, 20);
        if (explicitPitch && pitch < baseRow)
            return fail("row pitch", "Declared row pitch is smaller than the pixel row");
        // DDS writers use byte or DWORD row alignment. The base pitch is
        // authoritative when supplied; lower levels have no per-mip pitch field.
        // Accept only a complete, uniquely bounded byte/DWORD interpretation.
        for (const bool aligned : {false, true})
        {
            std::vector<DdsSubresource> subresources;
            uint64_t end = offset;
            bool fits = true;
            for (uint32_t face = 0; face < info.Faces && fits; ++face)
                for (uint32_t level = 0; level < info.Levels; ++level)
                {
                    const uint64_t row = uint64_t(std::max(info.Extent.Width >> level, 1u)) *
                                         DdsPixelBytes(info.PixelLayout);
                    const uint64_t rowPitch = level == 0 && explicitPitch ? pitch
                                              : aligned                   ? ((row + 3) / 4) * 4
                                                                          : row;
                    const uint64_t rows = std::max(info.Extent.Height >> level, 1u);
                    if (rowPitch > std::numeric_limits<uint64_t>::max() / rows || end > fileSize ||
                        rowPitch * rows > fileSize - end)
                    {
                        fits = false;
                        break;
                    }
                    subresources.push_back({level, face, end, rowPitch * rows, rowPitch});
                    end += rowPitch * rows;
                }
            if (fits && end == fileSize)
            {
                info.Subresources = std::move(subresources);
                return info;
            }
        }
        return fail("payload",
                    "Truncated/trailing pixel payload or unsupported row-pitch/mip packing");
    }
    const bool smallBlock = info.Format == TextureCompression::BC1 ||
                            info.Format == TextureCompression::BC4 ||
                            info.Format == TextureCompression::BC4Signed;
    const uint64_t blockBytes = smallBlock ? 8 : 16;
    for (uint32_t face = 0; face < info.Faces; ++face)
        for (uint32_t level = 0; level < info.Levels; ++level)
        {
            const uint64_t width = (uint64_t(std::max(info.Extent.Width >> level, 1u)) + 3) / 4;
            const uint64_t height = (uint64_t(std::max(info.Extent.Height >> level, 1u)) + 3) / 4;
            if (width > std::numeric_limits<uint64_t>::max() / height / blockBytes)
                return fail("payload", "Compressed block footprint overflows");
            const uint64_t size = width * height * blockBytes;
            if (offset > fileSize || size > fileSize - offset)
                return fail("payload", std::format("Truncated face {} mip {} (requires {} bytes)",
                                                   face, level, size));
            info.Subresources.push_back({level, face, offset, size});
            offset += size;
        }
    if (offset != fileSize)
        return fail("payload", "Unexpected trailing data; arrays and padded/ambiguous layouts "
                               "are not inferred");
    return info;
}
} // namespace

uint32_t FullTextureMipCount(Core::Extent2D extent)
{
    return std::bit_width(std::max(extent.Width, extent.Height));
}

TextureSettingsSource DdsSettingsSource(const DdsTextureInfo& info, bool bSettingsMissing)
{
    TextureSettingsSource source;
    source.bDds = true;
    source.bSettingsMissing = bSettingsMissing;
    source.bNeedsMipGeneration = info.Levels < FullTextureMipCount(info.Extent);
    if (info.IsCompressed())
        source.NativeFormat = info.Format;
    else
        source.Channels = DdsPixelChannels(info.PixelLayout);
    source.NativeColorSpace = info.ColorSpace;
    // Custom alpha explicitly denotes a data channel rather than transparency.
    // Neither case needs alpha-test coverage processing. Keep the representation
    // itself in DdsTextureInfo and the cooked metadata, not in authored settings.
    // An explicit AG normal swizzle uses alpha to store X, not transparency.
    if (info.AlphaMode == DdsAlphaMode::Opaque || info.AlphaMode == DdsAlphaMode::Custom ||
        info.ChannelSwizzle == DdsChannelSwizzle::SwapRedAlpha)
        source.NativeAlphaMode = TextureAlphaMode::Opaque;
    return source;
}

uint32_t DdsPixelBytes(DdsPixelLayout layout)
{
    return layout == DdsPixelLayout::Compressed                               ? 0
           : layout == DdsPixelLayout::RGB8 || layout == DdsPixelLayout::BGR8 ? 3
                                                                              : 4;
}

uint32_t DdsPixelChannels(DdsPixelLayout layout)
{
    return layout == DdsPixelLayout::Compressed                                 ? 0
           : layout == DdsPixelLayout::RGBA8 || layout == DdsPixelLayout::BGRA8 ? 4
                                                                                : 3;
}

std::optional<DdsTextureInfo> ReadDdsInfo(const std::filesystem::path& path, std::string& error)
{
    error.clear();
    std::ifstream file(path, std::ios::binary | std::ios::ate);
    if (!file || file.tellg() < 0)
    {
        error = std::format("{}: Cannot read DDS file", path.string());
        return std::nullopt;
    }
    const auto size = static_cast<uint64_t>(file.tellg());
    file.seekg(0);
    std::array<uint8_t, 148> header{};
    file.read(reinterpret_cast<char*>(header.data()), header.size());
    if (file.bad())
    {
        error = std::format("{}: Failed while reading DDS header", path.string());
        return std::nullopt;
    }
    return ParseHeader(std::span(header).first(static_cast<size_t>(file.gcount())), size,
                       path.string(), error);
}

std::optional<Image> ReadDdsPixels(const std::filesystem::path& path, const DdsTextureInfo& info,
                                   uint32_t level, uint32_t face, std::string& error)
{
    error.clear();
    const auto current = ReadDdsInfo(path, error);
    if (!current)
        return std::nullopt;
    if (*current != info || info.IsCompressed())
    {
        error = std::format("{}: Pixel DDS metadata changed or requires a compressed decoder",
                            path.string());
        return std::nullopt;
    }
    const auto entry =
        std::find_if(info.Subresources.begin(), info.Subresources.end(),
                     [&](const auto& sub) { return sub.Level == level && sub.Face == face; });
    if (entry == info.Subresources.end())
    {
        error = std::format("{}: DDS face/mip is out of range", path.string());
        return std::nullopt;
    }
    const Core::Extent2D extent{std::max(info.Extent.Width >> level, 1u),
                                std::max(info.Extent.Height >> level, 1u)};
    if (extent.Width > 16384 || extent.Height > 16384)
    {
        error = std::format("{}: Pixel cooking supports desktop 2D dimensions up to 16384; no "
                            "resizing is performed",
                            path.string());
        return std::nullopt;
    }
    Image image{{}, extent, DdsPixelChannels(info.PixelLayout)};
    image.Pixels.resize(size_t(extent.Width) * extent.Height * 4);
    std::vector<uint8_t> row(size_t(extent.Width) * DdsPixelBytes(info.PixelLayout));
    std::ifstream file(path, std::ios::binary);
    const bool bgr = info.PixelLayout == DdsPixelLayout::BGR8 ||
                     info.PixelLayout == DdsPixelLayout::BGRA8 ||
                     info.PixelLayout == DdsPixelLayout::BGRX8;
    for (uint32_t y = 0; y < extent.Height; ++y)
    {
        file.seekg(static_cast<std::streamoff>(entry->Offset + y * entry->RowPitch));
        file.read(reinterpret_cast<char*>(row.data()), static_cast<std::streamsize>(row.size()));
        if (!file)
        {
            error = std::format("{}: Cannot read DDS pixel row {}", path.string(), y);
            return std::nullopt;
        }
        for (uint32_t x = 0; x < extent.Width; ++x)
        {
            const size_t input = size_t(x) * DdsPixelBytes(info.PixelLayout);
            const size_t output = (size_t(y) * extent.Width + x) * 4;
            image.Pixels[output] = row[input + (bgr ? 2 : 0)];
            image.Pixels[output + 1] = row[input + 1];
            image.Pixels[output + 2] = row[input + (bgr ? 0 : 2)];
            image.Pixels[output + 3] = image.SourceChannels == 4 ? row[input + 3] : 255;
        }
    }
    return image;
}

} // namespace Hikari::Asset
