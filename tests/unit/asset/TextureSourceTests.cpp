#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_string.hpp>

#include <filesystem>
#include <future>
#include <string>
#include <vector>

#include <asset/TextureSource.h>

#include "DdsFixture.h"
#include "KtxFixture.h"
#include "TextureCookFixture.h"

using namespace Hikari::Asset;
using Catch::Matchers::ContainsSubstring;

TEST_CASE("Source acquisition uses the Release cooker for cold warm and edited DDS inputs",
          "[asset][acquisition]")
{
    TextureCookFixture files;
    auto dds = MakeDdsFixture("DX10", 8, 12, 12, 4, 1, 71);
    // A CLI success message naming this asset must not be mistaken for the
    // encoder's legacy-fallback diagnostic. The CLI checks its own encoder.
    const auto path = files.Source("with spaces/CPU will be used.dds");
    TextureCookFixture::WriteText(path, dds.Bytes);
    const TextureSource source(files.Content(), {}, HIKARI_RELEASE_COOKER);
    CHECK(source.CookedRoot() == files.Cooked());
    const auto output = files.Cooked() / "with spaces/CPU will be used.dds.ktx2";
    const auto record = files.Cooked() / "with spaces/CPU will be used.dds.cook.json";
    std::string error;
    std::vector<std::string> warnings;
    const auto first = source.Read("with spaces/CPU will be used.dds", error, warnings);
    INFO(error);
    REQUIRE(first);
    CHECK(first->MipLevels == 4);
    CHECK(first->Metadata.Usage == TextureUsage::Color);
    CHECK(std::filesystem::exists(path.string() + ".texture.json"));
    const auto bytes = TextureCookFixture::ReadText(output);
    const auto outputTime = std::filesystem::last_write_time(output);
    const auto recordTime = std::filesystem::last_write_time(record);
    const auto warm = source.Read(path, error, warnings);
    INFO(error);
    REQUIRE(warm);
    CHECK(warm->Data == first->Data);
    CHECK(std::filesystem::last_write_time(output) == outputTime);
    CHECK(std::filesystem::last_write_time(record) == recordTime);
    dds.Bytes.back() ^= 1;
    TextureCookFixture::WriteText(path, dds.Bytes);
    const auto edited = source.Read(path, error, warnings);
    INFO(error);
    REQUIRE(edited);
    CHECK(edited->Data != first->Data);
    TextureCookFixture::WriteText(path.string() + ".texture.json",
                                  R"({"version":1,"usage":"packed","colorSpace":"linear"})");
    const auto settingsEdit = source.Read(path, error, warnings);
    INFO(error);
    REQUIRE(settingsEdit);
    CHECK(settingsEdit->Metadata.Usage == TextureUsage::Packed);
    CHECK(settingsEdit->Data == edited->Data);
    std::filesystem::remove(output);
    REQUIRE(source.Read(path, error, warnings));
    REQUIRE(std::filesystem::is_regular_file(output));
    TextureCookFixture::WriteText(output, "damaged");
    const auto repaired = source.Read(path, error, warnings);
    INFO(error);
    REQUIRE(repaired);
    CHECK(repaired->Data == settingsEdit->Data);
    std::filesystem::remove(record);
    REQUIRE(source.Read(path, error, warnings));
    CHECK(std::filesystem::is_regular_file(record));
    CHECK(bytes != TextureCookFixture::ReadText(output));
}

TEST_CASE("Direct cooked acquisition needs no source files records or cooker",
          "[asset][acquisition]")
{
    TextureCookFixture files;
    const auto root = files.Root() / "custom cooked root";
    const auto path = root / "textures/standalone.ktx2";
    const auto fixture = MakeKtxFixture(142, 8, 8, 4);
    TextureCookFixture::WriteText(path, fixture.Bytes);
    const TextureSource source(files.Root() / "no source root", root,
                               files.Root() / "missing cooker");
    std::string error;
    std::vector<std::string> warnings;
    const auto data = source.Read("textures/standalone.ktx2", error, warnings);
    INFO(error);
    REQUIRE(data);
    CHECK(data->Format == Hikari::Rhi::Format::BC5Snorm);
    CHECK(warnings.empty());
    CHECK(source.ResolvePath(path) == source.ResolvePath("textures/./standalone.ktx2"));
    CHECK(source.SettingsPath(path).empty());
    CHECK(source.SettingsPath(files.Content() / "SKY.CUBEMAP.JSON").filename() ==
          "SKY.CUBEMAP.JSON");
    CHECK_THROWS(source.ResolvePath("../escape.ktx2"));
    CHECK_THROWS(source.ResolvePath("../escape.png"));
    CHECK_FALSE(source.Read("../escape.ktx2", error, warnings));
    CHECK_THAT(error, ContainsSubstring("escapes"));
}

TEST_CASE("Failed source acquisition preserves old output without returning stale blocks",
          "[asset][acquisition]")
{
    TextureCookFixture files;
    const auto dds = MakeDdsFixture("DX10", 8, 8, 8, 4, 1, 71);
    const auto path = files.Source("retained.dds");
    TextureCookFixture::WriteText(path, dds.Bytes);
    const TextureSource source(files.Content(), {}, HIKARI_RELEASE_COOKER);
    std::string error;
    std::vector<std::string> warnings;
    REQUIRE(source.Read(path, error, warnings));
    const auto output = files.Cooked() / "retained.dds.ktx2";
    const auto previous = TextureCookFixture::ReadText(output);
    SECTION("invalid settings")
    {
        TextureCookFixture::WriteText(path.string() + ".texture.json", R"({"version":99})");
        CHECK_FALSE(source.Read(path, error, warnings));
    }
    SECTION("missing shared cooker")
    {
        const TextureSource unavailable(files.Content(), {}, files.Root() / "missing cooker");
        CHECK_FALSE(unavailable.Read(path, error, warnings));
        CHECK_THAT(error, ContainsSubstring("Shared Release texture cooker"));
    }
    SECTION("failed encoder")
    {
        files.WriteImage("negative-normal.png");
        // Uniform (1,1,-1) is outside the agreed positive-Z representation.
        std::vector<uint8_t> pixels(4 * 4 * 4, 255);
        for (size_t i = 0; i < pixels.size(); i += 4)
            pixels[i + 2] = 0;
        REQUIRE(WritePng(pixels, {4u, 4u}, files.Source("negative-normal.png").string()));
        TextureCookFixture::WriteText(files.Source("negative-normal.png.texture.json"),
                                      R"({"version":1,"usage":"normal"})");
        CHECK_FALSE(source.Read("negative-normal.png", error, warnings));
        CHECK_THAT(error, ContainsSubstring("Texture cooking failed"));
    }
    SECTION("unwritable cooked root")
    {
        TextureCookFixture::WriteText(files.Root() / "blocked", "file");
        const TextureSource blocked(files.Content(), files.Root() / "blocked/cooked",
                                    HIKARI_RELEASE_COOKER);
        CHECK_FALSE(blocked.Read(path, error, warnings));
    }
    CHECK_FALSE(error.empty());
    CHECK(TextureCookFixture::ReadText(output) == previous);
}

TEST_CASE("Concurrent source requests share cooker publication and explicit roots",
          "[asset][acquisition]")
{
    TextureCookFixture files;
    const auto dds = MakeDdsFixture("DX10", 16, 8, 8, 4, 6, 98);
    TextureCookFixture::WriteText(files.Source("cube.dds"), dds.Bytes);
    const TextureSource source(files.Content(), files.Root() / "custom outputs",
                               HIKARI_RELEASE_COOKER);
    const auto read = [&]
    {
        std::string error;
        std::vector<std::string> warnings;
        return source.Read("cube.dds", error, warnings);
    };
    auto first = std::async(std::launch::async, read);
    auto second = std::async(std::launch::async, read);
    const auto one = first.get(), two = second.get();
    REQUIRE(one);
    REQUIRE(two);
    CHECK(one->Data == two->Data);
    CHECK(one->Faces == 6);
    CHECK(std::filesystem::is_regular_file(source.CookedRoot() / "cube.dds.cook.json"));
    CHECK_FALSE(std::filesystem::exists(files.Cooked()));
}
