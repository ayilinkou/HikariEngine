#include <asset/KtxReader.h>

#include <algorithm>
#include <array>
#include <bit>
#include <charconv>
#include <cmath>
#include <cstring>
#include <format>
#include <fstream>
#include <limits>
#include <memory>
#include <span>
#include <stdexcept>
#include <string_view>
#include <unordered_set>

#include <KHR/khr_df.h>
#include <ktx.h>

#include <rhi/TextureFootprint.h>

#include "KtxFormat.h"

namespace Hikari::Asset
{
namespace
{
using Bytes = std::span<const uint8_t>;

uint64_t ReadUnsigned(Bytes bytes, size_t offset, size_t width)
{
    if (offset > bytes.size() || width > bytes.size() - offset)
        throw std::runtime_error("truncated header or level index");
    uint64_t value = 0;
    for (size_t i = 0; i < width; ++i)
        value |= static_cast<uint64_t>(bytes[offset + i]) << (i * 8u);
    return value;
}

Rhi::Format MapFormat(uint32_t format)
{
    switch (static_cast<KtxFormat>(format))
    {
        case KtxFormat::BC1RgbaUnorm:
            return Rhi::Format::BC1Unorm;
        case KtxFormat::BC1RgbaSrgb:
            return Rhi::Format::BC1Srgb;
        case KtxFormat::BC2Unorm:
            return Rhi::Format::BC2Unorm;
        case KtxFormat::BC2Srgb:
            return Rhi::Format::BC2Srgb;
        case KtxFormat::BC3Unorm:
            return Rhi::Format::BC3Unorm;
        case KtxFormat::BC3Srgb:
            return Rhi::Format::BC3Srgb;
        case KtxFormat::BC4Unorm:
            return Rhi::Format::BC4Unorm;
        case KtxFormat::BC4Snorm:
            return Rhi::Format::BC4Snorm;
        case KtxFormat::BC5Unorm:
            return Rhi::Format::BC5Unorm;
        case KtxFormat::BC5Snorm:
            return Rhi::Format::BC5Snorm;
        case KtxFormat::BC7Unorm:
            return Rhi::Format::BC7Unorm;
        case KtxFormat::BC7Srgb:
            return Rhi::Format::BC7Srgb;
        case KtxFormat::BC1RgbUnorm:
        case KtxFormat::BC1RgbSrgb:
            // The RHI's BC1 is RGBA. RGB's selector 3 in three-colour mode
            // means opaque black, whereas RGBA means transparent black.
            throw std::runtime_error("BC1 RGB interpretation is unsupported by the neutral RHI; "
                                     "use BC1 RGBA KTX2");
    }
    throw std::runtime_error(std::format("unsupported native format {} or required transcoding; "
                                         "expected BC1 RGBA, BC2/3/4/5/7",
                                         format));
}

void Require(bool condition, std::string_view message)
{
    if (!condition)
        throw std::runtime_error(std::string(message));
}

struct KtxDeleter
{
    void operator()(ktxTexture2* texture) const { ktxTexture_Destroy(ktxTexture(texture)); }
};
using KtxOwner = std::unique_ptr<ktxTexture2, KtxDeleter>;

void CheckKtx(KTX_error_code status)
{
    if (status != KTX_SUCCESS)
        throw std::runtime_error(std::format("libktx: {}", ktxErrorString(status)));
}

/**
 * Check byte ranges before libktx rebases the level index or allocates image
 * storage. Its reader does not check every level's length against the format.
 * Layout and alignment follow KTX 2 sections 3.9, 3.10 and 3.13:
 * https://registry.khronos.org/KTX/specs/2.0/ktxspec.v2.html
 */
TextureData ValidateContainer(Bytes bytes)
{
    constexpr std::array<uint8_t, 12> kIdentifier{0xAB, 0x4B, 0x54, 0x58, 0x20, 0x32,
                                                  0x30, 0xBB, 0x0D, 0x0A, 0x1A, 0x0A};
    Require(bytes.size() >= 80 && std::equal(kIdentifier.begin(), kIdentifier.end(), bytes.begin()),
            "expected a complete KTX2 header and identifier");
    const auto u32 = [&](size_t offset)
    { return static_cast<uint32_t>(ReadUnsigned(bytes, offset, 4)); };
    Require(u32(44) == KTX_SS_NONE,
            "supercompression/transcoding is unsupported; use native BC KTX2");
    TextureData data;
    data.Format = MapFormat(u32(12));
    data.Extent = {u32(20), u32(24)};
    data.Faces = u32(36);
    data.MipLevels = u32(40);
    Require(u32(16) == 1, "compressed typeSize must be 1");
    Require(data.Extent.Width != 0 && data.Extent.Height != 0 && u32(28) == 0 && u32(32) == 0 &&
                (data.Faces == 1 || data.Faces == 6),
            "only ordinary 2D textures and single cubemaps are supported; arrays, 1D and volumes "
            "are unsupported");
    Require(data.Faces != 6 || data.Extent.Width == data.Extent.Height,
            "cubemap faces must be square");
    Require(
        data.MipLevels != 0 && data.MipLevels <= static_cast<uint32_t>(std::bit_width(std::max(
                                                     data.Extent.Width, data.Extent.Height))),
        "invalid levelCount; supply 1 through the full mip count, without implicit mip generation");
    const uint64_t indexEnd = 80u + 24u * data.MipLevels;
    const uint64_t dfdOffset = u32(48), dfdLength = u32(52);
    const uint64_t kvdOffset = u32(56), kvdLength = u32(60);
    const auto inFile = [&](uint64_t offset, uint64_t size)
    { return offset <= bytes.size() && size <= bytes.size() - offset; };
    Require(indexEnd <= bytes.size() && dfdOffset == indexEnd && dfdLength >= 28 &&
                dfdLength % 4 == 0 && inFile(dfdOffset, dfdLength),
            "invalid data format descriptor range");
    Require(
        (kvdLength == 0 && kvdOffset == 0) ||
            (kvdLength != 0 && kvdOffset == dfdOffset + dfdLength && inFile(kvdOffset, kvdLength)),
        "invalid key/value metadata range");
    Require(ReadUnsigned(bytes, 64, 8) == 0 && ReadUnsigned(bytes, 72, 8) == 0,
            "supercompression global data is unsupported");

    // Generate the format's canonical DFD through the same pinned library.
    // Primaries and premultiplication are independent metadata; block model,
    // signedness, sample layout, transfer function and sizes must match.
    ktxTextureCreateInfo info{};
    info.vkFormat = u32(12);
    info.baseWidth = info.baseHeight = 4;
    info.baseDepth = info.numLevels = info.numLayers = info.numFaces = 1;
    info.numDimensions = 2;
    ktxTexture2* expected = nullptr;
    CheckKtx(ktxTexture2_Create(&info, KTX_TEXTURE_CREATE_NO_STORAGE, &expected));
    const KtxOwner expectedOwner(expected);
    Require(expected->pDfd[0] == dfdLength && u32(static_cast<size_t>(dfdOffset)) == dfdLength,
            "data format descriptor size does not match the native format");
    const auto* canonical = reinterpret_cast<const uint8_t*>(expected->pDfd);
    const Bytes dfd = bytes.subspan(static_cast<size_t>(dfdOffset), static_cast<size_t>(dfdLength));
    for (size_t i = 0; i < dfd.size(); ++i)
        if (i != 13 && i != 15)
            Require(dfd[i] == canonical[i],
                    "data format descriptor does not match the native format");
    Require((dfd[15] & ~KHR_DF_FLAG_ALPHA_PREMULTIPLIED) == 0,
            "unsupported data format descriptor flags");
    data.Metadata.ColorPrimaries = dfd[13];
    data.Metadata.bPremultipliedAlpha = (dfd[15] & KHR_DF_FLAG_ALPHA_PREMULTIPLIED) != 0;

    // Validate KVD entry boundaries and duplicate keys before libktx's hash list
    // can hide a duplicate or accept a string extending into another entry.
    std::unordered_set<std::string_view> keys;
    size_t position = static_cast<size_t>(kvdOffset);
    const size_t kvdEnd = position + static_cast<size_t>(kvdLength);
    while (position < kvdEnd)
    {
        Require(kvdEnd - position >= 4, "truncated key/value metadata entry");
        const size_t size = u32(position);
        position += 4;
        Require(size != 0 && size <= kvdEnd - position, "invalid key/value metadata entry size");
        const Bytes entry = bytes.subspan(position, size);
        const auto terminator = std::find(entry.begin(), entry.end(), uint8_t{0});
        Require(terminator != entry.begin() && terminator != entry.end(), "invalid metadata key");
        const std::string_view key(reinterpret_cast<const char*>(entry.data()),
                                   static_cast<size_t>(terminator - entry.begin()));
        Require(keys.insert(key).second, "duplicate metadata key");
        const size_t padded = (size + 3u) & ~size_t{3};
        Require(padded <= kvdEnd - position, "truncated key/value metadata padding");
        position += padded;
    }

    const uint64_t alignment = Rhi::BytesPerBlock(data.Format);
    uint64_t cursor = dfdOffset + dfdLength + kvdLength;
    for (uint32_t level = data.MipLevels; level-- > 0;)
    {
        cursor = (cursor + alignment - 1) / alignment * alignment;
        const size_t entry = 80u + 24u * level;
        const uint64_t offset = ReadUnsigned(bytes, entry, 8);
        const uint64_t size = ReadUnsigned(bytes, entry + 8, 8);
        const Core::Extent3D extent{std::max(data.Extent.Width >> level, 1u),
                                    std::max(data.Extent.Height >> level, 1u), 1u};
        const uint64_t expectedSize =
            Rhi::ComputeTextureFootprint(data.Format, extent, data.Faces).TotalBytes;
        Require(size == expectedSize && ReadUnsigned(bytes, entry + 16, 8) == expectedSize,
                "level payload size does not match the native block footprint");
        Require(offset == cursor && inFile(offset, size),
                "invalid level offset/order/alignment or truncated payload");
        cursor += size;
    }
    Require(cursor == bytes.size(), "unexpected trailing payload bytes");
    return data;
}

std::optional<std::string_view> TextMetadata(ktxTexture2* texture, const char* key)
{
    unsigned int size = 0;
    void* value = nullptr;
    const auto status = ktxHashList_FindValue(&texture->kvDataHead, key, &size, &value);
    if (status == KTX_NOT_FOUND)
        return std::nullopt;
    CheckKtx(status);
    Require(size > 0 && value != nullptr, std::format("empty {} metadata", key));
    const auto* text = static_cast<const char*>(value);
    Require(text[size - 1] == '\0' && std::memchr(text, 0, size - 1) == nullptr,
            std::format("invalid {} metadata string", key));
    return std::string_view(text, size - 1);
}

void ReadSemantics(ktxTexture2* texture, TextureData& data)
{
    auto& metadata = data.Metadata;
    const auto readEnum =
        [&]<typename T>(const char* key, std::optional<T>& destination, auto parse)
    {
        if (const auto text = TextMetadata(texture, key))
        {
            destination = parse(*text);
            Require(destination.has_value(), std::format("invalid {} metadata", key));
        }
    };
    if (const auto orientation = TextMetadata(texture, KTX_ORIENTATION_KEY))
        Require(*orientation == "rd", "unsupported KTXorientation; expected rd (right/down)");
    if (const auto swizzle = TextMetadata(texture, "KTXswizzle"))
        Require(*swizzle == "rgba",
                "unsupported KTXswizzle; component remapping is not implemented");
    readEnum("HikariTextureUsage", metadata.Usage, ParseTextureUsage);
    readEnum("HikariTextureAlphaMode", metadata.AlphaMode, ParseTextureAlphaMode);
    if (const auto cutoff = TextMetadata(texture, "HikariTextureAlphaCutoff"))
    {
        double value = 0;
        const auto [end, ec] =
            std::from_chars(cutoff->data(), cutoff->data() + cutoff->size(), value);
        Require(
            ec == std::errc{} && end == cutoff->data() + cutoff->size() && std::isfinite(value) &&
                value >= 0 && value <= 1 && metadata.AlphaMode == TextureAlphaMode::Mask,
            "invalid HikariTextureAlphaCutoff metadata; mask cutoff must be finite and in [0, 1]");
        metadata.AlphaCutoff = value;
    }
    Require(metadata.AlphaMode != TextureAlphaMode::Mask || metadata.AlphaCutoff.has_value(),
            "mask metadata requires HikariTextureAlphaCutoff");
    Require(metadata.AlphaMode != TextureAlphaMode::Mask ||
                (data.Format != Rhi::Format::BC4Unorm && data.Format != Rhi::Format::BC4Snorm &&
                 data.Format != Rhi::Format::BC5Unorm && data.Format != Rhi::Format::BC5Snorm),
            "mask metadata requires an alpha-capable format");
    if (const auto normal = TextMetadata(texture, "HikariNormalEncoding"))
    {
        if (*normal == "xyz-unorm")
            metadata.NormalEncoding = TextureNormalEncoding::XyzUnorm;
        else if (*normal == "xy-unorm-positive-z")
            metadata.NormalEncoding = TextureNormalEncoding::XyUnormPositiveZ;
        else if (*normal == "xy-snorm-positive-z")
            metadata.NormalEncoding = TextureNormalEncoding::XySnormPositiveZ;
        else if (*normal == "yx-unorm-positive-z")
            metadata.NormalEncoding = TextureNormalEncoding::YxUnormPositiveZ;
        else if (*normal == "ag-unorm-positive-z")
            metadata.NormalEncoding = TextureNormalEncoding::AgUnormPositiveZ;
        else
            throw std::runtime_error("unsupported HikariNormalEncoding metadata");
        Require(metadata.Usage == TextureUsage::Normal,
                "normal encoding requires normal usage metadata");
        const auto encoding = *metadata.NormalEncoding;
        const bool xyz = encoding == TextureNormalEncoding::XyzUnorm;
        Require((xyz &&
                 (data.Format == Rhi::Format::BC1Unorm || data.Format == Rhi::Format::BC2Unorm ||
                  data.Format == Rhi::Format::BC3Unorm || data.Format == Rhi::Format::BC7Unorm)) ||
                    ((encoding == TextureNormalEncoding::XyUnormPositiveZ ||
                      encoding == TextureNormalEncoding::YxUnormPositiveZ) &&
                     data.Format == Rhi::Format::BC5Unorm) ||
                    (encoding == TextureNormalEncoding::XySnormPositiveZ &&
                     data.Format == Rhi::Format::BC5Snorm) ||
                    (encoding == TextureNormalEncoding::AgUnormPositiveZ &&
                     data.Format == Rhi::Format::BC3Unorm),
                "normal encoding does not match the native format");
    }
    Require(metadata.Usage != TextureUsage::Normal || metadata.NormalEncoding.has_value(),
            "normal usage requires HikariNormalEncoding metadata");
    if (const auto alpha = TextMetadata(texture, "HikariDdsAlphaMode"))
    {
        if (*alpha == "straight")
            metadata.DdsAlpha = DdsAlphaMode::Straight;
        else if (*alpha == "premultiplied")
            metadata.DdsAlpha = DdsAlphaMode::Premultiplied;
        else if (*alpha == "opaque")
            metadata.DdsAlpha = DdsAlphaMode::Opaque;
        else if (*alpha == "custom")
            metadata.DdsAlpha = DdsAlphaMode::Custom;
        else
            throw std::runtime_error("invalid HikariDdsAlphaMode metadata");
        Require((*alpha == "premultiplied") == metadata.bPremultipliedAlpha,
                "DDS alpha metadata disagrees with the data format descriptor");
    }
    if (const auto flag = TextMetadata(texture, "HikariDdsNormalMap"))
    {
        Require(*flag == "true", "invalid HikariDdsNormalMap metadata");
        metadata.bDdsNormalMap = true;
    }
    if (const auto swizzle = TextMetadata(texture, "HikariDdsSwizzle"))
    {
        if (*swizzle == "A2XY")
            metadata.DdsSwizzle = DdsChannelSwizzle::SwapRedGreen;
        else if (*swizzle == "A2D5")
            metadata.DdsSwizzle = DdsChannelSwizzle::SwapRedAlpha;
        else
            throw std::runtime_error("unsupported HikariDdsSwizzle metadata");
        Require((*swizzle == "A2XY" && data.Format == Rhi::Format::BC5Unorm) ||
                    (*swizzle == "A2D5" &&
                     (data.Format == Rhi::Format::BC3Unorm || data.Format == Rhi::Format::BC3Srgb)),
                "DDS swizzle metadata does not match the native format");
    }
    if (metadata.Usage == TextureUsage::Normal)
    {
        Require((metadata.DdsSwizzle == DdsChannelSwizzle::SwapRedGreen) ==
                        (metadata.NormalEncoding == TextureNormalEncoding::YxUnormPositiveZ) &&
                    (metadata.DdsSwizzle == DdsChannelSwizzle::SwapRedAlpha) ==
                        (metadata.NormalEncoding == TextureNormalEncoding::AgUnormPositiveZ),
                "DDS swizzle and normal encoding metadata disagree");
    }
}
} // namespace

std::optional<TextureData> ReadKtxTexture(const std::filesystem::path& path, std::string& error)
{
    error.clear();
    try
    {
        std::ifstream file(path, std::ios::binary | std::ios::ate);
        Require(file.is_open(), "cannot open file");
        const auto size = file.tellg();
        Require(size >= 0 && static_cast<uint64_t>(size) <= std::numeric_limits<size_t>::max() &&
                    static_cast<uint64_t>(size) <=
                        static_cast<uint64_t>(std::numeric_limits<std::streamsize>::max()),
                "invalid or unrepresentable file size");
        std::vector<uint8_t> bytes(static_cast<size_t>(size));
        file.seekg(0);
        file.read(reinterpret_cast<char*>(bytes.data()),
                  static_cast<std::streamsize>(bytes.size()));
        Require(static_cast<bool>(file), "cannot read complete file");
        TextureData data = ValidateContainer(bytes);
        ktxTexture2* texture = nullptr;
        CheckKtx(ktxTexture2_CreateFromMemory(bytes.data(), bytes.size(),
                                              KTX_TEXTURE_CREATE_LOAD_IMAGE_DATA_BIT, &texture));
        const KtxOwner owner(texture);
        Require(!ktxTexture2_NeedsTranscoding(texture) && texture->pData != nullptr &&
                    texture->isCompressed,
                "expected loaded native compressed blocks without transcoding");
        ReadSemantics(texture, data);
        // Range insertion constructs individual bytes in unoptimized builds.
        // Allocate once and copy complete native subresources instead.
        data.Data.resize(texture->dataSize);
        size_t outputOffset = 0;
        for (uint32_t mip = 0; mip < data.MipLevels; ++mip)
        {
            const Core::Extent3D extent{std::max(data.Extent.Width >> mip, 1u),
                                        std::max(data.Extent.Height >> mip, 1u), 1u};
            const uint64_t imageSize = Rhi::ComputeTextureFootprint(data.Format, extent).TotalBytes;
            Require(ktxTexture_GetImageSize(ktxTexture(texture), mip) == imageSize,
                    "libktx image size does not match the native block footprint");
            for (uint32_t face = 0; face < data.Faces; ++face)
            {
                ktx_size_t offset = 0;
                CheckKtx(ktxTexture_GetImageOffset(ktxTexture(texture), mip, 0, face, &offset));
                Require(offset <= texture->dataSize && imageSize <= texture->dataSize - offset,
                        "libktx subresource exceeds the loaded payload");
                Require(outputOffset <= data.Data.size() &&
                            imageSize <= data.Data.size() - outputOffset,
                        "subresources exceed the output payload");
                std::memcpy(data.Data.data() + outputOffset, texture->pData + offset,
                            static_cast<size_t>(imageSize));
                data.Subresources.push_back({.MipLevel = mip,
                                             .Face = face,
                                             .Extent = extent,
                                             .Offset = outputOffset,
                                             .Size = static_cast<size_t>(imageSize)});
                outputOffset += static_cast<size_t>(imageSize);
            }
        }
        Require(outputOffset == data.Data.size(), "subresources do not fill the output payload");
        return data;
    }
    catch (const std::exception& exception)
    {
        error = std::format("{}: Cannot load KTX2: {}", path.string(), exception.what());
        return std::nullopt;
    }
}
} // namespace Hikari::Asset
