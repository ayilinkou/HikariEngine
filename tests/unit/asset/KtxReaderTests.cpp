#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_string.hpp>

#include <array>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <span>
#include <string>
#include <utility>

#include <asset/KtxReader.h>
#include <asset/TextureCooker.h>
#include <rhi/TextureFootprint.h>

#include "DdsFixture.h"
#include "KtxFixture.h"
#include "TextureCookFixture.h"

using namespace Hikari::Asset;
using Catch::Matchers::ContainsSubstring;

namespace
{
void CheckPayloads(const TextureData& data, const KtxFixture& fixture)
{
    REQUIRE(data.Subresources.size() == fixture.Payloads.size());
    for (size_t i = 0; i < fixture.Payloads.size(); ++i)
    {
        const auto& resource = data.Subresources[i];
        CHECK(resource.MipLevel == i / data.Faces);
        CHECK(resource.Face == i % data.Faces);
        CHECK(resource.Size ==
              Hikari::Rhi::ComputeTextureFootprint(data.Format, resource.Extent).TotalBytes);
        CHECK(std::vector<std::byte>(data.Data.begin() + resource.Offset,
                                     data.Data.begin() + resource.Offset + resource.Size) ==
              fixture.Payloads[i]);
    }
}
} // namespace

TEST_CASE("KTX2 native formats retain every mip and cubemap face without source files",
          "[asset][ktx]")
{
    TextureCookFixture files;
    const std::array native{133u, 134u, 135u, 136u, 137u, 138u, 139u, 140u, 141u, 142u, 145u, 146u};
    const std::array formats{
        Hikari::Rhi::Format::BC1Unorm, Hikari::Rhi::Format::BC1Srgb,  Hikari::Rhi::Format::BC2Unorm,
        Hikari::Rhi::Format::BC2Srgb,  Hikari::Rhi::Format::BC3Unorm, Hikari::Rhi::Format::BC3Srgb,
        Hikari::Rhi::Format::BC4Unorm, Hikari::Rhi::Format::BC4Snorm, Hikari::Rhi::Format::BC5Unorm,
        Hikari::Rhi::Format::BC5Snorm, Hikari::Rhi::Format::BC7Unorm, Hikari::Rhi::Format::BC7Srgb};
    const auto path = files.Root() / "direct.ktx2";
    for (size_t i = 0; i < native.size(); ++i)
        for (const uint32_t faces : {1u, 6u})
        {
            CAPTURE(native[i], faces);
            const auto fixture = MakeKtxFixture(native[i], 7, faces == 6 ? 7 : 5, 3, faces);
            TextureCookFixture::WriteText(path, fixture.Bytes);
            std::string error = "old error";
            const auto data = ReadKtxTexture(path, error);
            INFO(error);
            REQUIRE(data);
            CHECK(error.empty());
            CHECK(data->Format == formats[i]);
            CHECK(data->Extent.Width == 7);
            CHECK(data->Extent.Height == (faces == 6 ? 7 : 5));
            CHECK(data->MipLevels == 3);
            CHECK(data->Faces == faces);
            CHECK_FALSE(data->Metadata.Usage);
            CHECK_FALSE(data->Metadata.AlphaMode);
            CHECK_FALSE(data->Metadata.NormalEncoding);
            CheckPayloads(*data, fixture);
        }
    // The value owns its blocks after the file and libktx object are gone.
    const auto fixture = MakeKtxFixture(145, 1, 1, 1);
    TextureCookFixture::WriteText(path, fixture.Bytes);
    std::string error;
    auto data = ReadKtxTexture(path, error);
    REQUIRE(data);
    std::filesystem::remove(path);
    auto moved = std::move(*data);
    CheckPayloads(moved, fixture);
}

TEST_CASE("KTX2 reader consumes real cooked DDS output and semantic metadata", "[asset][ktx]")
{
    TextureCookFixture files;
    for (const auto dxgi : {71u, 74u, 77u, 80u, 81u, 83u, 84u, 98u})
    {
        CAPTURE(dxgi);
        const uint32_t blockBytes = dxgi == 71 || dxgi == 80 || dxgi == 81 ? 8 : 16;
        const auto source = MakeDdsFixture("DX10", blockBytes, 12, 12, 4, 6, dxgi);
        TextureCookFixture::WriteText(files.Source("real.dds"), source.Bytes);
        std::filesystem::remove(files.Source("real.dds.texture.json"));
        const auto cooked = files.Cooker().Cook({.Source = "real.dds"});
        INFO(cooked.Error);
        REQUIRE(cooked.Status == TextureCookStatus::Cooked);
        std::string error;
        const auto data = ReadKtxTexture(cooked.Output, error);
        INFO(error);
        REQUIRE(data);
        CHECK(data->Metadata.Usage == TextureUsage::Color);
        CHECK_FALSE(data->Metadata.AlphaMode);
        REQUIRE(data->Subresources.size() == source.Payloads.size());
        for (const auto& payload : source.Payloads)
        {
            const auto& resource = data->Subresources[payload.Level * 6 + payload.Face];
            const auto bytes =
                std::as_bytes(std::span(source.Bytes)).subspan(payload.Offset, payload.Size);
            CHECK(std::equal(bytes.begin(), bytes.end(), data->Data.begin() + resource.Offset));
        }
    }
    const std::array normalFormats{141u, 142u, 145u, 141u, 137u};
    const std::array encodings{"xy-unorm-positive-z", "xy-snorm-positive-z", "xyz-unorm",
                               "yx-unorm-positive-z", "ag-unorm-positive-z"};
    const std::array expected{
        TextureNormalEncoding::XyUnormPositiveZ, TextureNormalEncoding::XySnormPositiveZ,
        TextureNormalEncoding::XyzUnorm, TextureNormalEncoding::YxUnormPositiveZ,
        TextureNormalEncoding::AgUnormPositiveZ};
    for (size_t i = 0; i < normalFormats.size(); ++i)
    {
        std::vector<std::pair<std::string, std::string>> metadata{
            {"HikariTextureUsage", "normal"},
            {"HikariNormalEncoding", encodings[i]},
            {"HikariDdsNormalMap", "true"}};
        if (i == 3 || i == 4)
            metadata.emplace_back("HikariDdsSwizzle", i == 3 ? "A2XY" : "A2D5");
        const auto fixture = MakeKtxFixture(normalFormats[i], 12, 12, 4, 1, metadata);
        const auto path = files.Root() / "normal.ktx2";
        TextureCookFixture::WriteText(path, fixture.Bytes);
        std::string error;
        const auto data = ReadKtxTexture(path, error);
        INFO(error);
        REQUIRE(data);
        CHECK(data->Metadata.Usage == TextureUsage::Normal);
        CHECK(data->Metadata.NormalEncoding == expected[i]);
        CHECK(data->Metadata.bDdsNormalMap);
        CheckPayloads(*data, fixture);
    }
}

TEST_CASE("KTX2 reader preserves masked cutoff and DDS premultiplied alpha", "[asset][ktx]")
{
    TextureCookFixture files;
    const auto path = files.Root() / "metadata.ktx2";
    auto fixture = MakeKtxFixture(134, 4, 4, 1, 1,
                                  {{"KTXorientation", "rd"},
                                   {"HikariTextureUsage", "color"},
                                   {"HikariTextureAlphaMode", "mask"},
                                   {"HikariTextureAlphaCutoff", "0.5"}});
    TextureCookFixture::WriteText(path, fixture.Bytes);
    std::string error;
    auto data = ReadKtxTexture(path, error);
    INFO(error);
    REQUIRE(data);
    CHECK(data->Metadata.AlphaMode == TextureAlphaMode::Mask);
    CHECK(data->Metadata.AlphaCutoff == 0.5);
    const auto dds = MakeDdsFixture("DX10", 16, 4, 4, 3, 1, 78, 2);
    TextureCookFixture::WriteText(files.Source("premult.dds"), dds.Bytes);
    const auto cooked = files.Cooker().Cook({.Source = "premult.dds"});
    INFO(cooked.Error);
    REQUIRE(cooked.Status == TextureCookStatus::Cooked);
    data = ReadKtxTexture(cooked.Output, error);
    INFO(error);
    REQUIRE(data);
    CHECK(data->Metadata.DdsAlpha == DdsAlphaMode::Premultiplied);
    CHECK(data->Metadata.bPremultipliedAlpha);
}

TEST_CASE("KTX2 reader accepts cooked images normals and cubemap descriptors", "[asset][ktx]")
{
    TextureCookFixture files;
    files.WriteImage("image.png", 12, 12);
    files.WriteImage("normal.png", 12, 12);
    files.WriteCubemap();
    const auto check = [&](const TextureCookRequest& request, uint32_t faces,
                           Hikari::Rhi::Format format, TextureUsage usage)
    {
        const auto cooked = files.Cooker().Cook(request);
        INFO(cooked.Error);
        REQUIRE(cooked.Status == TextureCookStatus::Cooked);
        std::string error;
        const auto data = ReadKtxTexture(cooked.Output, error);
        INFO(error);
        REQUIRE(data);
        CHECK(data->Faces == faces);
        CHECK(data->Format == format);
        CHECK(data->Metadata.Usage == usage);
        if (usage == TextureUsage::Normal)
            CHECK(data->Metadata.NormalEncoding == TextureNormalEncoding::XyUnormPositiveZ);
        for (const auto& resource : data->Subresources)
            CHECK(resource.Size ==
                  Hikari::Rhi::ComputeTextureFootprint(format, resource.Extent).TotalBytes);
    };
    check({.Source = "image.png"}, 1, Hikari::Rhi::Format::BC7Srgb, TextureUsage::Color);
    TextureCookRequest normal{.Source = "normal.png"};
    normal.Overrides.Usage = TextureUsage::Normal;
    check(normal, 1, Hikari::Rhi::Format::BC5Unorm, TextureUsage::Normal);
    check({.Source = "sky/sky.cubemap.json"}, 6, Hikari::Rhi::Format::BC7Srgb, TextureUsage::Color);
}

TEST_CASE("KTX2 reader rejects malformed header format shape level and payload metadata",
          "[asset][ktx]")
{
    TextureCookFixture files;
    auto fixture = MakeKtxFixture();
    SECTION("identifier")
    {
        fixture.Bytes[0] = 'K';
    }
    SECTION("KTX1")
    {
        fixture.Bytes[5] = '1';
    }
    SECTION("truncated header")
    {
        fixture.Bytes.resize(79);
    }
    SECTION("undefined transcoding format")
    {
        SetKtxFixtureUnsigned(fixture.Bytes, 12, 0);
    }
    SECTION("uncompressed format")
    {
        SetKtxFixtureUnsigned(fixture.Bytes, 12, 37);
    }
    SECTION("BC1 RGB interpretation")
    {
        SetKtxFixtureUnsigned(fixture.Bytes, 12, 131);
    }
    SECTION("unsupported BC6")
    {
        SetKtxFixtureUnsigned(fixture.Bytes, 12, 143);
    }
    SECTION("supercompression")
    {
        SetKtxFixtureUnsigned(fixture.Bytes, 44, 2);
    }
    SECTION("type size")
    {
        SetKtxFixtureUnsigned(fixture.Bytes, 16, 4);
    }
    SECTION("zero width")
    {
        SetKtxFixtureUnsigned(fixture.Bytes, 20, 0);
    }
    SECTION("1D")
    {
        SetKtxFixtureUnsigned(fixture.Bytes, 24, 0);
    }
    SECTION("volume")
    {
        SetKtxFixtureUnsigned(fixture.Bytes, 28, 1);
    }
    SECTION("array")
    {
        SetKtxFixtureUnsigned(fixture.Bytes, 32, 1);
    }
    SECTION("invalid faces")
    {
        SetKtxFixtureUnsigned(fixture.Bytes, 36, 2);
    }
    SECTION("nonsquare cube")
    {
        SetKtxFixtureUnsigned(fixture.Bytes, 36, 6);
        SetKtxFixtureUnsigned(fixture.Bytes, 24, 8);
    }
    SECTION("zero levels")
    {
        SetKtxFixtureUnsigned(fixture.Bytes, 40, 0);
    }
    SECTION("too many levels")
    {
        SetKtxFixtureUnsigned(fixture.Bytes, 40, 33);
    }
    SECTION("DFD range")
    {
        SetKtxFixtureUnsigned(fixture.Bytes, 48, std::numeric_limits<uint32_t>::max());
    }
    SECTION("DFD size")
    {
        SetKtxFixtureUnsigned(fixture.Bytes, 52, 16);
    }
    SECTION("DFD block model")
    {
        fixture.Bytes[KtxFixtureUnsigned(fixture.Bytes, 48) + 12] = 1;
    }
    SECTION("DFD block extent")
    {
        fixture.Bytes[KtxFixtureUnsigned(fixture.Bytes, 48) + 16] = 7;
    }
    SECTION("DFD block bytes")
    {
        fixture.Bytes[KtxFixtureUnsigned(fixture.Bytes, 48) + 20] = 16;
    }
    SECTION("DFD signedness")
    {
        fixture.Bytes[KtxFixtureUnsigned(fixture.Bytes, 48) + 31] ^= 0x40;
    }
    SECTION("DFD transfer")
    {
        fixture.Bytes[KtxFixtureUnsigned(fixture.Bytes, 48) + 14] = 2;
    }
    SECTION("KVD range")
    {
        SetKtxFixtureUnsigned(fixture.Bytes, 56, std::numeric_limits<uint32_t>::max());
    }
    SECTION("KVD entry size")
    {
        SetKtxFixtureUnsigned(fixture.Bytes, KtxFixtureUnsigned(fixture.Bytes, 56), 0xFFFFFFFF);
    }
    SECTION("duplicate metadata keys")
    {
        fixture = MakeKtxFixture(133, 12, 12, 4, 1, {{"Aaaa", "one"}, {"Bbbb", "two"}});
        const auto position = fixture.Bytes.find("Bbbb");
        REQUIRE(position != std::string::npos);
        fixture.Bytes.replace(position, 4, "Aaaa");
    }
    SECTION("SGD")
    {
        SetKtxFixtureUnsigned(fixture.Bytes, 64, 1, 8);
    }
    SECTION("level size")
    {
        SetKtxFixtureUnsigned(fixture.Bytes, 88, 8, 8);
    }
    SECTION("uncompressed level size")
    {
        SetKtxFixtureUnsigned(fixture.Bytes, 96, 8, 8);
    }
    SECTION("level overlaps metadata")
    {
        SetKtxFixtureUnsigned(fixture.Bytes, 80, 80, 8);
    }
    SECTION("level overlaps another")
    {
        SetKtxFixtureUnsigned(fixture.Bytes, 80, KtxFixtureUnsigned(fixture.Bytes, 104, 8), 8);
    }
    SECTION("overflowing offset")
    {
        SetKtxFixtureUnsigned(fixture.Bytes, 80, std::numeric_limits<uint64_t>::max(), 8);
    }
    SECTION("truncated payload")
    {
        fixture.Bytes.pop_back();
    }
    SECTION("extra payload")
    {
        fixture.Bytes += 'x';
    }
    const auto path = files.Root() / "malformed.ktx2";
    TextureCookFixture::WriteText(path, fixture.Bytes);
    std::string error;
    CHECK_FALSE(ReadKtxTexture(path, error));
    CHECK_THAT(error, ContainsSubstring(path.string()));
    CHECK_THAT(error, ContainsSubstring("KTX2"));
}

TEST_CASE("KTX2 reader rejects unsupported or contradictory semantic metadata", "[asset][ktx]")
{
    TextureCookFixture files;
    std::vector<std::pair<std::string, std::string>> metadata;
    uint32_t format = 133;
    SECTION("orientation")
    {
        metadata = {{"KTXorientation", "ru"}};
    }
    SECTION("swizzle")
    {
        metadata = {{"KTXswizzle", "bgra"}};
    }
    SECTION("usage")
    {
        metadata = {{"HikariTextureUsage", "unknown"}};
    }
    SECTION("alpha policy")
    {
        metadata = {{"HikariTextureAlphaMode", "blend"}};
    }
    SECTION("mask requires cutoff")
    {
        metadata = {{"HikariTextureAlphaMode", "mask"}};
    }
    SECTION("cutoff requires mask")
    {
        metadata = {{"HikariTextureAlphaCutoff", "0.5"}};
    }
    SECTION("nonfinite cutoff")
    {
        metadata = {{"HikariTextureAlphaMode", "mask"}, {"HikariTextureAlphaCutoff", "nan"}};
    }
    SECTION("out of range cutoff")
    {
        metadata = {{"HikariTextureAlphaMode", "mask"}, {"HikariTextureAlphaCutoff", "1.1"}};
    }
    SECTION("mask without alpha")
    {
        format = 141;
        metadata = {{"HikariTextureAlphaMode", "mask"}, {"HikariTextureAlphaCutoff", "0.5"}};
    }
    SECTION("normal missing encoding")
    {
        metadata = {{"HikariTextureUsage", "normal"}};
    }
    SECTION("normal signedness mismatch")
    {
        format = 141;
        metadata = {{"HikariTextureUsage", "normal"},
                    {"HikariNormalEncoding", "xy-snorm-positive-z"}};
    }
    SECTION("normal sRGB")
    {
        format = 134;
        metadata = {{"HikariTextureUsage", "normal"}, {"HikariNormalEncoding", "xyz-unorm"}};
    }
    SECTION("normal swizzle mismatch")
    {
        format = 141;
        metadata = {{"HikariTextureUsage", "normal"},
                    {"HikariNormalEncoding", "yx-unorm-positive-z"}};
    }
    SECTION("premultiplied flag mismatch")
    {
        metadata = {{"HikariDdsAlphaMode", "premultiplied"}};
    }
    SECTION("unknown DDS alpha")
    {
        metadata = {{"HikariDdsAlphaMode", "unknown"}};
    }
    SECTION("unknown DDS swizzle")
    {
        metadata = {{"HikariDdsSwizzle", "bad"}};
    }
    SECTION("DDS normal flag")
    {
        metadata = {{"HikariDdsNormalMap", "false"}};
    }
    SECTION("embedded NUL")
    {
        metadata = {{"HikariTextureUsage", std::string("color\0bad", 9)}};
    }
    const auto fixture = MakeKtxFixture(format, 4, 4, 1, 1, metadata);
    const auto path = files.Root() / "bad-metadata.ktx2";
    TextureCookFixture::WriteText(path, fixture.Bytes);
    std::string error;
    CHECK_FALSE(ReadKtxTexture(path, error));
    CHECK_THAT(error, ContainsSubstring(path.string()));
}

TEST_CASE("KTX2 reader reports missing files without requiring cook records", "[asset][ktx]")
{
    TextureCookFixture files;
    std::string error;
    const auto path = files.Root() / "missing.ktx2";
    CHECK_FALSE(ReadKtxTexture(path, error));
    CHECK_THAT(error, ContainsSubstring(path.string()));
    CHECK_THAT(error, ContainsSubstring("cannot open"));
}
