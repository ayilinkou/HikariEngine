#include <algorithm>
#include <array>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <memory>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

#include <asset/ImageWriter.h>
#include <ktx.h>
#include <vulkan/vulkan_core.h>

namespace
{
void Require(bool condition, std::string_view message)
{
    if (!condition)
        throw std::runtime_error(std::string(message));
}

void CheckKtx(KTX_error_code result)
{
    Require(result == KTX_SUCCESS, ktxErrorString(result));
}

std::vector<uint8_t> Read(const std::filesystem::path& path)
{
    std::ifstream file(path, std::ios::binary);
    Require(file.good(), "Cannot open probe output");
    return {std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>()};
}

uint32_t U32(const std::vector<uint8_t>& bytes, size_t offset)
{
    Require(offset + 4 <= bytes.size(), "Truncated probe output");
    return uint32_t(bytes[offset]) | (uint32_t(bytes[offset + 1]) << 8) |
           (uint32_t(bytes[offset + 2]) << 16) | (uint32_t(bytes[offset + 3]) << 24);
}

void CheckHeader(const std::vector<uint8_t>& bytes, uint32_t width, uint32_t height,
                 uint32_t levels)
{
    Require(U32(bytes, 0) == 0x20534444 && U32(bytes, 4) == 124 && U32(bytes, 76) == 32,
            "Probe output is not DDS");
    Require(U32(bytes, 16) == width && U32(bytes, 12) == height,
            "Encoder changed logical dimensions");
    Require(std::max(U32(bytes, 28), 1u) == levels, "Unexpected mip count");
}

void Generate(const std::filesystem::path& directory)
{
    std::filesystem::create_directories(directory);
    const auto write = [&](std::string_view name, uint32_t width, uint32_t height,
                           const std::vector<uint8_t>& pixels)
    {
        Require(Hikari::Asset::WritePng(pixels, {width, height}, (directory / name).string()),
                "Could not create probe fixture");
    };
    for (const auto [width, height] :
         std::array<std::array<uint32_t, 2>, 5>{{{1, 1}, {2, 2}, {4, 4}, {7, 5}, {8, 4}}})
    {
        std::vector<uint8_t> pixels(width * height * 4);
        for (size_t i = 0; i < pixels.size(); i += 4)
        {
            pixels[i] = 64;
            pixels[i + 1] = 192;
            pixels[i + 2] = 128;
            pixels[i + 3] = 255;
        }
        write(std::to_string(width) + "x" + std::to_string(height) + ".png", width, height, pixels);
    }

    std::vector<uint8_t> bw(4 * 4 * 4), normal(bw.size()), mask(bw.size());
    for (size_t i = 0; i < bw.size(); i += 4)
    {
        const bool bOdd = (i / 4) % 2 != 0;
        bw[i] = bw[i + 1] = bw[i + 2] = bOdd ? 255 : 0;
        bw[i + 3] = 255;
        normal[i] = bOdd ? 218 : 128;
        normal[i + 1] = 128;
        normal[i + 2] = bOdd ? 218 : 255;
        normal[i + 3] = 255;
        mask[i] = mask[i + 1] = mask[i + 2] = 255;
        mask[i + 3] = bOdd ? 128 : 127;
    }
    write("bw.png", 4, 4, bw);
    write("normal.png", 4, 4, normal);
    write("mask.png", 4, 4, mask);
}

/** Container identifiers for the fixed probe fixtures, not a production DDS parser. */
struct Format
{
    std::string_view Name;
    std::string_view FourCC;
    uint32_t BlockBytes;
    VkFormat KtxFormat;
};

constexpr std::array kFormats{Format{"BC1", "DXT1", 8, VK_FORMAT_BC1_RGBA_UNORM_BLOCK},
                              Format{"BC2", "DXT3", 16, VK_FORMAT_BC2_UNORM_BLOCK},
                              Format{"BC3", "DXT5", 16, VK_FORMAT_BC3_UNORM_BLOCK},
                              Format{"BC4", "ATI1", 8, VK_FORMAT_BC4_UNORM_BLOCK},
                              Format{"BC4_S", "BC4S", 8, VK_FORMAT_BC4_SNORM_BLOCK},
                              Format{"BC5", "ATI2", 16, VK_FORMAT_BC5_UNORM_BLOCK},
                              Format{"BC5_S", "BC5S", 16, VK_FORMAT_BC5_SNORM_BLOCK},
                              Format{"BC7", "DX10", 16, VK_FORMAT_BC7_UNORM_BLOCK}};

void Encoded(const std::filesystem::path& path, std::string_view name, uint32_t width,
             uint32_t height, uint32_t levels)
{
    const auto bytes = Read(path);
    CheckHeader(bytes, width, height, levels);
    const auto format = std::find_if(kFormats.begin(), kFormats.end(),
                                     [&](const Format& entry) { return entry.Name == name; });
    Require(format != kFormats.end(), "Unknown probe format");
    const std::string_view fourCC(reinterpret_cast<const char*>(bytes.data() + 84), 4);
    Require(fourCC == format->FourCC, "Incorrect compressed format or signedness");
    size_t sourceOffset = fourCC == "DX10" ? 148 : 128;
    if (fourCC == "DX10")
        Require(U32(bytes, 128) == 98, "Incorrect BC7 DXGI format");

    ktxTextureCreateInfo info{};
    info.vkFormat = format->KtxFormat;
    info.baseWidth = width;
    info.baseHeight = height;
    info.baseDepth = 1;
    info.numDimensions = 2;
    info.numLevels = levels;
    info.numLayers = 1;
    info.numFaces = 1;
    ktxTexture2* pCreated = nullptr;
    CheckKtx(ktxTexture2_Create(&info, KTX_TEXTURE_CREATE_ALLOC_STORAGE, &pCreated));
    const auto destroy = [](ktxTexture2* pTexture) { ktxTexture_Destroy(ktxTexture(pTexture)); };
    const std::unique_ptr<ktxTexture2, decltype(destroy)> created(pCreated, destroy);

    std::vector<size_t> offsets, sizes;
    for (uint32_t level = 0; level < levels; ++level)
    {
        const size_t size = size_t((std::max(width >> level, 1u) + 3) / 4) *
                            ((std::max(height >> level, 1u) + 3) / 4) * format->BlockBytes;
        Require(sourceOffset + size <= bytes.size(), "Compressed mip is truncated");
        Require(ktxTexture_GetImageSize(ktxTexture(pCreated), level) == size,
                "libktx disagrees on whole-block mip footprint");
        CheckKtx(ktxTexture_SetImageFromMemory(ktxTexture(pCreated), level, 0, 0,
                                               bytes.data() + sourceOffset, size));
        offsets.push_back(sourceOffset);
        sizes.push_back(size);
        sourceOffset += size;
    }
    Require(sourceOffset == bytes.size(), "Unexpected compressed payload size");

    ktx_uint8_t* pSerialized = nullptr;
    ktx_size_t serializedSize = 0;
    CheckKtx(ktxTexture2_WriteToMemory(pCreated, &pSerialized, &serializedSize));
    const std::unique_ptr<ktx_uint8_t, decltype(&std::free)> serialized(pSerialized, &std::free);
    ktxTexture2* pReopened = nullptr;
    CheckKtx(ktxTexture2_CreateFromMemory(pSerialized, serializedSize,
                                          KTX_TEXTURE_CREATE_LOAD_IMAGE_DATA_BIT, &pReopened));
    const std::unique_ptr<ktxTexture2, decltype(destroy)> reopened(pReopened, destroy);
    Require(pReopened->vkFormat == info.vkFormat && pReopened->baseWidth == width &&
                pReopened->baseHeight == height && pReopened->numLevels == levels,
            "libktx changed probe metadata");
    for (uint32_t level = 0; level < levels; ++level)
    {
        ktx_size_t offset = 0;
        CheckKtx(ktxTexture_GetImageOffset(ktxTexture(pReopened), level, 0, 0, &offset));
        Require(offset + sizes[level] <= pReopened->dataSize, "libktx mip out of bounds");
        Require(std::equal(bytes.begin() + offsets[level],
                           bytes.begin() + offsets[level] + sizes[level],
                           pReopened->pData + offset),
                "libktx changed compressed blocks");
    }
}

/** Read channels using the decoder's masks; signed DDS uses a different layout. */
std::array<int, 4> Pixel(const std::vector<uint8_t>& bytes, size_t offset)
{
    const uint32_t packed = U32(bytes, offset);
    std::array<int, 4> result{};
    for (size_t channel = 0; channel < result.size(); ++channel)
    {
        const uint32_t mask = U32(bytes, 92 + channel * 4);
        Require(mask != 0, "Missing decoded channel mask");
        uint32_t shift = 0;
        while (((mask >> shift) & 1u) == 0)
            ++shift;
        Require((mask >> shift) == 255, "Unexpected decoded channel layout");
        int value = static_cast<int>((packed & mask) >> shift);
        if ((U32(bytes, 80) & 0x80000) != 0 && value >= 128)
            value -= 256;
        result[channel] = value;
    }
    return result;
}

void Decoded(const std::filesystem::path& path, std::string_view name, uint32_t width,
             uint32_t height)
{
    const auto bytes = Read(path);
    CheckHeader(bytes, width, height, 1);
    Require(U32(bytes, 88) == 32 && bytes.size() == 128 + size_t(width) * height * 4,
            "Unexpected decoded pixel footprint");
    const bool bSigned = name == "BC4_S" || name == "BC5_S";
    Require(((U32(bytes, 80) & 0x80000) != 0) == bSigned,
            "Decoder lost signed-channel interpretation");
    for (size_t offset = 128; offset < bytes.size(); offset += 4)
    {
        const auto rgba = Pixel(bytes, offset);
        const int red = bSigned ? -63 : 64;
        Require(std::abs(rgba[0] - red) <= 4, "Red channel decode is incorrect");
        if (name != "BC4" && name != "BC4_S")
            Require(std::abs(rgba[1] - (bSigned ? 64 : 192)) <= 4,
                    "Green channel decode is incorrect");
        if (name == "BC1" || name == "BC2" || name == "BC3" || name == "BC7")
            Require(std::abs(rgba[2] - 128) <= 4 && std::abs(rgba[3] - 255) <= 2,
                    "Blue/alpha channel decode is incorrect");
    }
}

void Filtering(const std::filesystem::path& path, std::string_view name)
{
    const auto bytes = Read(path);
    CheckHeader(bytes, 4, 4, 3);
    Require(bytes.size() == 128 + (16 + 4 + 1) * 4, "Unexpected filtered footprint");
    const auto base = Pixel(bytes, 128);
    const auto mip = Pixel(bytes, 192);
    if (name == "bw")
        Require(base[0] == 0 && mip[0] == 127, "CPU box filtering changed");
    else if (name == "gamma")
        Require(base[0] == 0 && mip[0] >= 53 && mip[0] <= 57,
                "Post-average gamma behaviour changed; recheck colour filtering");
    else if (name == "normal")
        Require(mip[0] == 173 && mip[1] == 128 && mip[2] == 236,
                "CPU normal channel averaging changed; recheck normal filtering");
    else
        throw std::runtime_error("Unknown filtering probe");
}

void Alpha(const std::filesystem::path& path, bool bBinary)
{
    const auto bytes = Read(path);
    CheckHeader(bytes, 4, 4, 1);
    for (size_t pixel = 0; pixel < 16; ++pixel)
    {
        const int alpha = Pixel(bytes, 128 + pixel * 4)[3];
        const int expected = bBinary ? (pixel % 2 ? 255 : 0) : (pixel % 2 ? 128 : 127);
        Require(std::abs(alpha - expected) <= (bBinary ? 0 : 2),
                "Alpha threshold or varying-alpha storage is incorrect");
    }
}
} // namespace

int main(int argc, char** argv)
{
    try
    {
        Require(argc >= 3, "Missing probe arguments");
        const std::string_view mode = argv[1];
        if (mode == "generate" && argc == 3)
            Generate(argv[2]);
        else if (mode == "encoded" && argc == 7)
            Encoded(argv[2], argv[3], std::stoul(argv[4]), std::stoul(argv[5]),
                    std::stoul(argv[6]));
        else if (mode == "decoded" && argc == 6)
            Decoded(argv[2], argv[3], std::stoul(argv[4]), std::stoul(argv[5]));
        else if (mode == "filter" && argc == 4)
            Filtering(argv[2], argv[3]);
        else if (mode == "alpha" && argc == 4)
            Alpha(argv[2], std::string_view(argv[3]) == "binary");
        else
            throw std::runtime_error("Invalid probe arguments");
        return 0;
    }
    catch (const std::exception& error)
    {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
