#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_string.hpp>

#include <filesystem>
#include <optional>
#include <string>
#include <vector>

#include <asset/KtxReader.h>
#include <asset/TextureSource.h>

#include "CaptureStream.h"
#include "CookProcess.h"
#include "CookRecord.h"
#include "DdsFixture.h"
#include "TextureCookFixture.h"

using namespace Hikari::Asset;
using Catch::Matchers::ContainsSubstring;

TEST_CASE("Runtime cook logs identify actual work while warm and direct loads stay quiet",
          "[texture-delivery][logging]")
{
    TextureCookFixture files;
    files.WriteImage("with spaces/image.png", 1, 1);
    const TextureSource source(files.Content(), {}, HIKARI_RELEASE_COOKER, 0.5);
    std::string error;
    std::vector<std::string> warnings;
    std::optional<TextureData> texture;
    const auto read = [&](const auto& path)
    { return CaptureStream(stdout, [&] { texture = source.Read(path, error, warnings); }); };
    const auto cold = read("with spaces/image.png");
    INFO(error);
    REQUIRE(texture);
    CHECK_THAT(cold, ContainsSubstring("[Texture Cooker] Cooking texture"));
    CHECK_THAT(cold, ContainsSubstring("with spaces/image.png"));
    CHECK_THAT(cold, ContainsSubstring("at runtime: BC1, quality 0.5"));
    CHECK(cold.find("Cooked:") == std::string::npos);
    CHECK(read("with spaces/image.png").empty());
    REQUIRE(texture);
    CHECK(read("with spaces/image.png.ktx2").empty());
    REQUIRE(texture);
    const auto offline = files.Cooker().Cook({.Source = "with spaces/image.png"});
    INFO(offline.Error);
    REQUIRE(offline.Status == TextureCookStatus::Cooked);
    CHECK(read("with spaces/image.png").empty());
    REQUIRE(texture);
    CHECK(texture->Format == Hikari::Rhi::Format::BC7Srgb);
    files.WriteImage("explicit.png", 1, 1);
    TextureCookFixture::WriteText(files.Source("explicit.png.texture.json"),
                                  R"({"version":1,"format":"bc7"})");
    CHECK_THAT(read("explicit.png"), ContainsSubstring("at runtime: BC7, quality 0.5"));
    REQUIRE(texture);
    CHECK(read("missing.png").empty());
    CHECK_FALSE(texture);
    // Failure diagnostics must still reach acquisition when stdout is inherited.
    files.WriteImage("invalid-normal.png");
    std::vector<uint8_t> pixels(4 * 4 * 4, 255);
    for (size_t i = 0; i < pixels.size(); i += 4)
        pixels[i + 2] = 0;
    REQUIRE(WritePng(pixels, {4u, 4u}, files.Source("invalid-normal.png").string()));
    TextureCookFixture::WriteText(files.Source("invalid-normal.png.texture.json"),
                                  R"({"version":1,"usage":"normal"})");
    CHECK_THAT(read("invalid-normal.png"), ContainsSubstring("at runtime: BC5, quality 0.5"));
    CHECK_FALSE(texture);
    CHECK_THAT(error, ContainsSubstring("positive-Z"));
}

TEST_CASE("Runtime stdout inheritance preserves captured child errors", "[texture-delivery]")
{
    TextureCookFixture files;
    const auto log = files.Root() / "child.log";
    std::string error;
    bool succeeded = false;
    const auto captured = CaptureStream(
        stdout,
        [&]
        {
            succeeded = RunCookProcess(HIKARI_COOK_PROCESS_FIXTURE, {"echo", "runtime cook began"},
                                       log, error, false, true);
        });
    INFO(error);
    REQUIRE(succeeded);
    CHECK_THAT(captured, ContainsSubstring("runtime cook began"));
    CHECK(TextureCookFixture::ReadText(log).empty());
    CHECK_FALSE(RunCookProcess(HIKARI_COOK_PROCESS_FIXTURE, {"fail"}, log, error, false, true));
    CHECK_THAT(error, ContainsSubstring("intentional child process failure"));
}

TEST_CASE("Release cooker delivers CPU-only batch upgrades and direct cooked acquisition",
          "[texture-delivery][quality]")
{
    TextureCookFixture files;
    files.WriteImage("brick.png", 1, 1);
    // Mip filtering has dedicated coverage. This delivery gate needs only the
    // six-face container path, avoiding two extra encoder processes per face.
    files.WriteCubemap(R"("mipmaps":false,)");
    std::filesystem::copy_file(std::filesystem::path(HIKARI_TEXTURE_TEST_DATA_DIR) / "constant.jpg",
                               files.Source("image.jpg"));
    const auto native = MakeDdsFixture("DX10", 16, 4, 4, 3, 1, 83, 3);
    TextureCookFixture::WriteText(files.Source("native.dds"), native.Bytes);
    const auto log = files.Root() / "batch.log";
    std::string error;
    const auto batch = [&](const std::string& quality)
    {
        return RunCookProcess(HIKARI_RELEASE_COOKER,
                              {"cook-all", "--source-root", CookPathUtf8(files.Content()),
                               "--cooked-root", CookPathUtf8(files.Cooked()), "--quality", quality},
                              log, error, false);
    };
    REQUIRE(batch("0.5"));
    CHECK_THAT(TextureCookFixture::ReadText(log),
               ContainsSubstring("Batch: 4 cooked, 0 unchanged, 0 failed"));
    const auto settingsPath = files.Source("brick.png.texture.json");
    const auto settings = TextureCookFixture::ReadText(settingsPath);
    const auto recordPath = files.Cooked() / "brick.png.cook.json";
    const auto firstRecord = ReadCookRecord(recordPath);
    REQUIRE(firstRecord);
    CHECK(firstRecord->EncodingQuality == 0.5);
    const auto brick = ReadKtxTexture(files.Cooked() / "brick.png.ktx2", error);
    INFO(error);
    REQUIRE(brick);
    CHECK(brick->Format == Hikari::Rhi::Format::BC7Srgb);
    const auto retained = ReadKtxTexture(files.Cooked() / "native.dds.ktx2", error);
    REQUIRE(retained);
    CHECK(retained->Format == Hikari::Rhi::Format::BC5Unorm);
    for (size_t i = 0; i < native.Payloads.size(); ++i)
    {
        const auto& source = native.Payloads[i];
        const auto& output = retained->Subresources.at(i);
        const std::string blocks(
            reinterpret_cast<const char*>(retained->Data.data() + output.Offset), output.Size);
        CHECK(blocks == native.Bytes.substr(source.Offset, source.Size));
    }
    const auto cube = ReadKtxTexture(files.Cooked() / "sky/sky.cubemap.ktx2", error);
    REQUIRE(cube);
    CHECK(cube->Format == Hikari::Rhi::Format::BC7Srgb);
    CHECK(cube->Faces == 6);
    REQUIRE(batch("0.5"));
    CHECK_THAT(TextureCookFixture::ReadText(log),
               ContainsSubstring("Batch: 0 cooked, 4 unchanged, 0 failed"));
    REQUIRE(batch("1"));
    CHECK_THAT(TextureCookFixture::ReadText(log),
               ContainsSubstring("Batch: 4 cooked, 0 unchanged, 0 failed"));
    const auto upgraded = ReadCookRecord(recordPath);
    REQUIRE(upgraded);
    CHECK(upgraded->EncodingQuality == 1.0);
    CHECK(TextureCookFixture::ReadText(settingsPath) == settings);
    const auto upgradedBytes = TextureCookFixture::ReadText(recordPath);
    const TextureSource runtime(files.Content(), {}, HIKARI_RELEASE_COOKER, 0.25);
    std::vector<std::string> warnings;
    const auto reused = runtime.Read("brick.png", error, warnings);
    REQUIRE(reused);
    CHECK(reused->Format == Hikari::Rhi::Format::BC7Srgb);
    CHECK(TextureCookFixture::ReadText(recordPath) == upgradedBytes);
    // A packaged KTX2 requires neither its source tree nor the cooker/record.
    const auto package = files.Root() / "package";
    std::filesystem::create_directory(package);
    std::filesystem::copy_file(files.Cooked() / "brick.png.ktx2", package / "brick.ktx2");
    const TextureSource direct(files.Root() / "no sources", package, files.Root() / "no cooker");
    const auto packaged = direct.Read("brick.ktx2", error, warnings);
    INFO(error);
    REQUIRE(packaged);
    CHECK(packaged->Data == reused->Data);
}
