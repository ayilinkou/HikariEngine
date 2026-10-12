#include <catch2/catch_test_macros.hpp>

#include <filesystem>
#include <sstream>
#include <string>
#include <string_view>
#include <vector>

#include "DdsFixture.h"
#include "TextureCookFixture.h"
#include "TextureCookerCli.h"

namespace fs = std::filesystem;

namespace
{
int Run(const std::vector<std::string>& arguments, const fs::path& fallback, std::string& output,
        std::string& error)
{
    std::vector<const char*> argv;
    for (const auto& argument : arguments)
        argv.push_back(argument.c_str());
    std::ostringstream out, err;
    const int code = Hikari::Tools::RunTextureCooker(static_cast<int>(argv.size()), argv.data(),
                                                     fallback, out, err);
    output = out.str();
    error = err.str();
    return code;
}
bool Contains(std::string_view text, std::string_view part)
{
    return text.find(part) != std::string_view::npos;
}
} // namespace

TEST_CASE("Cooker help needs neither content nor a rendering device", "[texture-cooker-cli]")
{
    std::string output, error;
    for (const auto& arguments : {std::vector<std::string>{"cooker", "--help"},
                                  std::vector<std::string>{"cooker", "cook", "--help"},
                                  std::vector<std::string>{"cooker", "cook-all", "--help"}})
    {
        CHECK(Run(arguments, "no-such-content", output, error) == 0);
        CHECK(Contains(output, "Usage: HikariTextureCooker"));
        CHECK(Contains(output, "--flip-normal-y"));
        CHECK(error.empty());
    }
}

TEST_CASE("Cooker CLI rejects malformed arguments and numeric limits", "[texture-cooker-cli]")
{
    std::string output, error;
    for (const auto& arguments :
         {std::vector<std::string>{"cooker"}, std::vector<std::string>{"cooker", "unknown"},
          std::vector<std::string>{"cooker", "cook"},
          std::vector<std::string>{"cooker", "cook", "image.png", "extra.png"},
          std::vector<std::string>{"cooker", "cook", "image.png", "--bogus"},
          std::vector<std::string>{"cooker", "cook", "image.png", "--usage"},
          std::vector<std::string>{"cooker", "cook", "image.png", "--usage", "infer"},
          std::vector<std::string>{"cooker", "cook", "image.png", "--format", "bc6h"},
          std::vector<std::string>{"cooker", "cook", "image.png", "--color-space", "gamma"},
          std::vector<std::string>{"cooker", "cook", "image.png", "--alpha-mode", "blend"},
          std::vector<std::string>{"cooker", "cook", "image.png", "--source-root="},
          std::vector<std::string>{"cooker", "cook", "image.png", "--cooked-root="},
          std::vector<std::string>{"cooker", "cook", "image.png", "--flip-normal-y=false"},
          std::vector<std::string>{"cooker", "cook", "image.png", "--no-mips=true"},
          std::vector<std::string>{"cooker", "cook", "image.png", "--force=false"}})
    {
        INFO(arguments.back());
        CHECK(Run(arguments, "no-such-content", output, error) == 2);
        CHECK_FALSE(error.empty());
    }
    for (const auto value : {"129", "-1", "oops", "18446744073709551616"})
        CHECK(Run({"cooker", "cook", "image.png", "--threads", value}, "no-such-content", output,
                  error) == 2);
    for (const auto value : {"-0.1", "1.1", "nan", "inf", "0.5x", "1e309"})
    {
        CHECK(Run({"cooker", "cook", "image.png", "--alpha-cutoff", value}, "no-such-content",
                  output, error) == 2);
        CHECK(Run({"cooker", "cook", "image.png", "--quality", value}, "no-such-content", output,
                  error) == 2);
        CHECK(Run({"cooker", "cook-all", "--quality", value}, "no-such-content", output, error) ==
              2);
    }
    CHECK(Run({"cooker", "cook-all", "--quality"}, "no-such-content", output, error) == 2);
}

TEST_CASE("Batch CLI rejects texture overrides and succeeds on an empty source tree",
          "[texture-cooker-cli]")
{
    std::string output, error;
    for (const auto& option :
         {std::vector<std::string>{"--usage", "normal"},
          std::vector<std::string>{"--format", "bc7"},
          std::vector<std::string>{"--color-space", "linear"},
          std::vector<std::string>{"--alpha-mode", "opaque"},
          std::vector<std::string>{"--alpha-cutoff", "0"},
          std::vector<std::string>{"--flip-normal-y"}, std::vector<std::string>{"--no-mips"}})
    {
        std::vector<std::string> arguments{"cooker", "cook-all"};
        arguments.insert(arguments.end(), option.begin(), option.end());
        CHECK(Run(arguments, "no-such-content", output, error) == 2);
        CHECK(Contains(error, "per-texture overrides"));
    }
    TextureCookFixture fixture;
    CHECK(Run({"cooker", "cook-all", "--source-root", fixture.Content().string(), "--force",
               "--threads=0", "--quality=0.5"},
              fixture.Content(), output, error) == 0);
    CHECK(Contains(output, "Batch: 0 cooked, 0 unchanged, 0 failed"));
    CHECK_FALSE(fs::exists(fixture.Cooked()));
}

TEST_CASE("Batch CLI reports failures and later successes with warm skips and force",
          "[texture-cooker-cli][texture-batch]")
{
    TextureCookFixture fixture;
    TextureCookFixture::WriteText(fixture.Source("a-broken.png"), "not PNG");
    fixture.WriteImage("b-valid.png");
    fixture.WriteCubemap();
    const std::vector<std::string> arguments{"cooker", "cook-all", "--source-root",
                                             fixture.Content().string(), "--threads=2"};
    std::string output, error;
    CHECK(Run(arguments, fixture.Content(), output, error) == 1);
    CHECK(Contains(output, "Batch: 2 cooked, 0 unchanged, 1 failed"));
    CHECK(Contains(error, "Failed: a-broken.png"));
    CHECK(fs::exists(fixture.Cooked() / "b-valid.png.ktx2"));
    CHECK(fs::exists(fixture.Cooked() / "sky/sky.cubemap.ktx2"));
    CHECK_FALSE(fs::exists(fixture.Cooked() / "sky/right.png.ktx2"));
    CHECK_FALSE(fs::exists(fixture.Source("a-broken.png.json")));
    CHECK(Run(arguments, fixture.Content(), output, error) == 1);
    CHECK(Contains(output, "Batch: 0 cooked, 2 unchanged, 1 failed"));
    auto forced = arguments;
    forced.push_back("--force");
    CHECK(Run(forced, fixture.Content(), output, error) == 1);
    CHECK(Contains(output, "Batch: 2 cooked, 0 unchanged, 1 failed"));
}

TEST_CASE("Batch CLI rejects dry-run and positional inputs before writes",
          "[texture-cooker-cli][texture-batch]")
{
    TextureCookFixture fixture;
    fixture.WriteImage("image.png");
    std::string output, error;
    CHECK(Run({"cooker", "cook-all", "--source-root", fixture.Content().string(), "--dry-run"},
              fixture.Content(), output, error) == 2);
    CHECK(Contains(error, "single-file"));
    CHECK(Run({"cooker", "cook-all", "image.png", "--source-root", fixture.Content().string()},
              fixture.Content(), output, error) == 2);
    CHECK_FALSE(fs::exists(fixture.Source("image.png.json")));
    CHECK_FALSE(fs::exists(fixture.Cooked()));
}

TEST_CASE("CLI dry-run and code prepare identical requests without filesystem writes",
          "[texture-cooker-cli]")
{
    TextureCookFixture fixture;
    fixture.WriteImage("image.png");
    const auto custom = fixture.Root() / "custom cooked";
    std::string output, error;
    const int code =
        Run({"cooker", "cook", "image.png", "--source-root", fixture.Content().string(),
             "--cooked-root", custom.string(), "--usage=normal", "--format=bc7",
             "--color-space=linear", "--flip-normal-y", "--flip-normal-y", "--no-mips", "--force",
             "--threads", "2", "--quality", "0.5", "--dry-run"},
            fixture.Content(), output, error);
    INFO(error);
    REQUIRE(code == 0);
    Hikari::Asset::TextureCookRequest request{.Source = "image.png"};
    request.Overrides.Usage = Hikari::Asset::TextureUsage::Normal;
    request.Overrides.Format = Hikari::Asset::TextureCompression::BC7;
    request.Overrides.ColorSpace = Hikari::Asset::TextureColorSpace::Linear;
    request.Overrides.bFlipNormalY = true;
    request.Overrides.bNoMips = true;
    request.bForce = true;
    request.EncoderThreads = 2;
    request.EncodingQuality = 0.5;
    std::string serviceError;
    std::vector<std::string> warnings;
    const auto prepared = Hikari::Asset::TextureCooker(fixture.Content(), custom)
                              .Prepare(request, serviceError, warnings);
    REQUIRE(prepared);
    CHECK(Contains(output, prepared->Paths.CookedPath.string()));
    CHECK(Contains(output, prepared->Paths.SettingsPath.string()));
    CHECK(Contains(output, "Usage: normal"));
    CHECK(Contains(output, "Format: BC7"));
    CHECK(Contains(output, "Color space: linear"));
    CHECK(Contains(output, "Encoder threads: 2"));
    CHECK(Contains(output, "Encoding quality: 0.5"));
    CHECK(Contains(output, "Force: true"));
    CHECK_FALSE(fs::exists(prepared->Paths.SettingsPath));
    CHECK_FALSE(fs::exists(custom));
}

TEST_CASE("CLI quality flows through cook and cook-all with upgrades and forced downgrades",
          "[texture-cooker-cli][quality]")
{
    TextureCookFixture fixture;
    fixture.WriteImage("image.png");
    std::string output, error;
    const std::vector<std::string> single{
        "cooker", "cook", "image.png", "--source-root", fixture.Content().string(), "--threads=2"};
    auto low = single;
    low.insert(low.end(), {"--quality", "0.5"});
    REQUIRE(Run(low, fixture.Content(), output, error) == 0);
    CHECK(Contains(output, "Cooked: "));
    const auto settings = TextureCookFixture::ReadText(fixture.Source("image.png.json"));
    const auto recordPath = fixture.Cooked() / "image.png.cook.json";
    CHECK(Contains(TextureCookFixture::ReadText(recordPath), "\"encodingQuality\": 0.5"));
    const std::vector<std::string> batch{"cooker", "cook-all", "--source-root",
                                         fixture.Content().string(), "--threads=2"};
    REQUIRE(Run(batch, fixture.Content(), output, error) == 0);
    CHECK(Contains(output, "Batch: 1 cooked, 0 unchanged, 0 failed"));
    CHECK(Contains(TextureCookFixture::ReadText(recordPath), "\"encodingQuality\": 1.0"));
    REQUIRE(Run(low, fixture.Content(), output, error) == 0);
    CHECK(Contains(output, "Unchanged: "));
    auto forced = batch;
    forced.insert(forced.end(), {"--quality=0.5", "--force"});
    REQUIRE(Run(forced, fixture.Content(), output, error) == 0);
    CHECK(Contains(output, "Batch: 1 cooked, 0 unchanged, 0 failed"));
    CHECK(Contains(TextureCookFixture::ReadText(recordPath), "\"encodingQuality\": 0.5"));
    CHECK(TextureCookFixture::ReadText(fixture.Source("image.png.json")) == settings);
}

TEST_CASE("CLI cooks normals and retains saved flips through unchanged and forced requests",
          "[texture-cooker-cli]")
{
    TextureCookFixture fixture;
    fixture.WriteImage("image.png");
    std::string output, error;
    CHECK(Run({"cooker", "cook", "image.png", "--source-root", fixture.Content().string(),
               "--usage=normal", "--flip-normal-y", "--no-mips"},
              fixture.Content(), output, error) == 0);
    CHECK(Contains(output, "Cooked"));
    std::string parseError;
    const auto saved = Hikari::Asset::ParseTextureSettings(
        TextureCookFixture::ReadText(fixture.Source("image.png.json")), "image.png.json",
        parseError);
    REQUIRE(saved);
    CHECK(saved->Usage == Hikari::Asset::TextureUsage::Normal);
    CHECK(saved->bFlipNormalY);
    CHECK_FALSE(saved->bMipmaps);
    CHECK(fs::exists(fixture.Cooked() / "image.png.ktx2"));
    const auto before = TextureCookFixture::ReadText(fixture.Source("image.png.json"));
    const auto cooked = TextureCookFixture::ReadText(fixture.Cooked() / "image.png.ktx2");
    CHECK(Run({"cooker", "cook", "image.png", "--source-root", fixture.Content().string()},
              fixture.Content(), output, error) == 0);
    CHECK(Contains(output, "Unchanged"));
    CHECK(TextureCookFixture::ReadText(fixture.Cooked() / "image.png.ktx2") == cooked);
    CHECK(
        Run({"cooker", "cook", "image.png", "--source-root", fixture.Content().string(), "--force"},
            fixture.Content(), output, error) == 0);
    CHECK(Contains(output, "Cooked"));
    CHECK(TextureCookFixture::ReadText(fixture.Source("image.png.json")) == before);
}

TEST_CASE("CLI masked options validate together before any settings or output writes",
          "[texture-cooker-cli]")
{
    TextureCookFixture fixture;
    fixture.WriteImage("mask.png");
    std::string output, error;
    std::vector<std::string> arguments{"cooker",
                                       "cook",
                                       "mask.png",
                                       "--source-root",
                                       fixture.Content().string(),
                                       "--alpha-mode=mask",
                                       "--alpha-cutoff=0.25",
                                       "--format=bc1",
                                       "--dry-run"};
    CHECK(Run(arguments, fixture.Content(), output, error) == 1);
    CHECK(Contains(error, "--no-mips"));
    arguments.push_back("--no-mips");
    CHECK(Run(arguments, fixture.Content(), output, error) == 0);
    CHECK(Contains(output, "Format: BC1"));
    CHECK_FALSE(fs::exists(fixture.Source("mask.png.json")));
    CHECK_FALSE(fs::exists(fixture.Cooked()));
}

TEST_CASE("CLI masked cooking persists cutoff and no-mips through unchanged requests",
          "[texture-cooker-cli][masked-cook]")
{
    TextureCookFixture fixture;
    fixture.WriteImage("mask.png");
    std::string output, error;
    CHECK(Run({"cooker", "cook", "mask.png", "--source-root", fixture.Content().string(),
               "--alpha-mode=mask", "--alpha-cutoff=0.75", "--format=bc1", "--no-mips",
               "--threads=2"},
              fixture.Content(), output, error) == 0);
    CHECK(Contains(output, "Cooked: "));
    CHECK(fs::exists(fixture.Cooked() / "mask.png.ktx2"));
    const auto settingsText = TextureCookFixture::ReadText(fixture.Source("mask.png.json"));
    std::string parseError;
    const auto saved =
        Hikari::Asset::ParseTextureSettings(settingsText, "mask.png.json", parseError);
    REQUIRE(saved);
    CHECK(saved->AlphaMode == Hikari::Asset::TextureAlphaMode::Mask);
    CHECK(saved->AlphaCutoff == 0.75);
    CHECK_FALSE(saved->bMipmaps);
    CHECK(Run({"cooker", "cook", "mask.png", "--source-root", fixture.Content().string()},
              fixture.Content(), output, error) == 0);
    CHECK(Contains(output, "Unchanged: "));
    CHECK(TextureCookFixture::ReadText(fixture.Source("mask.png.json")) == settingsText);
    CHECK(Run({"cooker", "cook", "mask.png", "--source-root", fixture.Content().string(),
               "--alpha-cutoff=0.5", "--threads=2"},
              fixture.Content(), output, error) == 0);
    CHECK(Contains(output, "Cooked: "));
}

TEST_CASE("CLI cubemaps use descriptor identity and explicit face labels", "[texture-cooker-cli]")
{
    TextureCookFixture fixture;
    fixture.WriteCubemap();
    const auto descriptor = fixture.Source("sky/sky.cubemap.json");
    const auto before = TextureCookFixture::ReadText(descriptor);
    std::string output, error;
    CHECK(Run({"cooker", "cook", "sky/sky.cubemap.json", "--source-root",
               fixture.Content().string(), "--dry-run"},
              fixture.Content(), output, error) == 0);
    CHECK(Contains(output, (fixture.Cooked() / "sky/sky.cubemap.ktx2").make_preferred().string()));
    CHECK(Contains(output, "positiveX: " + fixture.Source("sky/right.png").string()));
    CHECK(Contains(output, "negativeZ: " + fixture.Source("sky/front.png").string()));
    CHECK(TextureCookFixture::ReadText(descriptor) == before);
    CHECK_FALSE(fs::exists(fixture.Cooked()));
}

TEST_CASE("CLI missing sources and blocked output roots return operational failures",
          "[texture-cooker-cli]")
{
    TextureCookFixture fixture;
    fixture.WriteImage("image.png");
    std::string output, error;
    CHECK(Run({"cooker", "cook", "missing.png", "--source-root", fixture.Content().string()},
              fixture.Content(), output, error) == 1);
    CHECK(Contains(error, "Source file was not found"));
    CHECK(Contains(error, fixture.Source("missing.png").string()));
    const auto blocked = fixture.Root() / "blocked";
    TextureCookFixture::WriteText(blocked, "keep");
    CHECK(Run({"cooker", "cook", "image.png", "--source-root", fixture.Content().string(),
               "--cooked-root", blocked.string()},
              fixture.Content(), output, error) == 1);
    CHECK(Contains(error, "cooked-root"));
    CHECK_FALSE(fs::exists(fixture.Source("image.png.json")));
}

TEST_CASE("CLI DDS cooking and preview share native metadata and incremental records",
          "[texture-cooker-cli][dds]")
{
    TextureCookFixture fixture;
    const auto dds = MakeDdsFixture("DX10", 16, 4, 4, 3, 6, 99);
    TextureCookFixture::WriteText(fixture.Source("sky.dds"), dds.Bytes);
    std::vector<std::string> args{
        "cooker", "cook", "sky.dds", "--source-root", fixture.Content().string(), "--dry-run"};
    std::string output, error;
    CHECK(Run(args, fixture.Content(), output, error) == 0);
    CHECK(Contains(output, "Format: BC7"));
    CHECK(Contains(output, "Color space: srgb"));
    CHECK(Contains(output, "DDS mip levels: 3"));
    CHECK(Contains(output, "DDS faces: 6"));
    CHECK_FALSE(fs::exists(fixture.Cooked()));
    CHECK_FALSE(fs::exists(fixture.Source("sky.dds.json")));
    args.pop_back();
    CHECK(Run(args, fixture.Content(), output, error) == 0);
    CHECK(Contains(output, "Cooked: "));
    CHECK(fs::exists(fixture.Cooked() / "sky.dds.ktx2"));
    CHECK(fs::exists(fixture.Cooked() / "sky.dds.cook.json"));
    CHECK(Run(args, fixture.Content(), output, error) == 0);
    CHECK(Contains(output, "Unchanged: "));
    args.push_back("--force");
    CHECK(Run(args, fixture.Content(), output, error) == 0);
    CHECK(Contains(output, "Cooked: "));
    args.push_back("--no-mips");
    CHECK(Run(args, fixture.Content(), output, error) == 0);
    std::string parseError;
    const auto saved = Hikari::Asset::ParseTextureSettings(
        TextureCookFixture::ReadText(fixture.Source("sky.dds.json")), "sky.dds.json", parseError);
    REQUIRE(saved);
    CHECK_FALSE(saved->bMipmaps);
}

TEST_CASE("CLI incomplete DDS chains generate mips and allow base-only preservation",
          "[texture-cooker-cli][dds]")
{
    TextureCookFixture fixture;
    TextureCookFixture::WriteText(fixture.Source("partial.dds"),
                                  MakeDdsFixture("DXT5", 16, 8, 8, 2).Bytes);
    std::vector<std::string> args{"cooker", "cook", "partial.dds", "--source-root",
                                  fixture.Content().string()};
    std::string output, error;
    CHECK(Run(args, fixture.Content(), output, error) == 0);
    CHECK(Contains(output, "Cooked: "));
    CHECK(Contains(error, "does not preserve alpha-test coverage"));
    CHECK(Contains(error, "--no-mips"));
    CHECK(fs::exists(fixture.Cooked() / "partial.dds.ktx2"));
    args.push_back("--no-mips");
    CHECK(Run(args, fixture.Content(), output, error) == 0);
    CHECK(Contains(output, "Cooked: "));
}

TEST_CASE("CLI color and scalar images cook real full and base-only KTX2 outputs",
          "[texture-cooker-cli]")
{
    TextureCookFixture fixture;
    fixture.WriteImage("color.png", 7, 5);
    std::string output, error;
    std::vector<std::string> args{
        "cooker", "cook", "color.png", "--source-root", fixture.Content().string(), "--threads=2"};
    CHECK(Run(args, fixture.Content(), output, error) == 0);
    CHECK(Contains(output, "Cooked: "));
    CHECK(fs::exists(fixture.Cooked() / "color.png.ktx2"));
    CHECK(Run(args, fixture.Content(), output, error) == 0);
    CHECK(Contains(output, "Unchanged: "));
    args.push_back("--usage=scalar");
    args.push_back("--no-mips");
    CHECK(Run(args, fixture.Content(), output, error) == 0);
    CHECK(Contains(output, "Cooked: "));
    std::string parseError;
    const auto saved = Hikari::Asset::ParseTextureSettings(
        TextureCookFixture::ReadText(fixture.Source("color.png.json")), "color.png.json",
        parseError);
    REQUIRE(saved);
    CHECK(saved->Usage == Hikari::Asset::TextureUsage::Scalar);
    CHECK_FALSE(saved->bMipmaps);
}
