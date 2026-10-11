#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include <ktx.h>
#include <vulkan/vulkan_core.h>

#include <asset/DdsReader.h>
#include <asset/ImageWriter.h>
#include <asset/TextureCooker.h>

#include "DdsFixture.h"
#include "PixelDdsFixture.h"
#include "PixelTextureEncoder.h"
#include "TextureBlockCodec.h"
#include "TextureCookFixture.h"
#include "TexturePixelFilter.h"

using namespace Hikari::Asset;
namespace fs = std::filesystem;
namespace
{
using Pixel = std::array<uint8_t, 4>;
using PixelFunction = std::function<Pixel(uint32_t, uint32_t, uint32_t, uint32_t)>;

Image MakeImage(uint32_t width, uint32_t height,
                const std::function<Pixel(uint32_t, uint32_t)>& pixel)
{
    Image image{{}, {width, height}, 4};
    for (uint32_t y = 0; y < height; ++y)
        for (uint32_t x = 0; x < width; ++x)
        {
            const auto value = pixel(x, y);
            image.Pixels.insert(image.Pixels.end(), value.begin(), value.end());
        }
    return image;
}

Image Constant(uint32_t size, Pixel pixel)
{
    return MakeImage(size, size, [&](uint32_t, uint32_t) { return pixel; });
}

uint32_t Dxgi(TextureCompression format)
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

DdsFixture EncodeDds(const TextureCookFixture& fixture, TextureCompression format, uint32_t width,
                     uint32_t height, uint32_t levels, uint32_t faces, const PixelFunction& pixel,
                     uint32_t alpha = 0)
{
    const bool small = format == TextureCompression::BC1 || format == TextureCompression::BC4 ||
                       format == TextureCompression::BC4Signed;
    auto dds =
        MakeDdsFixture("DX10", small ? 8 : 16, width, height, levels, faces, Dxgi(format), alpha);
    for (const auto& sub : dds.Payloads)
    {
        const auto image =
            MakeImage(std::max(width >> sub.Level, 1u), std::max(height >> sub.Level, 1u),
                      [&](uint32_t x, uint32_t y) { return pixel(sub.Face, sub.Level, x, y); });
        std::string error;
        const auto blocks =
            EncodeTextureBlocks(image, format, 2, 0.5,
                                fixture.Root() / std::string("fixture ") /
                                    std::to_string(sub.Face) / std::to_string(sub.Level),
                                ConfiguredTextureEncoder(), error);
        INFO(error);
        REQUIRE(blocks);
        REQUIRE(blocks->size() == sub.Size);
        dds.Bytes.replace(sub.Offset, sub.Size, reinterpret_cast<const char*>(blocks->data()),
                          blocks->size());
    }
    return dds;
}

void Destroy(ktxTexture2* texture)
{
    ktxTexture_Destroy(ktxTexture(texture));
}
using KtxOwner = std::unique_ptr<ktxTexture2, decltype(&Destroy)>;
KtxOwner Open(const fs::path& path)
{
    ktxTexture2* texture = nullptr;
    const auto status = ktxTexture2_CreateFromNamedFile(
        path.string().c_str(), KTX_TEXTURE_CREATE_LOAD_IMAGE_DATA_BIT, &texture);
    INFO(ktxErrorString(status));
    REQUIRE(status == KTX_SUCCESS);
    return {texture, &Destroy};
}

void Cooked(const TextureCookResult& result)
{
    INFO(result.Error);
    REQUIRE(result.Status == TextureCookStatus::Cooked);
}

std::span<const uint8_t> Blocks(ktxTexture2* texture, uint32_t level, uint32_t face)
{
    ktx_size_t offset = 0;
    REQUIRE(ktxTexture_GetImageOffset(ktxTexture(texture), level, 0, face, &offset) == KTX_SUCCESS);
    const auto size = ktxTexture_GetImageSize(ktxTexture(texture), level);
    REQUIRE(offset + size <= texture->dataSize);
    return {texture->pData + offset, size};
}

void Retained(ktxTexture2* texture, const DdsFixture& dds)
{
    for (const auto& sub : dds.Payloads)
    {
        if (sub.Level >= texture->numLevels)
            continue;
        const auto blocks = Blocks(texture, sub.Level, sub.Face);
        CHECK(std::string_view(reinterpret_cast<const char*>(blocks.data()), blocks.size()) ==
              std::string_view(dds.Bytes).substr(sub.Offset, sub.Size));
    }
}

Image Decode(ktxTexture2* texture, TextureCompression format, uint32_t level, uint32_t face,
             const fs::path& directory)
{
    std::string error;
    const auto image = DecodeTextureBlocks(
        Blocks(texture, level, face),
        {std::max(texture->baseWidth >> level, 1u), std::max(texture->baseHeight >> level, 1u)},
        format, directory / "decode" / std::to_string(face) / std::to_string(level),
        ConfiguredTextureEncoder(), error);
    INFO(error);
    REQUIRE(image);
    return *image;
}

std::optional<std::string> Metadata(ktxTexture2* texture, const char* key)
{
    unsigned int size = 0;
    void* value = nullptr;
    const auto status = ktxHashList_FindValue(&texture->kvDataHead, key, &size, &value);
    if (status == KTX_NOT_FOUND)
        return std::nullopt;
    REQUIRE(status == KTX_SUCCESS);
    REQUIRE(size > 0);
    return std::string(static_cast<const char*>(value), size - 1);
}

void Color(const Image& image, std::array<int, 4> expected, uint32_t channels, int tolerance = 12)
{
    for (size_t pixel = 0; pixel < image.Pixels.size(); pixel += 4)
        for (size_t c = 0; c < channels; ++c)
            CHECK(std::abs(int(image.Pixels[pixel + c]) - expected[c]) <= tolerance);
}

Pixel DirectionPixel(uint32_t face, uint32_t size, uint32_t x, uint32_t y)
{
    // Label each face with a continuous world-direction field, independently
    // authored in DDS order. Rotated or mirrored seams produce large errors.
    const double s = (double(x) + 0.5) * 2 / size - 1;
    const double t = (double(y) + 0.5) * 2 / size - 1;
    const std::array<std::array<double, 3>, 6> axes{
        {{1, -t, -s}, {-1, -t, s}, {s, 1, t}, {s, -1, -t}, {s, -t, 1}, {-s, -t, -1}}};
    const auto& vector = axes[face];
    const double length =
        std::sqrt(vector[0] * vector[0] + vector[1] * vector[1] + vector[2] * vector[2]);
    Pixel result{0, 0, 0, 255};
    for (size_t channel = 0; channel < 3; ++channel)
        result[channel] =
            static_cast<uint8_t>(std::lround((vector[channel] / length * 0.5 + 0.5) * 255));
    return result;
}

Pixel NormalizedDirection(std::span<const uint8_t> pixel)
{
    std::array<double, 3> direction{pixel[0] / 127.5 - 1, pixel[1] / 127.5 - 1,
                                    pixel[2] / 127.5 - 1};
    const double length = std::sqrt(direction[0] * direction[0] + direction[1] * direction[1] +
                                    direction[2] * direction[2]);
    REQUIRE(length > 0.05);
    Pixel result{0, 0, 0, 255};
    for (size_t channel = 0; channel < 3; ++channel)
        result[channel] =
            static_cast<uint8_t>(std::lround((direction[channel] / length * 0.5 + 0.5) * 255));
    return result;
}
} // namespace

TEST_CASE("Native DDS completion retains all supplied blocks and generates every BC format",
          "[asset][dds-completion]")
{
    TextureCookFixture fixture;
    for (const auto format :
         {TextureCompression::BC1, TextureCompression::BC2, TextureCompression::BC3,
          TextureCompression::BC4, TextureCompression::BC4Signed, TextureCompression::BC5,
          TextureCompression::BC5Signed, TextureCompression::BC7})
    {
        INFO(ToString(format));
        const auto dds =
            EncodeDds(fixture, format, 7, 5, 2, 1, [](uint32_t, uint32_t level, uint32_t, uint32_t)
                      { return Pixel{uint8_t(level ? 64 : 190), 192, 128, 160}; });
        TextureCookFixture::WriteText(fixture.Source("partial.dds"), dds.Bytes);
        fs::remove(fixture.Source("partial.dds.json"));
        TextureCookRequest request{.Source = "partial.dds", .EncoderThreads = 2};
        request.Overrides.Usage = TextureUsage::Packed;
        const auto result = fixture.Cooker().Cook(request);
        Cooked(result);
        const auto texture = Open(result.Output);
        CHECK(texture->numLevels == 3);
        CHECK(texture->numFaces == 1);
        Retained(texture.get(), dds);
        const auto decoded = Decode(texture.get(), format, 2, 0, fixture.Root());
        Color(decoded, {64, 192, 128, format == TextureCompression::BC1 ? 255 : 160},
              format == TextureCompression::BC4 || format == TextureCompression::BC4Signed   ? 1u
              : format == TextureCompression::BC5 || format == TextureCompression::BC5Signed ? 2u
                                                                                             : 4u);
        if (format == TextureCompression::BC4Signed || format == TextureCompression::BC5Signed)
        {
            const auto blocks = Blocks(texture.get(), 2, 0);
            CHECK(blocks[0] >= 128);
            CHECK(blocks[1] >= 128);
            if (format == TextureCompression::BC5Signed)
                CHECK(blocks[8] < 128);
            CHECK(texture->vkFormat == uint32_t(format == TextureCompression::BC4Signed
                                                    ? VK_FORMAT_BC4_SNORM_BLOCK
                                                    : VK_FORMAT_BC5_SNORM_BLOCK));
        }
        request.Overrides = {};
        CHECK(fixture.Cooker().Cook(request).Status == TextureCookStatus::Skipped);
    }
}

TEST_CASE("Unknown DDS alpha completion warns about coverage and filters sRGB in linear light",
          "[asset][dds-completion]")
{
    TextureCookFixture fixture;
    auto dds = EncodeDds(fixture, TextureCompression::BC7, 4, 4, 1, 1,
                         [](uint32_t, uint32_t, uint32_t x, uint32_t)
                         { return x % 2 ? Pixel{255, 255, 255, 255} : Pixel{0, 0, 0, 0}; });
    dds.SetWord(128, 99);
    TextureCookFixture::WriteText(fixture.Source("foliage.dds"), dds.Bytes);
    const auto result = fixture.Cooker().Cook({.Source = "foliage.dds", .EncoderThreads = 2});
    Cooked(result);
    bool warned = false;
    for (const auto& warning : result.Warnings)
        warned |= warning.find("foliage.dds.json") != std::string::npos &&
                  warning.find("does not preserve alpha-test coverage") != std::string::npos;
    CHECK(warned);
    const auto texture = Open(result.Output);
    CHECK(texture->vkFormat == VK_FORMAT_BC7_SRGB_BLOCK);
    Retained(texture.get(), dds);
    for (uint32_t level : {1u, 2u})
        Color(Decode(texture.get(), TextureCompression::BC7, level, 0, fixture.Root()),
              {188, 188, 188, 128}, 4, 5);
    CHECK_FALSE(Metadata(texture.get(), "HikariDdsAlphaMode"));
    TextureCookRequest mask{.Source = "foliage.dds"};
    mask.Overrides.AlphaMode = TextureAlphaMode::Mask;
    const auto before = TextureCookFixture::ReadText(result.Output);
    const auto rejected = fixture.Cooker().Cook(mask);
    CHECK(rejected.Status == TextureCookStatus::Failed);
    CHECK(rejected.Error.find("--no-mips") != std::string::npos);
    CHECK(TextureCookFixture::ReadText(result.Output) == before);
}

TEST_CASE("Signed BC5 normals complete and convert with positive Z and persistent Y flips",
          "[asset][dds-completion]")
{
    TextureCookFixture fixture;
    const auto dds =
        EncodeDds(fixture, TextureCompression::BC5Signed, 4, 4, 1, 1,
                  [](uint32_t, uint32_t, uint32_t, uint32_t) { return Pixel{64, 192, 128, 255}; });
    TextureCookFixture::WriteText(fixture.Source("normal.dds"), dds.Bytes);
    TextureCookRequest request{.Source = "normal.dds", .EncoderThreads = 2};
    request.Overrides.Usage = TextureUsage::Normal;
    const auto native = fixture.Cooker().Cook(request);
    Cooked(native);
    {
        const auto texture = Open(native.Output);
        Retained(texture.get(), dds);
        CHECK(Metadata(texture.get(), "HikariNormalEncoding") == "xy-snorm-positive-z");
        Color(Decode(texture.get(), TextureCompression::BC5Signed, 2, 0, fixture.Root()),
              {64, 192, 0, 255}, 2, 5);
    }
    request.Overrides.bFlipNormalY = true;
    const auto flipped = fixture.Cooker().Cook(request);
    Cooked(flipped);
    {
        const auto texture = Open(flipped.Output);
        for (uint32_t level = 0; level < 3; ++level)
            Color(Decode(texture.get(), TextureCompression::BC5Signed, level, 0, fixture.Root()),
                  {64, 63, 0, 255}, 2, 5);
    }
    request.Overrides = {};
    CHECK(fixture.Cooker().Cook(request).Status == TextureCookStatus::Skipped);
    request.Overrides.Format = TextureCompression::BC7;
    const auto converted = fixture.Cooker().Cook(request);
    Cooked(converted);
    const auto texture = Open(converted.Output);
    CHECK(texture->vkFormat == VK_FORMAT_BC7_UNORM_BLOCK);
    CHECK(Metadata(texture.get(), "HikariNormalEncoding") == "xyz-unorm");
    for (uint32_t level = 0; level < 3; ++level)
        Color(Decode(texture.get(), TextureCompression::BC7, level, 0, fixture.Root()),
              {64, 63, 217, 255}, 4, 6);
    bool warned = false;
    for (const auto& warning : converted.Warnings)
        warned |= warning.find("byte preservation no longer applies") != std::string::npos;
    CHECK(warned);
}

TEST_CASE("Cubemap mip kernels read every neighbouring edge and all three corner faces",
          "[asset][dds-completion][cube-filter]")
{
    constexpr std::array<std::array<size_t, 4>, 6> neighbours{
        {{4, 5, 2, 3}, {5, 4, 2, 3}, {1, 0, 5, 4}, {1, 0, 4, 5}, {1, 0, 2, 3}, {0, 1, 2, 3}}};
    std::string error;
    for (size_t face = 0; face < 6; ++face)
        for (size_t edge = 0; edge < 4; ++edge)
        {
            std::array<Image, 6> source;
            for (auto& image : source)
                image = Constant(16, {0, 0, 0, 255});
            source[neighbours[face][edge]] = Constant(16, {255, 0, 0, 255});
            const auto filtered =
                FilterCubemapMip(source, TextureColorSpace::Linear, TextureUsage::Packed, error);
            REQUIRE(filtered);
            const std::array<std::array<uint32_t, 2>, 4> points{{{0, 3}, {7, 3}, {3, 0}, {3, 7}}};
            for (size_t sampled = 0; sampled < 4; ++sampled)
            {
                const auto [x, y] = points[sampled];
                const auto value = (*filtered)[face].Pixels[(size_t(y) * 8 + x) * 4];
                if (sampled == edge)
                    CHECK(value > 0);
                else
                    CHECK(value == 0);
            }
        }
    std::array<Image, 6> source;
    for (auto& image : source)
        image = Constant(16, {0, 0, 0, 255});
    source[0] = Constant(16, {255, 0, 0, 255});
    source[2] = Constant(16, {0, 255, 0, 255});
    source[4] = Constant(16, {0, 0, 255, 255});
    const auto filtered =
        FilterCubemapMip(source, TextureColorSpace::Linear, TextureUsage::Packed, error);
    REQUIRE(filtered);
    // Tent edge mass is 1/8 per axis; the 1/64 corner mass is shared three ways.
    CHECK((*filtered)[0].Pixels[0] == 197);
    CHECK((*filtered)[0].Pixels[1] == 29);
    CHECK((*filtered)[0].Pixels[2] == 29);
    source[5].Pixels.pop_back();
    CHECK_FALSE(FilterCubemapMip(source, TextureColorSpace::Linear, TextureUsage::Packed, error));
}

TEST_CASE("Directional cubemap mip chains preserve face orientation across every edge and corner",
          "[asset][dds-completion][cube-filter]")
{
    std::array<Image, 6> source;
    for (uint32_t face = 0; face < 6; ++face)
        source[face] = MakeImage(17, 17, [&](uint32_t x, uint32_t y)
                                 { return DirectionPixel(face, 17, x, y); });
    std::string error;
    for (uint32_t level = 1; level < 5; ++level)
    {
        const auto filtered =
            FilterCubemapMip(source, TextureColorSpace::Linear, TextureUsage::Packed, error);
        REQUIRE(filtered);
        const auto size = (*filtered)[0].Extent.Width;
        for (uint32_t face = 0; face < 6; ++face)
            for (uint32_t y = 0; y < size; ++y)
                for (uint32_t x = 0; x < size; ++x)
                {
                    const auto expected = DirectionPixel(face, size, x, y);
                    // Averaging attenuates the field's magnitude. Orientation
                    // is its normalized direction, including the final 1x1 mip.
                    const auto actual = NormalizedDirection(
                        std::span((*filtered)[face].Pixels).subspan((size_t(y) * size + x) * 4, 4));
                    INFO("level " << level << " face " << face << " size " << size << " x " << x
                                  << " y " << y);
                    for (size_t channel = 0; channel < 3; ++channel)
                        CHECK(std::abs(int(actual[channel]) - expected[channel]) <=
                              (size == 1 ? 40 : 20));
                }
        source = *filtered;
    }
}

TEST_CASE("DDS cubemap completion retains each face and uses cross-face generated levels",
          "[asset][dds-completion]")
{
    TextureCookFixture fixture;
    const auto dds = EncodeDds(fixture, TextureCompression::BC7, 8, 8, 2, 6,
                               [](uint32_t face, uint32_t level, uint32_t x, uint32_t y)
                               { return DirectionPixel(face, 8u >> level, x, y); });
    TextureCookFixture::WriteText(fixture.Source("cube.dds"), dds.Bytes);
    TextureCookRequest request{.Source = "cube.dds", .EncoderThreads = 2};
    request.Overrides.Usage = TextureUsage::Packed;
    const auto result = fixture.Cooker().Cook(request);
    Cooked(result);
    const auto texture = Open(result.Output);
    CHECK(texture->numFaces == 6);
    CHECK(texture->numLevels == 4);
    CHECK(Metadata(texture.get(), KTX_ORIENTATION_KEY) == "rd");
    Retained(texture.get(), dds);
    for (uint32_t face = 0; face < 6; ++face)
    {
        const auto image = Decode(texture.get(), TextureCompression::BC7, 2, face, fixture.Root());
        for (uint32_t y = 0; y < 2; ++y)
            for (uint32_t x = 0; x < 2; ++x)
            {
                const auto expected = DirectionPixel(face, 2, x, y);
                INFO("face " << face << " x " << x << " y " << y << " actual "
                             << int(image.Pixels[(size_t(y) * 2 + x) * 4]) << ","
                             << int(image.Pixels[(size_t(y) * 2 + x) * 4 + 1]) << ","
                             << int(image.Pixels[(size_t(y) * 2 + x) * 4 + 2]));
                for (size_t c = 0; c < 3; ++c)
                    CHECK(std::abs(int(image.Pixels[(size_t(y) * 2 + x) * 4 + c]) - expected[c]) <=
                          20);
            }
    }
}

TEST_CASE("Partial BC blocks preserve distinct rows without changing logical dimensions",
          "[asset][dds-completion]")
{
    TextureCookFixture fixture;
    for (const auto extent : {Hikari::Core::Extent2D{2, 2}, {3, 1}, {1, 3}, {7, 5}})
        for (const auto format :
             {TextureCompression::BC3, TextureCompression::BC5, TextureCompression::BC7})
        {
            const auto image =
                MakeImage(extent.Width, extent.Height, [](uint32_t, uint32_t y)
                          { return y % 2 ? Pixel{192, 64, 128, 255} : Pixel{64, 192, 128, 255}; });
            std::string error;
            const auto blocks = EncodeTextureBlocks(image, format, 2, 0.5, fixture.Root() / "codec",
                                                    ConfiguredTextureEncoder(), error);
            INFO(error);
            REQUIRE(blocks);
            CHECK(blocks->size() ==
                  size_t((extent.Width + 3) / 4) * ((extent.Height + 3) / 4) * 16);
            const auto decoded =
                DecodeTextureBlocks(*blocks, extent, format, fixture.Root() / "codec",
                                    ConfiguredTextureEncoder(), error);
            INFO(error);
            REQUIRE(decoded);
            CHECK(decoded->Extent == extent);
            for (uint32_t y = 0; y < extent.Height; ++y)
                for (uint32_t x = 0; x < extent.Width; ++x)
                {
                    const auto offset = (size_t(y) * extent.Width + x) * 4;
                    CHECK(std::abs(int(decoded->Pixels[offset]) - (y % 2 ? 192 : 64)) <= 6);
                    CHECK(std::abs(int(decoded->Pixels[offset + 1]) - (y % 2 ? 64 : 192)) <= 6);
                }
        }
}

TEST_CASE("Native normal swizzles survive completion and become canonical after conversion",
          "[asset][dds-completion]")
{
    TextureCookFixture fixture;
    for (const bool bAlphaX : {false, true})
    {
        const auto format = bAlphaX ? TextureCompression::BC3 : TextureCompression::BC5;
        auto dds =
            EncodeDds(fixture, format, 4, 4, 1, 1, [&](uint32_t, uint32_t, uint32_t, uint32_t)
                      { return bAlphaX ? Pixel{0, 192, 0, 64} : Pixel{192, 64, 0, 255}; });
        dds.Bytes.erase(128, 20);
        for (auto& sub : dds.Payloads)
            sub.Offset -= 20;
        dds.Bytes.replace(84, 4, bAlphaX ? "DXT5" : "ATI2");
        dds.SetWord(80, 0x80000004u);
        dds.SetWord(88, bAlphaX ? 0x35443241u : 0x59583241u);
        TextureCookFixture::WriteText(fixture.Source("normal.dds"), dds.Bytes);
        fs::remove(fixture.Source("normal.dds.json"));
        TextureCookRequest request{.Source = "normal.dds", .EncoderThreads = 2};
        request.Overrides.Usage = TextureUsage::Normal;
        const auto completed = fixture.Cooker().Cook(request);
        Cooked(completed);
        {
            const auto texture = Open(completed.Output);
            Retained(texture.get(), dds);
            CHECK(Metadata(texture.get(), "HikariNormalEncoding") ==
                  (bAlphaX ? "ag-unorm-positive-z" : "yx-unorm-positive-z"));
            Color(Decode(texture.get(), format, 2, 0, fixture.Root()),
                  bAlphaX ? std::array<int, 4>{0, 192, 0, 64} : std::array<int, 4>{192, 64, 0, 255},
                  bAlphaX ? 4u : 2u, 6);
        }
        for (const auto& warning : completed.Warnings)
            CHECK(warning.find("does not preserve alpha-test coverage") == std::string::npos);
        request.Overrides.Format = TextureCompression::BC7;
        const auto converted = fixture.Cooker().Cook(request);
        Cooked(converted);
        const auto texture = Open(converted.Output);
        CHECK(Metadata(texture.get(), "HikariNormalEncoding") == "xyz-unorm");
        CHECK_FALSE(Metadata(texture.get(), "HikariDdsSwizzle"));
        for (uint32_t level = 0; level < 3; ++level)
            Color(Decode(texture.get(), TextureCompression::BC7, level, 0, fixture.Root()),
                  {64, 192, 217, 255}, 4, 6);
    }
}

TEST_CASE("DDS decoder and encoder failures retain the last published asset and record",
          "[asset][dds-completion]")
{
    TextureCookFixture fixture;
    const auto dds =
        EncodeDds(fixture, TextureCompression::BC7, 4, 4, 1, 1,
                  [](uint32_t, uint32_t, uint32_t, uint32_t) { return Pixel{60, 100, 180, 255}; });
    TextureCookFixture::WriteText(fixture.Source("partial.dds"), dds.Bytes);
    TextureCookRequest request{.Source = "partial.dds", .EncoderThreads = 2};
    const auto success = fixture.Cooker().Cook(request);
    Cooked(success);
    const auto output = TextureCookFixture::ReadText(success.Output);
    const auto record = TextureCookFixture::ReadText(fixture.Cooked() / "partial.dds.cook.json");
    request.bForce = true;
    fs::path executable;
    SECTION("decoder unavailable")
    {
        executable = fixture.Root() / "missing tool";
    }
    SECTION("decoder failed")
    {
        executable = HIKARI_COOK_PROCESS_FIXTURE;
    }
    TextureCookEncoder encoder;
    encoder.Encode = [&](const auto& prepared, const auto& temporary, auto& error)
    { return EncodeTexturePixels(prepared, temporary, error, executable); };
    const auto failed = fixture.Cooker().Cook(request, encoder);
    CHECK(failed.Status == TextureCookStatus::Failed);
    CHECK_FALSE(failed.Error.empty());
    CHECK(TextureCookFixture::ReadText(success.Output) == output);
    CHECK(TextureCookFixture::ReadText(fixture.Cooked() / "partial.dds.cook.json") == record);
    CHECK_FALSE(fs::exists(fixture.Cooked() / "partial.dds.ktx2.transaction"));
}
