#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <filesystem>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include <ktx.h>
#include <vulkan/vulkan_core.h>

#include <asset/DdsReader.h>
#include <asset/ImageWriter.h>
#include <asset/TextureCooker.h>

#include "CookProcess.h"
#include "PixelDdsFixture.h"
#include "PixelTextureEncoder.h"
#include "TextureCookFixture.h"
#include "TexturePixelFilter.h"

using namespace Hikari::Asset;
namespace fs = std::filesystem;

namespace
{
void Destroy(ktxTexture2* texture)
{
    ktxTexture_Destroy(ktxTexture(texture));
}
using KtxOwner = std::unique_ptr<ktxTexture2, decltype(&Destroy)>;
KtxOwner Open(const fs::path& path)
{
    ktxTexture2* texture = nullptr;
    const auto code = ktxTexture2_CreateFromNamedFile(
        path.string().c_str(), KTX_TEXTURE_CREATE_LOAD_IMAGE_DATA_BIT, &texture);
    INFO(ktxErrorString(code));
    REQUIRE(code == KTX_SUCCESS);
    return {texture, &Destroy};
}
void RequireCooked(const TextureCookResult& result)
{
    INFO(result.Error);
    REQUIRE(result.Status == TextureCookStatus::Cooked);
}
Image Constant(uint32_t w, uint32_t h, std::array<uint8_t, 4> color)
{
    Image image{{}, {w, h}, 4};
    for (uint32_t i = 0; i < w * h; ++i)
        image.Pixels.insert(image.Pixels.end(), color.begin(), color.end());
    return image;
}
Image Decode(ktxTexture2* texture, uint32_t level, uint32_t face, const fs::path& directory)
{
    const uint32_t width = std::max(texture->baseWidth >> level, 1u);
    const uint32_t height = std::max(texture->baseHeight >> level, 1u);
    const bool bc1 = texture->vkFormat == VK_FORMAT_BC1_RGBA_UNORM_BLOCK ||
                     texture->vkFormat == VK_FORMAT_BC1_RGBA_SRGB_BLOCK;
    const bool bc4 = texture->vkFormat == VK_FORMAT_BC4_UNORM_BLOCK;
    const bool bc5 = texture->vkFormat == VK_FORMAT_BC5_UNORM_BLOCK;
    auto dds = MakeDdsFixture(bc1   ? "DXT1"
                              : bc4 ? "ATI1"
                              : bc5 ? "ATI2"
                                    : "DX10",
                              bc1 || bc4 ? 8 : 16, width, height, 1, 1, 98);
    ktx_size_t offset = 0;
    REQUIRE(ktxTexture_GetImageOffset(ktxTexture(texture), level, 0, face, &offset) == KTX_SUCCESS);
    dds.Bytes.replace(dds.Payloads[0].Offset, dds.Payloads[0].Size,
                      reinterpret_cast<const char*>(texture->pData + offset), dds.Payloads[0].Size);
    const auto input = directory / "decode-input.dds", output = directory / "decode-output.dds";
    TextureCookFixture::WriteText(input, dds.Bytes);
    std::error_code ec;
    fs::remove(output, ec);
    std::string error;
    REQUIRE(
        RunCookProcess(ConfiguredTextureEncoder(),
                       {"-fd", "ARGB_8888", "-nomipmap", CookPathUtf8(input), CookPathUtf8(output)},
                       directory / "decode.log", error));
    const auto info = ReadDdsInfo(output, error);
    INFO(error);
    REQUIRE(info);
    const auto image = ReadDdsPixels(output, *info, 0, 0, error);
    INFO(error);
    REQUIRE(image);
    return *image;
}
void CheckColor(const Image& image, std::array<int, 4> color, int channels = 4, int tolerance = 5)
{
    for (size_t pixel = 0; pixel < image.Pixels.size(); pixel += 4)
        for (int channel = 0; channel < channels; ++channel)
            CHECK(std::abs(int(image.Pixels[pixel + channel]) - color[channel]) <= tolerance);
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
    REQUIRE(static_cast<const char*>(value)[size - 1] == '\0');
    return std::string(static_cast<const char*>(value), size - 1);
}

void CheckNormalDirection(const Image& image, std::array<double, 3> expected, bool bXY)
{
    for (size_t pixel = 0; pixel < image.Pixels.size(); pixel += 4)
    {
        const double x = image.Pixels[pixel] / 127.5 - 1;
        const double y = image.Pixels[pixel + 1] / 127.5 - 1;
        const double z =
            bXY ? std::sqrt(std::max(0.0, 1 - x * x - y * y)) : image.Pixels[pixel + 2] / 127.5 - 1;
        CHECK(std::abs(x - expected[0]) < 0.06);
        CHECK(std::abs(y - expected[1]) < 0.06);
        CHECK(std::abs(z - expected[2]) < 0.06);
        CHECK(std::abs(std::sqrt(x * x + y * y + z * z) - 1) < 0.06);
    }
}
} // namespace

TEST_CASE("Normal mips average signed vectors before normalization and retain numeric alpha",
          "[asset][pixel-cook][normal-cook]")
{
    auto image = Constant(2, 1, {255, 128, 128, 0});
    image.Pixels[4] = 128;
    image.Pixels[6] = 191;
    image.Pixels[7] = 255;
    std::string error;
    const auto mip =
        FilterTextureMip(image, TextureColorSpace::Linear, error, TextureUsage::Normal);
    REQUIRE(mip);
    // The shorter second source vector keeps its authored weight until averaging.
    CheckColor(*mip, {242, 128, 184, 128}, 4, 1);
    CheckNormalDirection(*mip, {0.895, 0.007, 0.446}, false);
    const auto data = FilterTextureMip(image, TextureColorSpace::Linear, error);
    REQUIRE(data);
    CHECK(data->Pixels == std::vector<uint8_t>{192, 128, 160, 128});
    CHECK_FALSE(FilterTextureMip(image, TextureColorSpace::Srgb, error, TextureUsage::Normal));
    image.SourceChannels = 2;
    CHECK_FALSE(FilterTextureMip(image, TextureColorSpace::Linear, error, TextureUsage::Normal));
    image.SourceChannels = 4;
    image.Pixels.pop_back();
    CHECK_FALSE(FilterTextureMip(image, TextureColorSpace::Linear, error, TextureUsage::Normal));
}

TEST_CASE("Cancelled normals use a flat fallback and odd normal footprints retain edge vectors",
          "[asset][pixel-cook][normal-cook]")
{
    auto image = Constant(2, 1, {255, 128, 128, 60});
    image.Pixels[4] = 0;
    image.Pixels[5] = image.Pixels[6] = 127;
    image.Pixels[7] = 180;
    std::string error;
    const auto cancelled =
        FilterTextureMip(image, TextureColorSpace::Linear, error, TextureUsage::Normal);
    REQUIRE(cancelled);
    CHECK(cancelled->Pixels == std::vector<uint8_t>{128, 128, 255, 120});
    for (const uint8_t center : {uint8_t(127), uint8_t(128)})
    {
        const auto zero = FilterTextureMip(Constant(2, 2, {center, center, center, 73}),
                                           TextureColorSpace::Linear, error, TextureUsage::Normal);
        REQUIRE(zero);
        CHECK(zero->Pixels == std::vector<uint8_t>{128, 128, 255, 73});
    }
    for (const auto extent : {Hikari::Core::Extent2D{3, 1}, {1, 3}, {3, 3}, {5, 3}})
    {
        auto odd = Constant(extent.Width, extent.Height, {128, 128, 255, 255});
        for (uint32_t y = 0; y < extent.Height; ++y)
            for (uint32_t x = 0; x < extent.Width; ++x)
                if (x == extent.Width - 1 || y == extent.Height - 1)
                {
                    const auto offset = (size_t(y) * extent.Width + x) * 4;
                    odd.Pixels[offset] = 255;
                    odd.Pixels[offset + 2] = 128;
                }
        const auto mip =
            FilterTextureMip(odd, TextureColorSpace::Linear, error, TextureUsage::Normal);
        REQUIRE(mip);
        CHECK(mip->Pixels[0] > 128);
        CHECK(mip->Pixels[3] == 255);
        for (size_t i = 0; i < mip->Pixels.size(); i += 4)
        {
            double lengthSquared = 0;
            for (size_t c = 0; c < 3; ++c)
            {
                const double value = mip->Pixels[i + c] / 127.5 - 1;
                lengthSquared += value * value;
            }
            CHECK(std::abs(lengthSquared - 1) < 0.015);
        }
    }
}

TEST_CASE("Real BC5 and BC7 encoders retain normal directions and decoding metadata",
          "[asset][pixel-cook][normal-cook]")
{
    TextureCookFixture fixture;
    for (const auto pixels : {std::array<uint8_t, 4>{128, 128, 255, 73}, {204, 204, 195, 73}})
        for (const auto format : {TextureCompression::BC5, TextureCompression::BC7})
            for (const bool bFlip : {false, true})
            {
                INFO("format " << ToString(format) << " flip " << bFlip);
                const auto image = Constant(7, 5, pixels);
                REQUIRE(
                    WritePng(image.Pixels, image.Extent, fixture.Source("normal.png").string()));
                fs::remove(fixture.Source("normal.png.texture.json"));
                TextureCookRequest request{.Source = "normal.png", .EncoderThreads = 2};
                request.Overrides.Usage = TextureUsage::Normal;
                request.Overrides.Format = format;
                request.Overrides.bFlipNormalY = bFlip;
                const auto result = fixture.Cooker().Cook(request);
                RequireCooked(result);
                const auto texture = Open(result.Output);
                CHECK(texture->vkFormat == uint32_t(format == TextureCompression::BC5
                                                        ? VK_FORMAT_BC5_UNORM_BLOCK
                                                        : VK_FORMAT_BC7_UNORM_BLOCK));
                CHECK(texture->numLevels == 3);
                CHECK(Metadata(texture.get(), "HikariTextureUsage") == "normal");
                CHECK(Metadata(texture.get(), "HikariNormalEncoding") ==
                      (format == TextureCompression::BC5 ? "xy-unorm-positive-z" : "xyz-unorm"));
                for (uint32_t level = 0; level < 3; ++level)
                {
                    const auto decoded = Decode(texture.get(), level, 0, fixture.Root());
                    const std::array<double, 3> expected{
                        pixels[0] / 127.5 - 1, (bFlip ? 255 - pixels[1] : pixels[1]) / 127.5 - 1,
                        pixels[2] / 127.5 - 1};
                    CheckNormalDirection(decoded, expected, format == TextureCompression::BC5);
                    if (format == TextureCompression::BC7)
                        CHECK(std::abs(int(decoded.Pixels[3]) - 73) <= 5);
                }
                request.Overrides = {};
                CHECK(fixture.Cooker().Cook(request).Status == TextureCookStatus::Skipped);
            }
}

TEST_CASE("Normal XY packing preserves non-unit directions and handles quantized zero",
          "[asset][pixel-cook][normal-cook]")
{
    TextureCookFixture fixture;
    const auto image = Constant(4, 4, {166, 166, 217, 73});
    std::string error;
    const auto prepared = PrepareNormalTextureXY(image, error);
    REQUIRE(prepared);
    CheckNormalDirection(*prepared, {0.368, 0.368, 0.854}, false);
    CHECK(prepared->Pixels[3] == 73);
    CHECK(image.Pixels[0] == 166);
    const auto zero = PrepareNormalTextureXY(Constant(1, 1, {128, 128, 128, 73}), error);
    REQUIRE(zero);
    CHECK(zero->Pixels == std::vector<uint8_t>{128, 128, 255, 73});
    auto invalid = image;
    invalid.SourceChannels = 2;
    CHECK_FALSE(PrepareNormalTextureXY(invalid, error));
    invalid.SourceChannels = 4;
    invalid.Pixels.pop_back();
    CHECK_FALSE(PrepareNormalTextureXY(invalid, error));
    REQUIRE(WritePng(image.Pixels, image.Extent, fixture.Source("normal.png").string()));
    TextureCookRequest request{.Source = "normal.png", .EncoderThreads = 2};
    request.Overrides.Usage = TextureUsage::Normal;
    request.Overrides.bNoMips = true;
    const auto result = fixture.Cooker().Cook(request);
    RequireCooked(result);
    const auto texture = Open(result.Output);
    CheckNormalDirection(Decode(texture.get(), 0, 0, fixture.Root()), {0.368, 0.368, 0.854}, true);
    request.Overrides.Format = TextureCompression::BC7;
    const auto xyz = fixture.Cooker().Cook(request);
    RequireCooked(xyz);
    const auto rgb = Open(xyz.Output);
    CheckColor(Decode(rgb.get(), 0, 0, fixture.Root()), {166, 166, 217, 73});
}

TEST_CASE("Unsupported negative-Z normal pixels retain the last successful asset",
          "[asset][pixel-cook][normal-cook]")
{
    TextureCookFixture fixture;
    const auto flat = Constant(4, 4, {128, 128, 255, 255});
    REQUIRE(WritePng(flat.Pixels, flat.Extent, fixture.Source("normal.png").string()));
    for (const auto format : {TextureCompression::BC5, TextureCompression::BC7})
    {
        REQUIRE(WritePng(flat.Pixels, flat.Extent, fixture.Source("normal.png").string()));
        TextureCookRequest request{.Source = "normal.png", .EncoderThreads = 2};
        request.Overrides.Usage = TextureUsage::Normal;
        request.Overrides.Format = format;
        const auto result = fixture.Cooker().Cook(request);
        RequireCooked(result);
        const auto output = TextureCookFixture::ReadText(result.Output);
        const auto record = TextureCookFixture::ReadText(fixture.Cooked() / "normal.png.cook.json");
        const auto negative = Constant(4, 4, {128, 128, 0, 255});
        REQUIRE(WritePng(negative.Pixels, negative.Extent, fixture.Source("normal.png").string()));
        const auto failed = fixture.Cooker().Cook(request);
        CHECK(failed.Status == TextureCookStatus::Failed);
        CHECK(failed.Error.find("positive-Z") != std::string::npos);
        CHECK(TextureCookFixture::ReadText(result.Output) == output);
        CHECK(TextureCookFixture::ReadText(fixture.Cooked() / "normal.png.cook.json") == record);
        CHECK_FALSE(fs::exists(fixture.Cooked() / "normal.png.ktx2.transaction"));
        std::string error;
        CHECK_FALSE(PrepareNormalTextureXY(negative, error));
    }
}

TEST_CASE("Generated normal mips retain a saved Y flip exactly once and settings edits recook",
          "[asset][pixel-cook][normal-cook]")
{
    TextureCookFixture fixture;
    auto image = Constant(4, 4, {230, 128, 204, 0});
    for (size_t i = 0; i < image.Pixels.size(); i += 4)
        if ((i / 4) % 2)
        {
            image.Pixels[i] = 128;
            image.Pixels[i + 1] = 230;
            image.Pixels[i + 3] = 255;
        }
    REQUIRE(WritePng(image.Pixels, image.Extent, fixture.Source("normal.png").string()));
    for (const auto format : {TextureCompression::BC5, TextureCompression::BC7})
    {
        fs::remove(fixture.Source("normal.png.texture.json"));
        TextureCookRequest request{.Source = "normal.png", .EncoderThreads = 2};
        request.Overrides.Usage = TextureUsage::Normal;
        request.Overrides.Format = format;
        request.Overrides.bFlipNormalY = true;
        const auto result = fixture.Cooker().Cook(request);
        RequireCooked(result);
        const auto texture = Open(result.Output);
        for (uint32_t level : {1u, 2u})
            CheckNormalDirection(Decode(texture.get(), level, 0, fixture.Root()),
                                 {0.487, -0.487, 0.725}, format == TextureCompression::BC5);
        request.Overrides = {};
        CHECK(fixture.Cooker().Cook(request).Status == TextureCookStatus::Skipped);
        std::string error;
        auto settings = ParseTextureSettings(
            TextureCookFixture::ReadText(fixture.Source("normal.png.texture.json")),
            "normal.png.texture.json", error);
        REQUIRE(settings);
        REQUIRE(settings->bFlipNormalY);
        settings->bFlipNormalY = false;
        const auto json = SerializeTextureSettings(*settings, "normal.png.texture.json", error);
        REQUIRE(json);
        TextureCookFixture::WriteText(fixture.Source("normal.png.texture.json"), *json);
        const auto unflipped = fixture.Cooker().Cook(request);
        RequireCooked(unflipped);
        const auto updated = Open(unflipped.Output);
        CheckNormalDirection(Decode(updated.get(), 2, 0, fixture.Root()), {0.487, 0.487, 0.725},
                             format == TextureCompression::BC5);
    }
}

TEST_CASE("Pixel DDS normals retain supplied XYZ levels and generate only absent vector mips",
          "[asset][pixel-cook][normal-cook]")
{
    TextureCookFixture fixture;
    const auto pixel = [](uint32_t, uint32_t level, uint32_t, uint32_t)
    {
        return level ? std::array<uint8_t, 4>{128, 204, 230, 75}
                     : std::array<uint8_t, 4>{204, 204, 195, 75};
    };
    auto dds = MakePixelDdsFixture(DdsPixelLayout::RGBA8, 8, 4, 2, 1, true, 4, pixel);
    dds.SetWord(80, 0x80000041u);
    TextureCookFixture::WriteText(fixture.Source("normal.dds"), dds.Bytes);
    TextureCookRequest request{.Source = "normal.dds", .EncoderThreads = 2};
    request.Overrides.Usage = TextureUsage::Normal;
    request.Overrides.Format = TextureCompression::BC7;
    request.Overrides.bFlipNormalY = true;
    const auto result = fixture.Cooker().Cook(request);
    RequireCooked(result);
    const auto texture = Open(result.Output);
    CHECK(texture->numLevels == 4);
    CHECK(Metadata(texture.get(), "HikariNormalEncoding") == "xyz-unorm");
    CHECK(Metadata(texture.get(), "HikariDdsNormalMap") == "true");
    CheckColor(Decode(texture.get(), 0, 0, fixture.Root()), {204, 51, 195, 75});
    for (uint32_t level = 1; level < 4; ++level)
        CheckNormalDirection(Decode(texture.get(), level, 0, fixture.Root()), {0.004, -0.6, 0.804},
                             false);
}

TEST_CASE("Pixel mips filter sRGB in linear light and data numerically", "[asset][pixel-cook]")
{
    auto image = Constant(2, 2, {0, 0, 0, 0});
    for (size_t i : {size_t(4), size_t(12)})
        for (size_t c = 0; c < 4; ++c)
            image.Pixels[i + c] = 255;
    std::string error;
    const auto srgb = FilterTextureMip(image, TextureColorSpace::Srgb, error);
    REQUIRE(srgb);
    CHECK(srgb->Extent == Hikari::Core::Extent2D{1, 1});
    CHECK(srgb->Pixels == std::vector<uint8_t>{188, 188, 188, 128});
    const auto linear = FilterTextureMip(image, TextureColorSpace::Linear, error);
    REQUIRE(linear);
    CHECK(linear->Pixels == std::vector<uint8_t>{128, 128, 128, 128});
    image.Pixels.pop_back();
    CHECK_FALSE(FilterTextureMip(image, TextureColorSpace::Linear, error));
}

TEST_CASE("Odd mip footprints include edge pixels and degenerate rows and columns",
          "[asset][pixel-cook]")
{
    std::string error;
    for (const auto extent : {Hikari::Core::Extent2D{3, 1}, {1, 3}, {3, 3}, {5, 3}})
    {
        auto image = Constant(extent.Width, extent.Height, {0, 0, 0, 255});
        for (uint32_t y = 0; y < extent.Height; ++y)
            for (uint32_t x = 0; x < extent.Width; ++x)
                if (x == extent.Width - 1 || y == extent.Height - 1)
                    image.Pixels[(size_t(y) * extent.Width + x) * 4] = 255;
        auto mip = FilterTextureMip(image, TextureColorSpace::Linear, error);
        REQUIRE(mip);
        CHECK(mip->Pixels[0] > 0);
        CHECK(mip->Pixels[3] == 255);
    }
    auto line = Constant(3, 1, {0, 0, 0, 255});
    line.Pixels[8] = 255;
    const auto reduced = FilterTextureMip(line, TextureColorSpace::Linear, error);
    REQUIRE(reduced);
    CHECK(reduced->Pixels[0] == 85);
}

TEST_CASE("PNG pixel cooking uses real CPU encoders, correct channels and complete mip chains",
          "[asset][pixel-cook]")
{
    TextureCookFixture fixture;
    const auto pixels = Constant(7, 5, {64, 192, 128, 100});
    REQUIRE(WritePng(pixels.Pixels, pixels.Extent, fixture.Source("material.png").string()));
    for (const auto format : {TextureCompression::BC1, TextureCompression::BC4,
                              TextureCompression::BC5, TextureCompression::BC7})
    {
        TextureCookRequest request{.Source = "material.png"};
        request.Overrides.Usage = TextureUsage::Packed;
        request.Overrides.Format = format;
        request.EncoderThreads = 2;
        const auto result = fixture.Cooker().Cook(request);
        RequireCooked(result);
        const auto texture = Open(result.Output);
        CHECK(texture->baseWidth == 7);
        CHECK(texture->baseHeight == 5);
        CHECK(texture->numLevels == 3);
        CHECK(texture->numFaces == 1);
        const uint32_t expectedFormat =
            format == TextureCompression::BC1   ? VK_FORMAT_BC1_RGBA_UNORM_BLOCK
            : format == TextureCompression::BC4 ? VK_FORMAT_BC4_UNORM_BLOCK
            : format == TextureCompression::BC5 ? VK_FORMAT_BC5_UNORM_BLOCK
                                                : VK_FORMAT_BC7_UNORM_BLOCK;
        CHECK(texture->vkFormat == expectedFormat);
        for (uint32_t level = 0; level < 3; ++level)
        {
            const auto decoded = Decode(texture.get(), level, 0, fixture.Root());
            CheckColor(decoded, {64, 192, 128, format == TextureCompression::BC1 ? 255 : 100},
                       format == TextureCompression::BC4   ? 1
                       : format == TextureCompression::BC5 ? 2
                                                           : 4);
        }
        request.Overrides = {};
        CHECK(fixture.Cooker().Cook(request).Status == TextureCookStatus::Skipped);
    }
}

TEST_CASE("Encoded sRGB mips retain linear-light averages after compression", "[asset][pixel-cook]")
{
    TextureCookFixture fixture;
    auto image = Constant(4, 4, {0, 0, 0, 255});
    for (size_t i = 0; i < image.Pixels.size(); i += 4)
        if ((i / 4) % 2)
            image.Pixels[i] = image.Pixels[i + 1] = image.Pixels[i + 2] = 255;
    REQUIRE(WritePng(image.Pixels, image.Extent, fixture.Source("color.png").string()));
    const auto result = fixture.Cooker().Cook({.Source = "color.png", .EncoderThreads = 2});
    RequireCooked(result);
    const auto texture = Open(result.Output);
    CHECK(texture->vkFormat == VK_FORMAT_BC7_SRGB_BLOCK);
    for (uint32_t level : {1u, 2u})
        CheckColor(Decode(texture.get(), level, 0, fixture.Root()), {188, 188, 188, 255});
}

TEST_CASE("JPEG and grayscale scalar images cook using saved semantics", "[asset][pixel-cook]")
{
    TextureCookFixture fixture;
    fs::copy_file(HIKARI_TEXTURE_TEST_DATA_DIR "constant.jpg", fixture.Source("color.jpg"));
    auto result = fixture.Cooker().Cook({.Source = "color.jpg", .EncoderThreads = 2});
    RequireCooked(result);
    const auto color = Open(result.Output);
    CHECK(color->vkFormat == VK_FORMAT_BC7_SRGB_BLOCK);
    CheckColor(Decode(color.get(), 2, 0, fixture.Root()), {64, 192, 128, 255});
    fs::copy_file(HIKARI_TEXTURE_TEST_DATA_DIR "channels-gray.png", fixture.Source("scalar.png"));
    TextureCookRequest request{.Source = "scalar.png", .EncoderThreads = 2};
    request.Overrides.Usage = TextureUsage::Scalar;
    result = fixture.Cooker().Cook(request);
    RequireCooked(result);
    const auto scalar = Open(result.Output);
    CHECK(scalar->vkFormat == VK_FORMAT_BC4_UNORM_BLOCK);
    const auto original = ReadTextureImage(fixture.Source("scalar.png").string(), result.Error);
    REQUIRE(original);
    const auto decoded = Decode(scalar.get(), 0, 0, fixture.Root());
    REQUIRE(decoded.Extent == original->Extent);
    for (size_t i = 0; i < decoded.Pixels.size(); i += 4)
        CHECK(std::abs(int(decoded.Pixels[i]) - original->Pixels[i]) <= 5);
}

TEST_CASE("Pixel DDS decoding handles all channel orders, padding and supplied mip offsets",
          "[asset][pixel-cook]")
{
    TextureCookFixture fixture;
    for (const auto layout : {DdsPixelLayout::RGB8, DdsPixelLayout::BGR8, DdsPixelLayout::RGBA8,
                              DdsPixelLayout::BGRA8, DdsPixelLayout::RGBX8, DdsPixelLayout::BGRX8})
        for (const bool aligned : {false, true})
        {
            const auto pixel = [](uint32_t face, uint32_t level, uint32_t x, uint32_t y)
            {
                return std::array<uint8_t, 4>{uint8_t(30 + face + level * 10 + x), uint8_t(90 + y),
                                              uint8_t(150 + x + y), 210};
            };
            const auto dds = MakePixelDdsFixture(layout, 5, 3, 3, 1, aligned, 4, pixel);
            const auto path = fixture.Source("layout.dds");
            TextureCookFixture::WriteText(path, dds.Bytes);
            std::string error;
            const auto info = ReadDdsInfo(path, error);
            INFO(error);
            REQUIRE(info);
            CHECK_FALSE(info->IsCompressed());
            CHECK(info->PixelLayout == layout);
            CHECK(info->Levels == 3);
            for (uint32_t level = 0; level < 3; ++level)
            {
                const auto image = ReadDdsPixels(path, *info, level, 0, error);
                REQUIRE(image);
                for (uint32_t y = 0; y < image->Extent.Height; ++y)
                    for (uint32_t x = 0; x < image->Extent.Width; ++x)
                    {
                        auto value = pixel(0, level, x, y);
                        if (DdsPixelChannels(layout) == 3)
                            value[3] = 255;
                        const size_t offset = (size_t(y) * image->Extent.Width + x) * 4;
                        for (size_t channel = 0; channel < 4; ++channel)
                            CHECK(image->Pixels[offset + channel] == value[channel]);
                    }
            }
        }
}

TEST_CASE("Uncompressed DDS cooks supplied cubemap mips without overwriting their pixels",
          "[asset][pixel-cook]")
{
    TextureCookFixture fixture;
    const auto pixel = [](uint32_t face, uint32_t level, uint32_t, uint32_t)
    {
        return std::array<uint8_t, 4>{uint8_t(30 + face * 25), uint8_t(level ? 180 : 60), 120, 255};
    };
    TextureCookFixture::WriteText(
        fixture.Source("cube.dds"),
        MakePixelDdsFixture(DdsPixelLayout::BGRA8, 4, 4, 3, 6, false, 4, pixel, 87).Bytes);
    TextureCookRequest request{.Source = "cube.dds", .EncoderThreads = 2};
    request.Overrides.Usage = TextureUsage::Packed;
    const auto result = fixture.Cooker().Cook(request);
    RequireCooked(result);
    const auto texture = Open(result.Output);
    CHECK(texture->numFaces == 6);
    CHECK(texture->numLevels == 3);
    for (uint32_t face = 0; face < 6; ++face)
        for (uint32_t level = 0; level < 3; ++level)
            CheckColor(Decode(texture.get(), level, face, fixture.Root()),
                       {int(30 + face * 25), level ? 180 : 60, 120, 255});
}

TEST_CASE("Cubemap descriptors generate cross-face mip chains and retain base faces",
          "[asset][pixel-cook]")
{
    TextureCookFixture fixture;
    fixture.WriteCubemap();
    TextureCookRequest request{.Source = "sky/sky.cubemap.json", .EncoderThreads = 2};
    const auto complete = fixture.Cooker().Cook(request);
    RequireCooked(complete);
    const auto full = Open(complete.Output);
    CHECK(full->numLevels == 3);
    CHECK(full->numFaces == 6);
    for (uint32_t face = 0; face < 6; ++face)
        for (uint32_t level = 0; level < 3; ++level)
            CheckColor(Decode(full.get(), level, face, fixture.Root()), {255, 255, 255, 255});
    request.Overrides.bNoMips = true;
    const auto result = fixture.Cooker().Cook(request);
    RequireCooked(result);
    const auto texture = Open(result.Output);
    CHECK(texture->numLevels == 1);
    CHECK(texture->numFaces == 6);
    for (uint32_t face = 0; face < 6; ++face)
        CheckColor(Decode(texture.get(), 0, face, fixture.Root()), {255, 255, 255, 255});
}

TEST_CASE("CPU child processes preserve literal argument boundaries and report failures",
          "[asset][pixel-cook]")
{
    TextureCookFixture fixture;
    std::string error;
    const auto log = fixture.Root() / "child log.txt";
    const fs::path executable = HIKARI_COOK_PROCESS_FIXTURE;
    const std::vector<std::string> values{"two spaces", "quote\"here", "trailing\\", "$() ; | &"};
    auto args = values;
    args.insert(args.begin(), "echo");
    REQUIRE(RunCookProcess(executable, args, log, error));
    std::string expected;
    for (const auto& value : values)
        expected += value + '\n';
    CHECK(TextureCookFixture::ReadText(log) == expected);
    CHECK_FALSE(RunCookProcess(executable, {"fail"}, log, error));
    CHECK(error.find("exit 17") != std::string::npos);
    CHECK(error.find("intentional child") != std::string::npos);
    CHECK_FALSE(RunCookProcess(executable, {"fallback"}, log, error));
    CHECK(error.find("legacy path") != std::string::npos);
    CHECK(error.size() < 34000);
    CHECK_FALSE(RunCookProcess(fixture.Root() / "missing", {}, log, error));
    CHECK(error.find("Reconfigure/rebuild") != std::string::npos);
}

TEST_CASE("Pixel encoder failures retain the last successful output and record",
          "[asset][pixel-cook]")
{
    TextureCookFixture fixture;
    fixture.WriteImage("source.png");
    TextureCookRequest request{.Source = "source.png", .EncoderThreads = 2};
    const auto success = fixture.Cooker().Cook(request);
    RequireCooked(success);
    const auto output = TextureCookFixture::ReadText(success.Output);
    const auto record = TextureCookFixture::ReadText(fixture.Cooked() / "source.png.cook.json");
    request.bForce = true;
    TextureCookEncoder encoder;
    fs::path executable;
    SECTION("missing tool")
    {
        executable = fixture.Root() / "missing tool";
    }
    SECTION("failed tool")
    {
        executable = HIKARI_COOK_PROCESS_FIXTURE;
    }
    encoder.Encode = [&](const auto& prepared, const auto& temporary, auto& error)
    { return EncodeTexturePixels(prepared, temporary, error, executable); };
    const auto failed = fixture.Cooker().Cook(request, encoder);
    CHECK(failed.Status == TextureCookStatus::Failed);
    CHECK_FALSE(failed.Error.empty());
    CHECK(TextureCookFixture::ReadText(success.Output) == output);
    CHECK(TextureCookFixture::ReadText(fixture.Cooked() / "source.png.cook.json") == record);
}

TEST_CASE("Pixel DDS completes missing 2D mips using the last supplied level",
          "[asset][pixel-cook]")
{
    TextureCookFixture fixture;
    const auto pixel = [](uint32_t, uint32_t level, uint32_t, uint32_t)
    { return std::array<uint8_t, 4>{uint8_t(level ? 170 : 30), 80, 120, 255}; };
    TextureCookFixture::WriteText(
        fixture.Source("partial.dds"),
        MakePixelDdsFixture(DdsPixelLayout::RGB8, 8, 4, 2, 1, true, 4, pixel).Bytes);
    TextureCookRequest request{.Source = "partial.dds", .EncoderThreads = 2};
    request.Overrides.Usage = TextureUsage::Packed;
    const auto result = fixture.Cooker().Cook(request);
    RequireCooked(result);
    const auto texture = Open(result.Output);
    CHECK(texture->numLevels == 4);
    CheckColor(Decode(texture.get(), 0, 0, fixture.Root()), {30, 80, 120, 255});
    for (uint32_t level = 1; level < 4; ++level)
        CheckColor(Decode(texture.get(), level, 0, fixture.Root()), {170, 80, 120, 255});
}

TEST_CASE("Typed DX10 pixel DDS retains colour interpretation and explicit overrides",
          "[asset][pixel-cook]")
{
    TextureCookFixture fixture;
    const auto pixel = [](uint32_t, uint32_t, uint32_t, uint32_t)
    { return std::array<uint8_t, 4>{60, 100, 180, 255}; };
    for (uint32_t dxgi : {28u, 29u, 87u, 88u, 91u, 93u})
    {
        const auto layout = dxgi == 28 || dxgi == 29   ? DdsPixelLayout::RGBA8
                            : dxgi == 88 || dxgi == 93 ? DdsPixelLayout::BGRX8
                                                       : DdsPixelLayout::BGRA8;
        TextureCookFixture::WriteText(
            fixture.Source("typed.dds"),
            MakePixelDdsFixture(layout, 2, 2, 2, 1, false, 0, pixel, dxgi).Bytes);
        fs::remove(fixture.Source("typed.dds.texture.json"));
        const auto result = fixture.Cooker().Cook({.Source = "typed.dds", .EncoderThreads = 2});
        RequireCooked(result);
        const auto texture = Open(result.Output);
        const bool srgb = dxgi == 29 || dxgi == 91 || dxgi == 93;
        CHECK(texture->vkFormat ==
              uint32_t(srgb ? VK_FORMAT_BC7_SRGB_BLOCK : VK_FORMAT_BC7_UNORM_BLOCK));
        CheckColor(Decode(texture.get(), 1, 0, fixture.Root()), {60, 100, 180, 255});
    }
    TextureCookRequest linear{.Source = "typed.dds", .EncoderThreads = 2};
    linear.Overrides.ColorSpace = TextureColorSpace::Linear;
    const auto result = fixture.Cooker().Cook(linear);
    RequireCooked(result);
    const auto texture = Open(result.Output);
    CHECK(texture->vkFormat == VK_FORMAT_BC7_UNORM_BLOCK);
}

TEST_CASE("Malformed pixel DDS masks, precision and row layouts fail before writes",
          "[asset][pixel-cook]")
{
    TextureCookFixture fixture;
    const auto pixel = [](uint32_t, uint32_t, uint32_t, uint32_t)
    { return std::array<uint8_t, 4>{60, 100, 180, 255}; };
    auto dds = MakePixelDdsFixture(DdsPixelLayout::RGBA8, 3, 3, 1, 1, false, 4, pixel);
    SECTION("short pitch")
    {
        dds.SetWord(20, 11);
    }
    SECTION("unusual channel mask")
    {
        dds.SetWord(92, 0x3ff);
    }
    SECTION("higher precision")
    {
        dds.SetWord(88, 64);
    }
    SECTION("inconsistent alpha flags")
    {
        dds.SetWord(80, 0x40);
    }
    SECTION("truncated row padding")
    {
        dds.Bytes.pop_back();
    }
    SECTION("trailing pixels")
    {
        dds.Bytes += 'x';
    }
    TextureCookFixture::WriteText(fixture.Source("invalid.dds"), dds.Bytes);
    const auto result = fixture.Cooker().Cook({.Source = "invalid.dds"});
    CHECK(result.Status == TextureCookStatus::Failed);
    CHECK_FALSE(result.Error.empty());
    CHECK_FALSE(fs::exists(fixture.Source("invalid.dds.texture.json")));
    CHECK_FALSE(fs::exists(fixture.Cooked()));
}

TEST_CASE("Higher-precision PNG and oversized dimensions are rejected without resizing",
          "[asset][pixel-cook]")
{
    TextureCookFixture fixture;
    std::string hex;
    std::string expected;
    SECTION("16-bit PNG")
    {
        hex = "89504e470d0a1a0a0000000d49484452000000010000000110060000004f8518ca0000000e4944415478"
              "9c63103201c1ffff01075602d19914cd6f0000000049454e44ae426082";
        expected = "higher-precision/HDR";
    }
    SECTION("above desktop dimension limit")
    {
        hex = "89504e470d0a1a0a0000000d4948445200004001000000010806000000c95ddd660000000b4944415478"
              "9c63f80f040009fb03fdfb5e6b2b0000000049454e44ae426082";
        expected = "16384";
    }
    std::string bytes;
    for (size_t i = 0; i < hex.size(); i += 2)
        bytes += char(std::stoul(hex.substr(i, 2), nullptr, 16));
    TextureCookFixture::WriteText(fixture.Source("unsupported.png"), bytes);
    const auto result = fixture.Cooker().Cook({.Source = "unsupported.png"});
    CHECK(result.Status == TextureCookStatus::Failed);
    CHECK(result.Error.find(expected) != std::string::npos);
    CHECK_FALSE(fs::exists(fixture.Source("unsupported.png.texture.json")));
    CHECK_FALSE(fs::exists(fixture.Cooked()));
}

TEST_CASE("Pixel encoding treats spaces and shell metacharacters in asset paths literally",
          "[asset][pixel-cook]")
{
    TextureCookFixture fixture;
    fixture.WriteImage("odd ' ; $().png", 2, 2);
    RequireCooked(fixture.Cooker().Cook({.Source = "odd ' ; $().png", .EncoderThreads = 2}));
}
