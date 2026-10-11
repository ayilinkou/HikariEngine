#include <catch2/catch_test_macros.hpp>

#include <array>
#include <filesystem>
#include <limits>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include <KHR/khr_df.h>
#include <ktx.h>
#include <vulkan/vulkan_core.h>

#include <asset/DdsReader.h>
#include <asset/TextureCooker.h>

#include "DdsFixture.h"
#include "DdsKtxWriter.h"
#include "TextureCookFixture.h"

using namespace Hikari::Asset;
namespace fs = std::filesystem;

namespace
{
struct FormatCase
{
    std::string_view FourCC;
    uint32_t Dxgi;
    TextureCompression Format;
    uint32_t BlockBytes;
    uint32_t NativeKtx;
};

constexpr std::array kFormats{
    FormatCase{"DXT1", 0, TextureCompression::BC1, 8, VK_FORMAT_BC1_RGBA_SRGB_BLOCK},
    FormatCase{"DXT2", 0, TextureCompression::BC2, 16, VK_FORMAT_BC2_SRGB_BLOCK},
    FormatCase{"DXT3", 0, TextureCompression::BC2, 16, VK_FORMAT_BC2_SRGB_BLOCK},
    FormatCase{"DXT4", 0, TextureCompression::BC3, 16, VK_FORMAT_BC3_SRGB_BLOCK},
    FormatCase{"DXT5", 0, TextureCompression::BC3, 16, VK_FORMAT_BC3_SRGB_BLOCK},
    FormatCase{"ATI1", 0, TextureCompression::BC4, 8, VK_FORMAT_BC4_UNORM_BLOCK},
    FormatCase{"BC4U", 0, TextureCompression::BC4, 8, VK_FORMAT_BC4_UNORM_BLOCK},
    FormatCase{"BC4S", 0, TextureCompression::BC4Signed, 8, VK_FORMAT_BC4_SNORM_BLOCK},
    FormatCase{"ATI2", 0, TextureCompression::BC5, 16, VK_FORMAT_BC5_UNORM_BLOCK},
    FormatCase{"BC5U", 0, TextureCompression::BC5, 16, VK_FORMAT_BC5_UNORM_BLOCK},
    FormatCase{"BC5S", 0, TextureCompression::BC5Signed, 16, VK_FORMAT_BC5_SNORM_BLOCK},
    FormatCase{"DX10", 71, TextureCompression::BC1, 8, VK_FORMAT_BC1_RGBA_UNORM_BLOCK},
    FormatCase{"DX10", 72, TextureCompression::BC1, 8, VK_FORMAT_BC1_RGBA_SRGB_BLOCK},
    FormatCase{"DX10", 74, TextureCompression::BC2, 16, VK_FORMAT_BC2_UNORM_BLOCK},
    FormatCase{"DX10", 75, TextureCompression::BC2, 16, VK_FORMAT_BC2_SRGB_BLOCK},
    FormatCase{"DX10", 77, TextureCompression::BC3, 16, VK_FORMAT_BC3_UNORM_BLOCK},
    FormatCase{"DX10", 78, TextureCompression::BC3, 16, VK_FORMAT_BC3_SRGB_BLOCK},
    FormatCase{"DX10", 80, TextureCompression::BC4, 8, VK_FORMAT_BC4_UNORM_BLOCK},
    FormatCase{"DX10", 81, TextureCompression::BC4Signed, 8, VK_FORMAT_BC4_SNORM_BLOCK},
    FormatCase{"DX10", 83, TextureCompression::BC5, 16, VK_FORMAT_BC5_UNORM_BLOCK},
    FormatCase{"DX10", 84, TextureCompression::BC5Signed, 16, VK_FORMAT_BC5_SNORM_BLOCK},
    FormatCase{"DX10", 98, TextureCompression::BC7, 16, VK_FORMAT_BC7_UNORM_BLOCK},
    FormatCase{"DX10", 99, TextureCompression::BC7, 16, VK_FORMAT_BC7_SRGB_BLOCK}};

void DestroyKtx(ktxTexture2* texture)
{
    ktxTexture_Destroy(ktxTexture(texture));
}

std::unique_ptr<ktxTexture2, decltype(&DestroyKtx)> OpenKtx(const fs::path& path)
{
    ktxTexture2* texture = nullptr;
    const auto status = ktxTexture2_CreateFromNamedFile(
        path.string().c_str(), KTX_TEXTURE_CREATE_LOAD_IMAGE_DATA_BIT, &texture);
    INFO(ktxErrorString(status));
    REQUIRE(status == KTX_SUCCESS);
    return {texture, &DestroyKtx};
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

void CheckPayloads(ktxTexture2* texture, const DdsFixture& fixture)
{
    for (const auto& payload : fixture.Payloads)
    {
        if (payload.Level >= texture->numLevels)
            continue;
        INFO("face " << payload.Face << " mip " << payload.Level);
        ktx_size_t offset = 0;
        REQUIRE(ktxTexture_GetImageOffset(ktxTexture(texture), payload.Level, 0, payload.Face,
                                          &offset) == KTX_SUCCESS);
        REQUIRE(ktxTexture_GetImageSize(ktxTexture(texture), payload.Level) == payload.Size);
        REQUIRE(offset <= texture->dataSize);
        REQUIRE(payload.Size <= texture->dataSize - offset);
        CHECK(std::string_view(reinterpret_cast<const char*>(texture->pData + offset),
                               payload.Size) ==
              std::string_view(fixture.Bytes).substr(payload.Offset, payload.Size));
    }
}

void RequireStatus(const TextureCookResult& result, TextureCookStatus expected)
{
    INFO(result.Error);
    REQUIRE(result.Status == expected);
}
} // namespace

TEST_CASE("DDS native formats preserve all blocks, faces and supplied mips through KTX2",
          "[asset][dds]")
{
    TextureCookFixture fixture;
    const auto cooker = fixture.Cooker();
    for (const auto& format : kFormats)
        for (const uint32_t faces : {1u, 6u})
        {
            INFO(format.FourCC << " DXGI " << format.Dxgi << " faces " << faces);
            const auto dds = MakeDdsFixture(format.FourCC, format.BlockBytes, 7, faces == 6 ? 7 : 5,
                                            3, faces, format.Dxgi);
            TextureCookFixture::WriteText(fixture.Source("native.dds"), dds.Bytes);
            fs::remove(fixture.Source("native.dds.json"));
            TextureCookRequest request{.Source = "native.dds"};
            // Legacy DDS without colour metadata resolves from explicitly authored usage.
            if (format.NativeKtx == VK_FORMAT_BC4_UNORM_BLOCK ||
                format.NativeKtx == VK_FORMAT_BC4_SNORM_BLOCK ||
                format.NativeKtx == VK_FORMAT_BC5_UNORM_BLOCK ||
                format.NativeKtx == VK_FORMAT_BC5_SNORM_BLOCK)
                request.Overrides.Usage = TextureUsage::Packed;
            const auto result = cooker.Cook(request);
            RequireStatus(result, TextureCookStatus::Cooked);
            const auto texture = OpenKtx(result.Output);
            CHECK(texture->vkFormat == format.NativeKtx);
            CHECK(texture->baseWidth == 7);
            CHECK(texture->baseHeight == (faces == 6 ? 7 : 5));
            CHECK(texture->baseDepth == 1);
            CHECK(texture->numDimensions == 2);
            CHECK(texture->numLevels == 3);
            CHECK(texture->numFaces == faces);
            CHECK(texture->numLayers == 1);
            CHECK_FALSE(texture->isArray);
            CHECK(texture->supercompressionScheme == KTX_SS_NONE);
            CHECK(Metadata(texture.get(), KTX_ORIENTATION_KEY) == "rd");
            CheckPayloads(texture.get(), dds);
            std::string error;
            const auto info = ReadDdsInfo(fixture.Source("native.dds"), error);
            REQUIRE(info);
            CHECK(info->Format == format.Format);
            CHECK(info->Faces == faces);
            REQUIRE(info->Subresources.size() == dds.Payloads.size());
            for (size_t i = 0; i < dds.Payloads.size(); ++i)
            {
                CHECK(info->Subresources[i].Offset == dds.Payloads[i].Offset);
                CHECK(info->Subresources[i].Size == dds.Payloads[i].Size);
                CHECK(info->Subresources[i].Level == dds.Payloads[i].Level);
                CHECK(info->Subresources[i].Face == dds.Payloads[i].Face);
            }
            request.Overrides = {};
            RequireStatus(cooker.Cook(request), TextureCookStatus::Skipped);
            request.Overrides.bNoMips = true;
            const auto base = cooker.Cook(request);
            RequireStatus(base, TextureCookStatus::Cooked);
            const auto baseTexture = OpenKtx(base.Output);
            CHECK(baseTexture->numLevels == 1);
            CHECK(baseTexture->numFaces == faces);
            CheckPayloads(baseTexture.get(), dds);
        }
}

TEST_CASE("DDS alpha representation survives repackaging without inventing missing metadata",
          "[asset][dds]")
{
    TextureCookFixture fixture;
    for (uint32_t alpha = 0; alpha <= 4; ++alpha)
    {
        const auto dds = MakeDdsFixture("DX10", 16, 4, 4, 3, 6, 78, alpha);
        TextureCookFixture::WriteText(fixture.Source("alpha.dds"), dds.Bytes);
        fs::remove(fixture.Source("alpha.dds.json"));
        const auto result = fixture.Cooker().Cook({.Source = "alpha.dds"});
        RequireStatus(result, TextureCookStatus::Cooked);
        const auto texture = OpenKtx(result.Output);
        const std::array<const char*, 5> names{nullptr, "straight", "premultiplied", "opaque",
                                               "custom"};
        const auto mode = Metadata(texture.get(), "HikariDdsAlphaMode");
        if (!alpha)
            CHECK_FALSE(mode);
        else
            CHECK(mode == names[alpha]);
        CHECK(KHR_DFDVAL(texture->pDfd + 1, FLAGS) ==
              (alpha == 2 ? KHR_DF_FLAG_ALPHA_PREMULTIPLIED : KHR_DF_FLAG_ALPHA_STRAIGHT));
        CHECK(KHR_DFDVAL(texture->pDfd + 1, TRANSFER) == KHR_DF_TRANSFER_SRGB);
        CheckPayloads(texture.get(), dds);
        std::string error;
        const auto saved =
            ParseTextureSettings(TextureCookFixture::ReadText(fixture.Source("alpha.dds.json")),
                                 "alpha.dds.json", error);
        REQUIRE(saved);
        CHECK_FALSE(saved->AlphaMode);
    }
    for (const auto fourCC : {"DXT2", "DXT4"})
    {
        const auto dds = MakeDdsFixture(fourCC, 16);
        TextureCookFixture::WriteText(fixture.Source("legacy.dds"), dds.Bytes);
        const auto result = fixture.Cooker().Cook({.Source = "legacy.dds"});
        RequireStatus(result, TextureCookStatus::Cooked);
        const auto texture = OpenKtx(result.Output);
        CHECK(Metadata(texture.get(), "HikariDdsAlphaMode") == "premultiplied");
        CHECK(KHR_DFDVAL(texture->pDfd + 1, FLAGS) == KHR_DF_FLAG_ALPHA_PREMULTIPLIED);
        CheckPayloads(texture.get(), dds);
    }
}

TEST_CASE("DDS colour reinterpretation and usage changes preserve native payloads", "[asset][dds]")
{
    TextureCookFixture fixture;
    const auto dds = MakeDdsFixture("DX10", 16, 4, 4, 3, 1, 98);
    TextureCookFixture::WriteText(fixture.Source("data.dds"), dds.Bytes);
    TextureCookRequest request{.Source = "data.dds"};
    auto result = fixture.Cooker().Cook(request);
    RequireStatus(result, TextureCookStatus::Cooked);
    {
        const auto texture = OpenKtx(result.Output);
        CHECK(texture->vkFormat == VK_FORMAT_BC7_UNORM_BLOCK);
        CheckPayloads(texture.get(), dds);
    }
    request.Overrides.ColorSpace = TextureColorSpace::Srgb;
    result = fixture.Cooker().Cook(request);
    RequireStatus(result, TextureCookStatus::Cooked);
    {
        const auto texture = OpenKtx(result.Output);
        CHECK(texture->vkFormat == VK_FORMAT_BC7_SRGB_BLOCK);
        CHECK(KHR_DFDVAL(texture->pDfd + 1, TRANSFER) == KHR_DF_TRANSFER_SRGB);
        CheckPayloads(texture.get(), dds);
    }
    request.Overrides.Usage = TextureUsage::Normal;
    request.Overrides.ColorSpace = TextureColorSpace::Linear;
    result = fixture.Cooker().Cook(request);
    RequireStatus(result, TextureCookStatus::Cooked);
    const auto texture = OpenKtx(result.Output);
    CHECK(texture->vkFormat == VK_FORMAT_BC7_UNORM_BLOCK);
    CHECK(Metadata(texture.get(), "HikariTextureUsage") == "normal");
    CheckPayloads(texture.get(), dds);
}

TEST_CASE("Partial DDS chains are bounded and repackaged but cooking waits for mip completion",
          "[asset][dds]")
{
    TextureCookFixture fixture;
    for (const uint32_t faces : {1u, 6u})
    {
        const auto dds = MakeDdsFixture("DX10", 16, 8, 8, 2, faces, 77);
        const auto source = fixture.Source("partial.dds");
        TextureCookFixture::WriteText(source, dds.Bytes);
        fs::remove(fixture.Source("partial.dds.json"));
        std::string error;
        std::vector<std::string> warnings;
        const auto prepared = fixture.Cooker().Prepare({.Source = "partial.dds"}, error, warnings);
        REQUIRE(prepared);
        REQUIRE(prepared->Dds);
        REQUIRE(RepackageDds(source, *prepared->Dds, prepared->Settings,
                             fixture.Root() / "partial.ktx2", error));
        const auto texture = OpenKtx(fixture.Root() / "partial.ktx2");
        CHECK(texture->numLevels == 2);
        CHECK(texture->numFaces == faces);
        CheckPayloads(texture.get(), dds);
        const auto result = fixture.Cooker().Cook({.Source = "partial.dds"});
        CHECK(result.Status == TextureCookStatus::Failed);
        CHECK(result.Error.find("Missing DDS mip generation") != std::string::npos);
        bool warned = false;
        for (const auto& warning : result.Warnings)
            warned |= warning.find("does not preserve alpha-test coverage") != std::string::npos;
        CHECK(warned);
        TextureCookRequest base{.Source = "partial.dds"};
        base.Overrides.bNoMips = true;
        const auto baseResult = fixture.Cooker().Cook(base);
        RequireStatus(baseResult, TextureCookStatus::Cooked);
        const auto baseTexture = OpenKtx(baseResult.Output);
        CHECK(baseTexture->numLevels == 1);
        CHECK(baseTexture->numFaces == faces);
        CheckPayloads(baseTexture.get(), dds);
    }
}

TEST_CASE("Native normal DDS preserves signedness, all blocks and normal decoding metadata",
          "[asset][dds][normal-cook]")
{
    TextureCookFixture fixture;
    for (const auto dxgi : {83u, 84u, 98u})
        for (const auto faces : {1u, 6u})
        {
            auto dds = MakeDdsFixture("DX10", 16, 4, 4, 3, faces, dxgi);
            dds.SetWord(80, 0x80000004u);
            TextureCookFixture::WriteText(fixture.Source("normal.dds"), dds.Bytes);
            fs::remove(fixture.Source("normal.dds.json"));
            TextureCookRequest request{.Source = "normal.dds"};
            request.Overrides.Usage = TextureUsage::Normal;
            const auto result = fixture.Cooker().Cook(request);
            RequireStatus(result, TextureCookStatus::Cooked);
            const auto texture = OpenKtx(result.Output);
            CHECK(texture->vkFormat == uint32_t(dxgi == 83   ? VK_FORMAT_BC5_UNORM_BLOCK
                                                : dxgi == 84 ? VK_FORMAT_BC5_SNORM_BLOCK
                                                             : VK_FORMAT_BC7_UNORM_BLOCK));
            CHECK(Metadata(texture.get(), "HikariTextureUsage") == "normal");
            CHECK(Metadata(texture.get(), "HikariNormalEncoding") ==
                  (dxgi == 83   ? "xy-unorm-positive-z"
                   : dxgi == 84 ? "xy-snorm-positive-z"
                                : "xyz-unorm"));
            CHECK(Metadata(texture.get(), "HikariDdsNormalMap") == "true");
            CheckPayloads(texture.get(), dds);
        }
}

TEST_CASE("Legacy normal-map metadata never overrides authored DDS usage or accepts unknown flags",
          "[asset][dds][normal-cook]")
{
    TextureCookFixture fixture;
    auto dds = MakeDdsFixture("ATI2", 16, 4, 4, 3);
    dds.SetWord(80, 0x80000004u);
    const auto source = fixture.Source("data.dds");
    TextureCookFixture::WriteText(source, dds.Bytes);
    std::string error;
    const auto info = ReadDdsInfo(source, error);
    REQUIRE(info);
    CHECK(info->bNormalMap);
    TextureCookRequest request{.Source = "data.dds"};
    request.Overrides.Usage = TextureUsage::Packed;
    const auto result = fixture.Cooker().Cook(request);
    RequireStatus(result, TextureCookStatus::Cooked);
    const auto texture = OpenKtx(result.Output);
    CHECK(Metadata(texture.get(), "HikariTextureUsage") == "packed");
    CHECK(Metadata(texture.get(), "HikariDdsNormalMap") == "true");
    CHECK_FALSE(Metadata(texture.get(), "HikariNormalEncoding"));
    CheckPayloads(texture.get(), dds);
    dds.SetWord(80, 0x80000044u);
    TextureCookFixture::WriteText(source, dds.Bytes);
    CHECK_FALSE(ReadDdsInfo(source, error));
    CHECK(error.find("flags") != std::string::npos);
}

TEST_CASE("Explicit NVTT swizzles retain blocks and identify their normal decoding channels",
          "[asset][dds][normal-cook]")
{
    TextureCookFixture fixture;
    for (const bool bSwapAlpha : {false, true})
    {
        auto dds = MakeDdsFixture(bSwapAlpha ? "DXT5" : "ATI2", 16, 4, 4, 3);
        dds.SetWord(80, 0x80000004u);
        dds.SetWord(88, bSwapAlpha ? 0x35443241u : 0x59583241u);
        TextureCookFixture::WriteText(fixture.Source("normal.dds"), dds.Bytes);
        fs::remove(fixture.Source("normal.dds.json"));
        TextureCookRequest request{.Source = "normal.dds"};
        request.Overrides.Usage = TextureUsage::Normal;
        const auto result = fixture.Cooker().Cook(request);
        RequireStatus(result, TextureCookStatus::Cooked);
        const auto texture = OpenKtx(result.Output);
        CHECK(Metadata(texture.get(), "HikariNormalEncoding") ==
              (bSwapAlpha ? "ag-unorm-positive-z" : "yx-unorm-positive-z"));
        CHECK(Metadata(texture.get(), "HikariDdsSwizzle") == (bSwapAlpha ? "A2D5" : "A2XY"));
        CheckPayloads(texture.get(), dds);
        dds.SetWord(88, bSwapAlpha ? 0x59583241u : 0x35443241u);
        TextureCookFixture::WriteText(fixture.Source("normal.dds"), dds.Bytes);
        std::string error;
        CHECK_FALSE(ReadDdsInfo(fixture.Source("normal.dds"), error));
        CHECK(error.find("swizzle") != std::string::npos);
    }
}

TEST_CASE("DDS masks preserve supplied levels and reject generated masked mips", "[asset][dds]")
{
    TextureCookFixture fixture;
    const auto dds = MakeDdsFixture("DXT1", 8, 4, 4, 3, 6);
    TextureCookFixture::WriteText(fixture.Source("mask.dds"), dds.Bytes);
    TextureCookRequest request{.Source = "mask.dds"};
    request.Overrides.AlphaMode = TextureAlphaMode::Mask;
    const auto result = fixture.Cooker().Cook(request);
    RequireStatus(result, TextureCookStatus::Cooked);
    const auto texture = OpenKtx(result.Output);
    CheckPayloads(texture.get(), dds);
    const auto original = TextureCookFixture::ReadText(result.Output);
    TextureCookFixture::WriteText(fixture.Source("mask.dds"),
                                  MakeDdsFixture("DXT1", 8, 4, 4, 1, 6).Bytes);
    const auto failure = fixture.Cooker().Cook(request);
    CHECK(failure.Status == TextureCookStatus::Failed);
    CHECK(failure.Error.find("--no-mips") != std::string::npos);
    CHECK(TextureCookFixture::ReadText(result.Output) == original);
}

TEST_CASE("DDS conversions stay explicit and unavailable operations retain successful files",
          "[asset][dds]")
{
    TextureCookFixture fixture;
    TextureCookFixture::WriteText(fixture.Source("normal.dds"),
                                  MakeDdsFixture("DX10", 16, 4, 4, 3, 1, 83).Bytes);
    TextureCookRequest request{.Source = "normal.dds"};
    request.Overrides.Usage = TextureUsage::Normal;
    const auto result = fixture.Cooker().Cook(request);
    RequireStatus(result, TextureCookStatus::Cooked);
    const auto output = TextureCookFixture::ReadText(result.Output);
    const auto recordPath = fixture.Cooked() / "normal.dds.cook.json";
    const auto record = TextureCookFixture::ReadText(recordPath);
    SECTION("format conversion")
    {
        request.Overrides.Format = TextureCompression::BC7;
    }
    SECTION("normal flip")
    {
        request.Overrides.bFlipNormalY = true;
    }
    const auto failure = fixture.Cooker().Cook(request);
    CHECK(failure.Status == TextureCookStatus::Failed);
    CHECK(failure.Error.find("conversion is not available") != std::string::npos);
    bool warned = false;
    for (const auto& warning : failure.Warnings)
        warned |= warning.find("byte preservation no longer applies") != std::string::npos;
    CHECK(warned);
    CHECK(TextureCookFixture::ReadText(result.Output) == output);
    CHECK(TextureCookFixture::ReadText(recordPath) == record);
}

TEST_CASE("DDS rejects malformed and unsupported headers before saving settings or output",
          "[asset][dds]")
{
    TextureCookFixture fixture;
    auto dds = MakeDdsFixture("DX10", 16, 4, 4, 3, 1, 98);
    const auto source = fixture.Source("broken.dds");
    std::string expected;
    SECTION("magic")
    {
        dds.SetWord(0, 0);
        expected = "magic";
    }
    SECTION("header size")
    {
        dds.SetWord(4, 128);
        expected = "header";
    }
    SECTION("pixel header size")
    {
        dds.SetWord(76, 0);
        expected = "header";
    }
    SECTION("zero width")
    {
        dds.SetWord(16, 0);
        expected = "dimensions";
    }
    SECTION("zero height")
    {
        dds.SetWord(12, 0);
        expected = "dimensions";
    }
    SECTION("mip count")
    {
        dds.SetWord(28, 4);
        expected = "mip count";
    }
    SECTION("volume depth")
    {
        dds.SetWord(24, 2);
        expected = "Volumes";
    }
    SECTION("volume flag")
    {
        dds.SetWord(8, 0x800000);
        expected = "Volumes";
    }
    SECTION("volume caps")
    {
        dds.SetWord(112, 0x200000);
        expected = "Volumes";
    }
    SECTION("uncompressed layout")
    {
        dds.SetWord(80, 0x41);
        expected = "Uncompressed";
    }
    SECTION("conflicting pixel flags")
    {
        dds.SetWord(80, 0x44);
        expected = "pixel layout";
    }
    SECTION("unknown FourCC")
    {
        dds.Bytes.replace(84, 4, "FAIL");
        expected = "FourCC";
    }
    SECTION("1D")
    {
        dds.SetWord(132, 2);
        expected = "1D";
    }
    SECTION("volume dimension")
    {
        dds.SetWord(132, 4);
        expected = "volume";
    }
    SECTION("unknown dimension")
    {
        dds.SetWord(132, 0);
        expected = "2D";
    }
    SECTION("zero array")
    {
        dds.SetWord(140, 0);
        expected = "arraySize";
    }
    SECTION("texture array")
    {
        dds.SetWord(140, 2);
        expected = "arrays";
    }
    SECTION("cube array")
    {
        dds.SetWord(136, 4);
        dds.SetWord(140, 2);
        expected = "cubemap arrays";
    }
    SECTION("unknown misc flags")
    {
        dds.SetWord(136, 8);
        expected = "miscFlag";
    }
    SECTION("alpha mode")
    {
        dds.SetWord(144, 5);
        expected = "alpha mode";
    }
    SECTION("reserved alpha bits")
    {
        dds.SetWord(144, 8);
        expected = "reserved bits";
    }
    SECTION("typeless")
    {
        dds.SetWord(128, 97);
        expected = "Typeless";
    }
    SECTION("HDR")
    {
        dds.SetWord(128, 95);
        expected = "HDR";
    }
    SECTION("truncated 8-bit DDS")
    {
        dds.SetWord(128, 28);
        expected = "payload";
    }
    SECTION("unknown format")
    {
        dds.SetWord(128, 3);
        expected = "Unsupported DX10 format";
    }
    SECTION("partial legacy cube")
    {
        dds = MakeDdsFixture("DXT5", 16, 4, 4, 3, 6);
        dds.SetWord(112, 0x600);
        expected = "Partial";
    }
    SECTION("contradictory DX10 cube")
    {
        dds.SetWord(112, 0xfe00);
        expected = "Conflicting";
    }
    SECTION("face bits without cube")
    {
        dds.SetWord(112, 0x400);
        expected = "faces";
    }
    SECTION("nonsquare cube")
    {
        dds = MakeDdsFixture("DX10", 16, 8, 4, 4, 6, 98);
        expected = "square";
    }
    SECTION("overflowing footprint")
    {
        dds.SetWord(12, std::numeric_limits<uint32_t>::max());
        dds.SetWord(16, std::numeric_limits<uint32_t>::max());
        expected = "overflows";
    }
    SECTION("trailing bytes")
    {
        dds.Bytes += "extra";
        expected = "trailing";
    }
    TextureCookFixture::WriteText(source, dds.Bytes);
    const auto result = fixture.Cooker().Cook({.Source = "broken.dds"});
    CHECK(result.Status == TextureCookStatus::Failed);
    CHECK(result.Error.find(expected) != std::string::npos);
    CHECK(result.Error.find(source.string()) != std::string::npos);
    CHECK_FALSE(fs::exists(fixture.Source("broken.dds.json")));
    CHECK_FALSE(fs::exists(fixture.Cooked()));
}

TEST_CASE("Every truncation of a DDS cubemap is rejected, including discarded mip payloads",
          "[asset][dds]")
{
    TextureCookFixture fixture;
    const auto dds = MakeDdsFixture("DX10", 16, 4, 4, 3, 6, 98);
    const auto source = fixture.Source("truncated.dds");
    for (size_t size = 0; size < dds.Bytes.size(); ++size)
    {
        INFO("file size " << size);
        TextureCookFixture::WriteText(source, std::string_view(dds.Bytes).substr(0, size));
        std::string error;
        CHECK_FALSE(ReadDdsInfo(source, error));
        CHECK_FALSE(error.empty());
    }
    TextureCookRequest request{.Source = "truncated.dds"};
    request.Overrides.bNoMips = true;
    CHECK(fixture.Cooker().Cook(request).Status == TextureCookStatus::Failed);
    CHECK_FALSE(fs::exists(fixture.Source("truncated.dds.json")));
}

TEST_CASE("DDS reading accepts omitted advisory flags and DX10 cube metadata without legacy caps",
          "[asset][dds]")
{
    TextureCookFixture fixture;
    auto dds = MakeDdsFixture("DX10", 16, 4, 4, 3, 6, 98);
    dds.SetWord(8, 0);
    dds.SetWord(108, 0);
    dds.SetWord(112, 0);
    dds.SetWord(20, 12345);
    TextureCookFixture::WriteText(fixture.Source("compatible.dds"), dds.Bytes);
    const auto result = fixture.Cooker().Cook({.Source = "compatible.dds"});
    RequireStatus(result, TextureCookStatus::Cooked);
    const auto texture = OpenKtx(result.Output);
    CHECK(texture->numFaces == 6);
    CheckPayloads(texture.get(), dds);
}

TEST_CASE("Single-pixel DDS textures retain whole blocks with an omitted mip count", "[asset][dds]")
{
    TextureCookFixture fixture;
    for (const uint32_t faces : {1u, 6u})
    {
        auto dds = MakeDdsFixture("DX10", 8, 1, 1, 1, faces, 80, 1);
        dds.SetWord(28, 0);
        TextureCookFixture::WriteText(fixture.Source("tiny.dds"), dds.Bytes);
        const auto result = fixture.Cooker().Cook({.Source = "tiny.dds"});
        RequireStatus(result, TextureCookStatus::Cooked);
        const auto texture = OpenKtx(result.Output);
        CHECK(texture->vkFormat == VK_FORMAT_BC4_UNORM_BLOCK);
        CHECK(texture->numLevels == 1);
        CHECK(texture->numFaces == faces);
        // DDS alpha representation is conditional on an alpha channel being
        // present. Preserve explicit metadata even for formats without alpha.
        CHECK(Metadata(texture.get(), "HikariDdsAlphaMode") == "straight");
        CheckPayloads(texture.get(), dds);
    }
}

TEST_CASE("Explicit DDS data alpha does not acquire an unresolved transparency warning",
          "[asset][dds]")
{
    TextureCookFixture fixture;
    TextureCookFixture::WriteText(fixture.Source("packed.dds"),
                                  MakeDdsFixture("DX10", 16, 8, 8, 2, 1, 98, 4).Bytes);
    std::string error;
    std::vector<std::string> warnings;
    const auto prepared = fixture.Cooker().Prepare({.Source = "packed.dds"}, error, warnings);
    REQUIRE(prepared);
    CHECK(prepared->Settings.bAlphaUsageKnown);
    CHECK(prepared->Dds->AlphaMode == DdsAlphaMode::Custom);
    for (const auto& warning : warnings)
        CHECK(warning.find("alpha usage is unresolved") == std::string::npos);
    CHECK_FALSE(prepared->Settings.Authored.AlphaMode);
}

TEST_CASE("Saving DDS settings checks the validated native metadata before writing", "[asset][dds]")
{
    TextureCookFixture fixture;
    const auto source = fixture.Source("native.dds");
    TextureCookFixture::WriteText(source, MakeDdsFixture("DX10", 16).Bytes);
    const auto cooker = fixture.Cooker();
    std::string error;
    std::vector<std::string> warnings;
    auto prepared = cooker.Prepare({.Source = "native.dds"}, error, warnings);
    REQUIRE(prepared);
    SECTION("intervening source metadata edit")
    {
        TextureCookFixture::WriteText(source, MakeDdsFixture("DX10", 16, 4, 4, 3).Bytes);
    }
    SECTION("tampered native metadata")
    {
        prepared->Dds->Faces = 6;
    }
    CHECK_FALSE(cooker.SaveSettings(*prepared, error));
    CHECK(error.find("changed since preparation") != std::string::npos);
    CHECK_FALSE(fs::exists(fixture.Source("native.dds.json")));
}
