#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <limits>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <utility>
#include <vector>

#include <ktx.h>
#include <vulkan/vulkan_core.h>

#include <asset/ImageWriter.h>
#include <asset/TextureCooker.h>

#include "DdsFixture.h"
#include "PixelDdsFixture.h"
#include "PixelTextureEncoder.h"
#include "TextureBlockCodec.h"
#include "TextureCookFixture.h"

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

std::span<const uint8_t> Blocks(ktxTexture2* texture, uint32_t level = 0, uint32_t face = 0)
{
    ktx_size_t offset = 0;
    REQUIRE(ktxTexture_GetImageOffset(ktxTexture(texture), level, 0, face, &offset) == KTX_SUCCESS);
    const auto size = ktxTexture_GetImageSize(ktxTexture(texture), level);
    REQUIRE(offset + size <= texture->dataSize);
    return {texture->pData + offset, size};
}

Image Decode(ktxTexture2* texture, TextureCompression format, const fs::path& directory,
             uint32_t level = 0, uint32_t face = 0)
{
    std::string error;
    const auto image = DecodeTextureBlocks(
        Blocks(texture, level, face),
        {std::max(texture->baseWidth >> level, 1u), std::max(texture->baseHeight >> level, 1u)},
        format, directory / "decode", ConfiguredTextureEncoder(), error);
    INFO(error);
    REQUIRE(image);
    return *image;
}

Image AlphaRamp()
{
    Image image{{}, {256, 1}, 4};
    for (uint32_t alpha = 0; alpha < 256; ++alpha)
        image.Pixels.insert(image.Pixels.end(), {64, 192, 128, static_cast<uint8_t>(alpha)});
    return image;
}

TextureCookRequest MaskRequest(std::string source, TextureCompression format)
{
    TextureCookRequest request{.Source = std::move(source), .EncoderThreads = 2};
    request.Overrides.Format = format;
    request.Overrides.AlphaMode = TextureAlphaMode::Mask;
    request.Overrides.bNoMips = true;
    return request;
}
} // namespace

TEST_CASE("Masked BC1 encodes exact cutoff boundaries, endpoints and binary alpha",
          "[asset][masked-cook]")
{
    TextureCookFixture fixture;
    const auto image = AlphaRamp();
    REQUIRE(WritePng(image.Pixels, image.Extent, fixture.Source("mask.png").string()));
    const double boundary = 128.0 / 255;
    for (const double cutoff :
         {0.0, 0.1, 0.25, 0.5, boundary, std::nextafter(boundary, 1.0), 0.502, 1.0})
    {
        INFO("cutoff " << cutoff);
        auto request = MaskRequest("mask.png", TextureCompression::BC1);
        request.Overrides.AlphaCutoff = cutoff;
        const auto result = fixture.Cooker().Cook(request);
        Cooked(result);
        const auto texture = Open(result.Output);
        CHECK(texture->vkFormat == VK_FORMAT_BC1_RGBA_SRGB_BLOCK);
        CHECK(texture->numLevels == 1);
        CHECK(texture->baseWidth == 256);
        CHECK(texture->baseHeight == 1);
        CHECK(Metadata(texture.get(), "HikariTextureAlphaMode") == "mask");
        const auto storedCutoff = Metadata(texture.get(), "HikariTextureAlphaCutoff");
        REQUIRE(storedCutoff);
        CHECK(std::stod(*storedCutoff) == cutoff);
        const auto decoded = Decode(texture.get(), TextureCompression::BC1, fixture.Root());
        for (uint32_t alpha = 0; alpha < 256; ++alpha)
            CHECK(decoded.Pixels[size_t(alpha) * 4 + 3] == (alpha / 255.0 >= cutoff ? 255 : 0));
        request.Overrides = {};
        CHECK(fixture.Cooker().Cook(request).Status == TextureCookStatus::Skipped);
    }
}

TEST_CASE("Opaque BC1 discards alpha while masked BC7 retains varying alpha and cutoff metadata",
          "[asset][masked-cook]")
{
    TextureCookFixture fixture;
    const auto image = AlphaRamp();
    REQUIRE(WritePng(image.Pixels, image.Extent, fixture.Source("alpha.png").string()));
    auto request = MaskRequest("alpha.png", TextureCompression::BC1);
    request.Overrides.AlphaMode = TextureAlphaMode::Opaque;
    request.Overrides.AlphaCutoff = 0.75;
    const auto opaque = fixture.Cooker().Cook(request);
    Cooked(opaque);
    {
        const auto texture = Open(opaque.Output);
        const auto decoded = Decode(texture.get(), TextureCompression::BC1, fixture.Root());
        for (size_t i = 3; i < decoded.Pixels.size(); i += 4)
            CHECK(decoded.Pixels[i] == 255);
        CHECK(Metadata(texture.get(), "HikariTextureAlphaMode") == "opaque");
        CHECK_FALSE(Metadata(texture.get(), "HikariTextureAlphaCutoff"));
    }
    bool warned = false;
    for (const auto& warning : opaque.Warnings)
        warned |= warning.find("discards the source alpha") != std::string::npos;
    CHECK(warned);
    request.Overrides.Format = TextureCompression::BC7;
    request.Overrides.AlphaMode = TextureAlphaMode::Mask;
    const auto masked = fixture.Cooker().Cook(request);
    Cooked(masked);
    const auto texture = Open(masked.Output);
    CHECK(texture->vkFormat == VK_FORMAT_BC7_SRGB_BLOCK);
    CHECK(texture->numLevels == 1);
    CHECK(Metadata(texture.get(), "HikariTextureAlphaMode") == "mask");
    CHECK(Metadata(texture.get(), "HikariTextureAlphaCutoff") == "0.75");
    const auto decoded = Decode(texture.get(), TextureCompression::BC7, fixture.Root());
    for (uint32_t alpha = 0; alpha < 256; ++alpha)
        CHECK(std::abs(int(decoded.Pixels[size_t(alpha) * 4 + 3]) - int(alpha)) <= 2);
}

TEST_CASE("Masked pixel DDS and cubemap descriptors encode base levels using authored settings",
          "[asset][masked-cook]")
{
    TextureCookFixture fixture;
    SECTION("pixel DDS ignores supplied lower levels with no-mips")
    {
        const auto pixel = [](uint32_t, uint32_t level, uint32_t x, uint32_t)
        {
            return std::array<uint8_t, 4>{64, 192, 128, uint8_t(level ? 255 : (x % 2 ? 128 : 127))};
        };
        TextureCookFixture::WriteText(
            fixture.Source("mask.dds"),
            MakePixelDdsFixture(DdsPixelLayout::BGRA8, 5, 3, 3, 1, true, 4, pixel).Bytes);
        const auto result = fixture.Cooker().Cook(MaskRequest("mask.dds", TextureCompression::BC1));
        Cooked(result);
        const auto texture = Open(result.Output);
        CHECK(texture->numLevels == 1);
        CHECK(texture->baseWidth == 5);
        CHECK(texture->baseHeight == 3);
        const auto decoded = Decode(texture.get(), TextureCompression::BC1, fixture.Root());
        for (uint32_t y = 0; y < 3; ++y)
            for (uint32_t x = 0; x < 5; ++x)
                CHECK(decoded.Pixels[(size_t(y) * 5 + x) * 4 + 3] == (x % 2 ? 255 : 0));
    }
    SECTION("descriptor shares mask settings across six faces")
    {
        fixture.WriteCubemap(
            R"("alphaMode":"mask","alphaCutoff":0.5,"mipmaps":false,"format":"bc1",)");
        const std::array names{"right", "left", "top", "bottom", "back", "front"};
        for (size_t face = 0; face < 6; ++face)
        {
            std::vector<uint8_t> pixels(4 * 4 * 4, 255);
            for (size_t i = 3; i < pixels.size(); i += 4)
                pixels[i] = face % 2 ? 128 : 127;
            REQUIRE(WritePng(pixels, {4, 4},
                             fixture.Source(std::string("sky/") + names[face] + ".png").string()));
        }
        const auto result =
            fixture.Cooker().Cook({.Source = "sky/sky.cubemap.json", .EncoderThreads = 2});
        Cooked(result);
        const auto texture = Open(result.Output);
        CHECK(texture->numLevels == 1);
        CHECK(texture->numFaces == 6);
        for (uint32_t face = 0; face < 6; ++face)
        {
            const auto decoded =
                Decode(texture.get(), TextureCompression::BC1, fixture.Root(), 0, face);
            for (size_t i = 3; i < decoded.Pixels.size(); i += 4)
                CHECK(decoded.Pixels[i] == (face % 2 ? 255 : 0));
        }
    }
}

TEST_CASE(
    "Native DDS mask preservation retains supplied blocks and unspecified alpha stays unspecified",
    "[asset][masked-cook]")
{
    TextureCookFixture fixture;
    const auto dds = MakeDdsFixture("DXT1", 8, 4, 4, 3);
    TextureCookFixture::WriteText(fixture.Source("native.dds"), dds.Bytes);
    const auto unknown = fixture.Cooker().Cook({.Source = "native.dds"});
    Cooked(unknown);
    {
        const auto texture = Open(unknown.Output);
        CHECK_FALSE(Metadata(texture.get(), "HikariTextureAlphaMode"));
        CHECK_FALSE(Metadata(texture.get(), "HikariTextureAlphaCutoff"));
        CHECK_FALSE(Metadata(texture.get(), "HikariDdsAlphaMode"));
    }
    auto request = MaskRequest("native.dds", TextureCompression::BC1);
    request.Overrides.AlphaCutoff = 0.75;
    const auto masked = fixture.Cooker().Cook(request);
    Cooked(masked);
    const auto texture = Open(masked.Output);
    CHECK(texture->numLevels == 1);
    const auto blocks = Blocks(texture.get());
    CHECK(std::equal(blocks.begin(), blocks.end(), dds.Bytes.begin() + dds.Payloads[0].Offset,
                     [](uint8_t actual, char expected)
                     { return actual == static_cast<uint8_t>(expected); }));
    CHECK(Metadata(texture.get(), "HikariTextureAlphaMode") == "mask");
    CHECK(Metadata(texture.get(), "HikariTextureAlphaCutoff") == "0.75");
    CHECK_FALSE(Metadata(texture.get(), "HikariDdsAlphaMode"));
}

TEST_CASE("Invalid mask settings preserve saved settings and the last published asset",
          "[asset][masked-cook]")
{
    TextureCookFixture fixture;
    fixture.WriteImage("mask.png");
    auto request = MaskRequest("mask.png", TextureCompression::BC7);
    const auto success = fixture.Cooker().Cook(request);
    Cooked(success);
    const auto settings = TextureCookFixture::ReadText(fixture.Source("mask.png.json"));
    const auto output = TextureCookFixture::ReadText(success.Output);
    const auto record = TextureCookFixture::ReadText(fixture.Cooked() / "mask.png.cook.json");
    for (const double cutoff : {-0.01, 1.01, std::numeric_limits<double>::infinity(),
                                std::numeric_limits<double>::quiet_NaN()})
    {
        request.Overrides.AlphaCutoff = cutoff;
        const auto failure = fixture.Cooker().Cook(request);
        CHECK(failure.Status == TextureCookStatus::Failed);
        CHECK(failure.Error.find("alphaCutoff") != std::string::npos);
        CHECK(TextureCookFixture::ReadText(fixture.Source("mask.png.json")) == settings);
        CHECK(TextureCookFixture::ReadText(success.Output) == output);
        CHECK(TextureCookFixture::ReadText(fixture.Cooked() / "mask.png.cook.json") == record);
    }
    request.Overrides.AlphaCutoff.reset();
    request.Overrides.ColorSpace = TextureColorSpace::Linear;
    for (const auto format : {TextureCompression::BC4, TextureCompression::BC5})
    {
        request.Overrides.Format = format;
        const auto failure = fixture.Cooker().Cook(request);
        CHECK(failure.Status == TextureCookStatus::Failed);
        CHECK(failure.Error.find("alpha-capable") != std::string::npos);
        CHECK(TextureCookFixture::ReadText(fixture.Source("mask.png.json")) == settings);
        CHECK(TextureCookFixture::ReadText(success.Output) == output);
    }
}

TEST_CASE("Generated masked mips fail before writes and saved cutoffs invalidate warm cooks",
          "[asset][masked-cook]")
{
    TextureCookFixture fixture;
    const auto image = AlphaRamp();
    REQUIRE(WritePng(image.Pixels, image.Extent, fixture.Source("mask.png").string()));
    auto request = MaskRequest("mask.png", TextureCompression::BC1);
    request.Overrides.bNoMips = false;
    const auto rejected = fixture.Cooker().Cook(request);
    CHECK(rejected.Status == TextureCookStatus::Failed);
    CHECK(rejected.Error.find("--no-mips") != std::string::npos);
    CHECK_FALSE(fs::exists(fixture.Source("mask.png.json")));
    CHECK_FALSE(fs::exists(fixture.Cooked()));
    request.Overrides.bNoMips = true;
    Cooked(fixture.Cooker().Cook(request));
    request.Overrides = {};
    CHECK(fixture.Cooker().Cook(request).Status == TextureCookStatus::Skipped);
    std::string error;
    auto saved = ParseTextureSettings(TextureCookFixture::ReadText(fixture.Source("mask.png.json")),
                                      "mask.png.json", error);
    REQUIRE(saved);
    CHECK_FALSE(saved->bMipmaps);
    saved->AlphaCutoff = 0.75;
    const auto json = SerializeTextureSettings(*saved, "mask.png.json", error);
    REQUIRE(json);
    TextureCookFixture::WriteText(fixture.Source("mask.png.json"), *json);
    const auto result = fixture.Cooker().Cook(request);
    Cooked(result);
    const auto texture = Open(result.Output);
    const auto decoded = Decode(texture.get(), TextureCompression::BC1, fixture.Root());
    CHECK(decoded.Pixels[191 * 4 + 3] == 0);
    CHECK(decoded.Pixels[192 * 4 + 3] == 255);
    CHECK(fixture.Cooker().Cook(request).Status == TextureCookStatus::Skipped);
}

TEST_CASE("Masked encoder failures retain successful output, record and recoverable publication",
          "[asset][masked-cook]")
{
    TextureCookFixture fixture;
    fixture.WriteImage("mask.png");
    auto request = MaskRequest("mask.png", TextureCompression::BC1);
    const auto success = fixture.Cooker().Cook(request);
    Cooked(success);
    const auto output = TextureCookFixture::ReadText(success.Output);
    const auto record = TextureCookFixture::ReadText(fixture.Cooked() / "mask.png.cook.json");
    TextureCookEncoder encoder;
    encoder.Encode = [&](const auto& prepared, const auto& temporary, auto& error)
    { return EncodeTexturePixels(prepared, temporary, error, fixture.Root() / "missing tool"); };
    request.bForce = true;
    const auto failure = fixture.Cooker().Cook(request, encoder);
    CHECK(failure.Status == TextureCookStatus::Failed);
    CHECK_FALSE(failure.Error.empty());
    CHECK(TextureCookFixture::ReadText(success.Output) == output);
    CHECK(TextureCookFixture::ReadText(fixture.Cooked() / "mask.png.cook.json") == record);
    CHECK_FALSE(fs::exists(fixture.Cooked() / "mask.png.ktx2.transaction"));
}
