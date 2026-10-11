#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <atomic>
#include <barrier>
#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <future>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

#include <nlohmann/json.hpp>

#include <asset/TextureCooker.h>

#include "CookHash.h"
#include "CookLock.h"
#include "CookPublication.h"
#include "CookRecord.h"
#include "KtxFormat.h"
#include "TextureCookFixture.h"
#include "TextureCookKtxFixture.h"

using namespace Hikari::Asset;
namespace fs = std::filesystem;

namespace
{
static_assert(uint32_t(KtxFormat::BC1RgbUnorm) == VK_FORMAT_BC1_RGB_UNORM_BLOCK);
static_assert(uint32_t(KtxFormat::BC1RgbSrgb) == VK_FORMAT_BC1_RGB_SRGB_BLOCK);
static_assert(uint32_t(KtxFormat::BC1RgbaUnorm) == VK_FORMAT_BC1_RGBA_UNORM_BLOCK);
static_assert(uint32_t(KtxFormat::BC1RgbaSrgb) == VK_FORMAT_BC1_RGBA_SRGB_BLOCK);
static_assert(uint32_t(KtxFormat::BC2Unorm) == VK_FORMAT_BC2_UNORM_BLOCK);
static_assert(uint32_t(KtxFormat::BC2Srgb) == VK_FORMAT_BC2_SRGB_BLOCK);
static_assert(uint32_t(KtxFormat::BC3Unorm) == VK_FORMAT_BC3_UNORM_BLOCK);
static_assert(uint32_t(KtxFormat::BC3Srgb) == VK_FORMAT_BC3_SRGB_BLOCK);
static_assert(uint32_t(KtxFormat::BC4Unorm) == VK_FORMAT_BC4_UNORM_BLOCK);
static_assert(uint32_t(KtxFormat::BC4Snorm) == VK_FORMAT_BC4_SNORM_BLOCK);
static_assert(uint32_t(KtxFormat::BC5Unorm) == VK_FORMAT_BC5_UNORM_BLOCK);
static_assert(uint32_t(KtxFormat::BC5Snorm) == VK_FORMAT_BC5_SNORM_BLOCK);
static_assert(uint32_t(KtxFormat::BC7Unorm) == VK_FORMAT_BC7_UNORM_BLOCK);
static_assert(uint32_t(KtxFormat::BC7Srgb) == VK_FORMAT_BC7_SRGB_BLOCK);

void RequireStatus(const TextureCookResult& result, TextureCookStatus status)
{
    INFO(result.Error);
    REQUIRE(result.Status == status);
}

std::string ShellQuote(std::string_view value)
{
#ifdef _WIN32
    return "\"" + std::string(value) + "\"";
#else
    std::string quoted = "'";
    for (const auto ch : value)
        quoted += ch == '\'' ? "'\\''" : std::string(1, ch);
    return quoted + "'";
#endif
}

int RunWorker(const TextureCookFixture& fixture, std::string_view mode)
{
    std::string command = ShellQuote(HIKARI_TEXTURE_COOK_WORKER) + " " +
                          ShellQuote(fixture.Content().string()) + " " + ShellQuote("image.png") +
                          " " + ShellQuote(mode);
#ifdef _WIN32
    // cmd.exe strips its outer pair of quotes before parsing the executable.
    command = "\"" + command + "\"";
#endif
    return std::system(command.c_str());
}

fs::path TransactionPath(const TextureCookFixture& fixture)
{
    return fixture.Cooked() / "image.png.ktx2.transaction";
}
} // namespace

TEST_CASE("Cooking retains locks in the dedicated tree through warm skips",
          "[asset][texture-cooker][incremental]")
{
    TextureCookFixture fixture;
    fixture.WriteImage("nested/image.png");
    const TextureCooker cooker(fixture.Content(), fixture.Root() / "custom cooked");
    const TextureCookRequest request{.Source = "nested/image.png"};
    const auto encoder = TextureCookFixtureEncoder();
    RequireStatus(cooker.Cook(request, encoder), TextureCookStatus::Cooked);
    const auto root = fixture.Root() / "custom cooked";
    const auto lock = root / ".locks/nested/image.png.ktx2.lock";
    REQUIRE(fs::is_regular_file(lock));
    CHECK(fs::file_size(lock) == 0);
    CHECK_FALSE(fs::exists(root / "nested/image.png.ktx2.lock"));
    RequireStatus(cooker.Cook(request, encoder), TextureCookStatus::Skipped);
    CHECK(fs::is_regular_file(lock));
    CHECK_FALSE(fs::exists(root / "nested/image.png.ktx2.lock"));
}

TEST_CASE("Cook content hashes match SHA-256 vectors across streaming block boundaries",
          "[asset][texture-cooker][incremental]")
{
    CHECK(HashCookText("") == "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855");
    CHECK(HashCookText("abc") ==
          "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad");
    CHECK(HashCookText("abcdbcdecdefdefgefghfghighijhijkijkljklmklmnlmnomnopnopq") ==
          "248d6a61d20638b8e5c026930c3e6039a33ce45964ff2167f6ecedd419db06c1");
    CookHash streamed;
    const std::string chunk(1000, 'a');
    for (int i = 0; i < 1000; ++i)
        streamed.Update({reinterpret_cast<const uint8_t*>(chunk.data()), chunk.size()});
    CHECK(streamed.Finish() == "cdc76e5c9914fb9281a1c7e284d73e67f1809a48a497200e046d39ccc7112cd0");
    TextureCookFixture fixture;
    const std::string binary(65539, '\0');
    TextureCookFixture::WriteText(fixture.Source("bytes"), binary);
    std::string error;
    CHECK(HashCookFile(fixture.Source("bytes"), error) == HashCookText(binary));
    CHECK_FALSE(HashCookFile(fixture.Source("missing"), error));
}

TEST_CASE("Unchanged cooks verify output hashes and preserve settings and record bytes",
          "[asset][texture-cooker][incremental]")
{
    TextureCookFixture fixture;
    fixture.WriteImage("image.png", 3, 5);
    auto encoder = TextureCookFixtureEncoder();
    int calls = 0;
    const auto encode = encoder.Encode;
    encoder.Encode = [&](const auto& prepared, const auto& output, auto& error)
    {
        ++calls;
        return encode(prepared, output, error);
    };
    const auto cooker = fixture.Cooker();
    TextureCookRequest request{.Source = "image.png"};
    RequireStatus(cooker.Cook(request, encoder), TextureCookStatus::Cooked);
    const auto recordPath = fixture.Cooked() / "image.png.cook.json";
    const auto record = TextureCookFixture::ReadText(recordPath);
    const auto parsed = ReadCookRecord(recordPath);
    REQUIRE(parsed);
    CHECK(parsed->Versions.Compressonator == "4.5.52");
    CHECK_FALSE(parsed->Versions.Ktx.empty());
    CHECK_FALSE(parsed->Versions.ImageDecoder.empty());
    const auto settingsPath = fixture.Source("image.png.json");
    const auto settings = TextureCookFixture::ReadText(settingsPath);
    fs::last_write_time(fixture.Source("image.png"),
                        fs::last_write_time(fixture.Source("image.png")) + std::chrono::hours(1));
    TextureCookFixture::WriteText(settingsPath, " \n" + settings + "\n ");
    request.EncoderThreads = 1;
    RequireStatus(cooker.Cook(request, encoder), TextureCookStatus::Skipped);
    CHECK(calls == 1);
    CHECK(TextureCookFixture::ReadText(recordPath) == record);
    CHECK(TextureCookFixture::ReadText(settingsPath) == " \n" + settings + "\n ");
    // Authored auto and explicit defaults have the same effective recipe.
    TextureCookFixture::WriteText(settingsPath,
                                  R"({"format":"bc7","version":1,"colorSpace":"srgb"})");
    RequireStatus(cooker.Cook(request, encoder), TextureCookStatus::Skipped);
    CHECK(calls == 1);
    request.bForce = true;
    RequireStatus(cooker.Cook(request, encoder), TextureCookStatus::Cooked);
    CHECK(calls == 2);
    CHECK(TextureCookFixture::ReadText(recordPath) == record);
}

TEST_CASE("Source, effective settings and every pipeline dependency identity invalidate cooks",
          "[asset][texture-cooker][incremental]")
{
    TextureCookFixture fixture;
    fixture.WriteImage("image.png");
    auto encoder = TextureCookFixtureEncoder();
    const auto cooker = fixture.Cooker();
    TextureCookRequest request{.Source = "image.png"};
    RequireStatus(cooker.Cook(request, encoder), TextureCookStatus::Cooked);
    const auto recordPath = fixture.Cooked() / "image.png.cook.json";
    const auto before = ReadCookRecord(recordPath);
    REQUIRE(before);
    SECTION("source bytes with unchanged dimensions and timestamp")
    {
        const auto source = fixture.Source("image.png");
        const auto time = fs::last_write_time(source);
        TextureCookFixture::WriteText(source, TextureCookFixture::ReadText(source) + "extra bytes");
        fs::last_write_time(source, time);
    }
    SECTION("settings")
    {
        request.Overrides.Usage = TextureUsage::Normal;
        request.Overrides.bFlipNormalY = true;
    }
    SECTION("pipeline")
    {
        encoder.Versions.Pipeline += "-changed";
    }
    SECTION("encoder")
    {
        encoder.Versions.Compressonator += "-changed";
    }
    SECTION("container")
    {
        encoder.Versions.Ktx += "-changed";
    }
    SECTION("decoder")
    {
        encoder.Versions.ImageDecoder += "-changed";
    }
    RequireStatus(cooker.Cook(request, encoder), TextureCookStatus::Cooked);
    const auto after = ReadCookRecord(recordPath);
    REQUIRE(after);
    CHECK(before->Fingerprint != after->Fingerprint);
    request.Overrides = {};
    RequireStatus(cooker.Cook(request, encoder), TextureCookStatus::Skipped);
}

TEST_CASE("Missing or corrupt outputs and records recook instead of claiming freshness",
          "[asset][texture-cooker][incremental]")
{
    TextureCookFixture fixture;
    fixture.WriteImage("image.png");
    const auto cooker = fixture.Cooker();
    const auto encoder = TextureCookFixtureEncoder();
    const TextureCookRequest request{.Source = "image.png"};
    RequireStatus(cooker.Cook(request, encoder), TextureCookStatus::Cooked);
    const auto output = fixture.Cooked() / "image.png.ktx2";
    const auto recordPath = fixture.Cooked() / "image.png.cook.json";
    const auto recordBytes = TextureCookFixture::ReadText(recordPath);
    const auto outputBytes = TextureCookFixture::ReadText(output);
    SECTION("missing output")
    {
        fs::remove(output);
    }
    SECTION("damaged output with unchanged size")
    {
        auto damaged = outputBytes;
        damaged.back() ^= 1;
        TextureCookFixture::WriteText(output, damaged);
    }
    SECTION("missing record")
    {
        fs::remove(recordPath);
    }
    SECTION("malformed record")
    {
        TextureCookFixture::WriteText(recordPath, "interrupted JSON");
    }
    SECTION("stale fingerprint")
    {
        auto json = nlohmann::json::parse(recordBytes);
        json["fingerprint"] = std::string(64, 'a');
        TextureCookFixture::WriteText(recordPath, json.dump());
    }
    SECTION("invalid digest")
    {
        auto json = nlohmann::json::parse(recordBytes);
        json["outputHash"] = "bad hash";
        TextureCookFixture::WriteText(recordPath, json.dump());
    }
    SECTION("unsupported record version")
    {
        auto json = nlohmann::json::parse(recordBytes);
        json["version"] = 2;
        TextureCookFixture::WriteText(recordPath, json.dump());
    }
    SECTION("damaged dependency identity")
    {
        auto json = nlohmann::json::parse(recordBytes);
        json["versions"]["ktx"] = "incorrect version";
        TextureCookFixture::WriteText(recordPath, json.dump());
    }
    SECTION("duplicate fields")
    {
        TextureCookFixture::WriteText(recordPath, "{\"version\":1," + recordBytes.substr(1));
    }
    RequireStatus(cooker.Cook(request, encoder), TextureCookStatus::Cooked);
    CHECK(TextureCookFixture::ReadText(output) == outputBytes);
    CHECK(TextureCookFixture::ReadText(recordPath) == recordBytes);
    RequireStatus(cooker.Cook(request, encoder), TextureCookStatus::Skipped);
}

TEST_CASE("Cook output validation releases files after success and malformed input",
          "[asset][texture-cooker][incremental]")
{
    TextureCookFixture fixture;
    fixture.WriteImage("image.png");
    std::string error;
    std::vector<std::string> warnings;
    const auto prepared = fixture.Cooker().Prepare({.Source = "image.png"}, error, warnings);
    REQUIRE(prepared);
    const auto output = fixture.Root() / "validation.ktx2";
    REQUIRE(WriteTextureCookKtxFixture(*prepared, output, error));
    bool bExpectedValid = true;
    SECTION("valid output") {}
    SECTION("short header")
    {
        TextureCookFixture::WriteText(output, "not KTX2");
        bExpectedValid = false;
    }
    SECTION("invalid complete header")
    {
        auto bytes = TextureCookFixture::ReadText(output);
        bytes.front() = 0;
        TextureCookFixture::WriteText(output, bytes);
        bExpectedValid = false;
    }
    SECTION("truncated payload")
    {
        fs::resize_file(output, fs::file_size(output) - 1);
        bExpectedValid = false;
    }
    CHECK(ValidateCookOutput(*prepared, output, error) == bExpectedValid);
#ifndef _WIN32
    // Linux can unlink an open file, so removal alone would miss the handle leak.
    for (const auto& descriptor : fs::directory_iterator("/proc/self/fd"))
    {
        std::error_code ec;
        const auto target = fs::read_symlink(descriptor.path(), ec);
        if (!ec)
            CHECK(target != output);
    }
#endif
    CHECK(fs::remove(output));
}

TEST_CASE("Failed encoding and validation retain the last successful asset and record",
          "[asset][texture-cooker][incremental]")
{
    TextureCookFixture fixture;
    fixture.WriteImage("image.png");
    const auto cooker = fixture.Cooker();
    auto encoder = TextureCookFixtureEncoder();
    TextureCookRequest request{.Source = "image.png", .bForce = true};
    RequireStatus(cooker.Cook(request, encoder), TextureCookStatus::Cooked);
    const auto output = fixture.Cooked() / "image.png.ktx2";
    const auto record = fixture.Cooked() / "image.png.cook.json";
    const auto outputBytes = TextureCookFixture::ReadText(output);
    const auto recordBytes = TextureCookFixture::ReadText(record);
    SECTION("failed encoder after partial write")
    {
        encoder.Encode = [](const auto&, const auto& temporary, auto& error)
        {
            TextureCookFixture::WriteText(temporary, "partial output");
            error = "intentional encoder failure";
            return false;
        };
    }
    SECTION("exception")
    {
        encoder.Encode = [](const auto&, const auto&, auto&) -> bool
        { throw std::runtime_error("encoder exception"); };
    }
    SECTION("corrupt output reported as success")
    {
        encoder.Encode = [](const auto&, const auto& temporary, auto&)
        {
            TextureCookFixture::WriteText(temporary, "not KTX2");
            return true;
        };
    }
    SECTION("wrong format")
    {
        encoder.Encode = [](auto prepared, const auto& temporary, auto& error)
        {
            prepared.Settings.ColorSpace = TextureColorSpace::Linear;
            return WriteTextureCookKtxFixture(prepared, temporary, error);
        };
    }
    SECTION("wrong dimensions")
    {
        encoder.Encode = [](auto prepared, const auto& temporary, auto& error)
        {
            prepared.ImageMetadata.front().Extent.Width = 8;
            return WriteTextureCookKtxFixture(prepared, temporary, error);
        };
    }
    SECTION("incomplete mip chain")
    {
        encoder.Encode = [](auto prepared, const auto& temporary, auto& error)
        {
            prepared.Settings.Authored.bMipmaps = false;
            return WriteTextureCookKtxFixture(prepared, temporary, error);
        };
    }
    SECTION("truncated payload")
    {
        encoder.Encode = [](const auto& prepared, const auto& temporary, auto& error)
        {
            if (!WriteTextureCookKtxFixture(prepared, temporary, error))
                return false;
            fs::resize_file(temporary, fs::file_size(temporary) - 1);
            return true;
        };
    }
    SECTION("source edited during encoding")
    {
        encoder.Encode = [&](const auto& prepared, const auto& temporary, auto& error)
        {
            TextureCookFixture::WriteText(
                fixture.Source("image.png"),
                TextureCookFixture::ReadText(fixture.Source("image.png")) + "edited");
            return WriteTextureCookKtxFixture(prepared, temporary, error);
        };
    }
    SECTION("settings edited during encoding")
    {
        encoder.Encode = [&](const auto& prepared, const auto& temporary, auto& error)
        {
            TextureCookFixture::WriteText(fixture.Source("image.png.json"),
                                          R"({"version":1,"usage":"packed"})");
            return WriteTextureCookKtxFixture(prepared, temporary, error);
        };
    }
    const auto result = cooker.Cook(request, encoder);
    CHECK(result.Status == TextureCookStatus::Failed);
    CHECK_FALSE(result.Error.empty());
    CHECK(TextureCookFixture::ReadText(output) == outputBytes);
    CHECK(TextureCookFixture::ReadText(record) == recordBytes);
    CHECK_FALSE(fs::exists(TransactionPath(fixture)));
}

TEST_CASE(
    "Cubemap fingerprints cover every face and its assignment but ignore descriptor whitespace",
    "[asset][texture-cooker][incremental]")
{
    TextureCookFixture fixture;
    fixture.WriteCubemap();
    const auto cooker = fixture.Cooker();
    const auto encoder = TextureCookFixtureEncoder();
    const TextureCookRequest request{.Source = "sky/sky.cubemap.json"};
    RequireStatus(cooker.Cook(request, encoder), TextureCookStatus::Cooked);
    const auto descriptor = fixture.Source("sky/sky.cubemap.json");
    const auto json = TextureCookFixture::ReadText(descriptor);
    TextureCookFixture::WriteText(descriptor, "\n " + json + " \n");
    RequireStatus(cooker.Cook(request, encoder), TextureCookStatus::Skipped);
    for (const auto face : {"right", "left", "top", "bottom", "back", "front"})
    {
        const auto path = fixture.Source(std::string("sky/") + face + ".png");
        TextureCookFixture::WriteText(path,
                                      TextureCookFixture::ReadText(path) + "changed bytes " + face);
        RequireStatus(cooker.Cook(request, encoder), TextureCookStatus::Cooked);
        RequireStatus(cooker.Cook(request, encoder), TextureCookStatus::Skipped);
    }
    // Swap assignments of two byte-identical images: identities must still change.
    fs::copy_file(fixture.Source("sky/right.png"), fixture.Source("sky/left.png"),
                  fs::copy_options::overwrite_existing);
    RequireStatus(cooker.Cook(request, encoder), TextureCookStatus::Cooked);
    auto swapped = nlohmann::json::parse(json);
    std::swap(swapped["faces"]["positiveX"], swapped["faces"]["negativeX"]);
    TextureCookFixture::WriteText(descriptor, swapped.dump());
    RequireStatus(cooker.Cook(request, encoder), TextureCookStatus::Cooked);
    auto wrongShape = encoder;
    wrongShape.Encode = [](auto prepared, const auto& temporary, auto& error)
    {
        prepared.Descriptor.reset();
        return WriteTextureCookKtxFixture(prepared, temporary, error);
    };
    auto forced = request;
    forced.bForce = true;
    RequireStatus(cooker.Cook(forced, wrongShape), TextureCookStatus::Failed);
    RequireStatus(cooker.Cook(request, encoder), TextureCookStatus::Skipped);
}

TEST_CASE("Concurrent threads cook an output once and reread settings after waiting",
          "[asset][texture-cooker][incremental]")
{
    TextureCookFixture fixture;
    fixture.WriteImage("image.png");
    std::atomic<int> calls = 0;
    auto encoder = TextureCookFixtureEncoder();
    encoder.Encode = [&](const auto& prepared, const auto& output, auto& error)
    {
        ++calls;
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
        return WriteTextureCookKtxFixture(prepared, output, error);
    };
    const auto cooker = fixture.Cooker();
    std::barrier start(4);
    std::vector<std::future<TextureCookResult>> futures;
    for (int i = 0; i < 4; ++i)
        futures.push_back(std::async(std::launch::async,
                                     [&]
                                     {
                                         start.arrive_and_wait();
                                         return cooker.Cook({.Source = "image.png"}, encoder);
                                     }));
    int cooked = 0, skipped = 0;
    for (auto& future : futures)
    {
        const auto result = future.get();
        INFO(result.Error);
        CHECK(result.Status != TextureCookStatus::Failed);
        cooked += result.Status == TextureCookStatus::Cooked;
        skipped += result.Status == TextureCookStatus::Skipped;
    }
    CHECK(calls == 1);
    CHECK(cooked == 1);
    CHECK(skipped == 3);
}

TEST_CASE("Independent cooker processes serialize on one output",
          "[asset][texture-cooker][incremental]")
{
    TextureCookFixture fixture;
    fixture.WriteImage("image.png");
    std::barrier start(4);
    std::vector<std::future<int>> futures;
    for (int i = 0; i < 4; ++i)
        futures.push_back(std::async(std::launch::async,
                                     [&]
                                     {
                                         start.arrive_and_wait();
                                         return RunWorker(fixture, "cook");
                                     }));
    for (auto& future : futures)
        CHECK(future.get() == 0);
    CHECK(TextureCookFixture::ReadText(fixture.Cooked() / "image.png.ktx2.attempts") ==
          "encoded\n");
    RequireStatus(fixture.Cooker().Cook({.Source = "image.png"}, TextureCookFixtureEncoder()),
                  TextureCookStatus::Skipped);
}

TEST_CASE(
    "Abrupt process termination recovers each publication boundary and releases the output lock",
    "[asset][texture-cooker][incremental]")
{
    TextureCookFixture fixture;
    fixture.WriteImage("image.png");
    const auto cooker = fixture.Cooker();
    const auto encoder = TextureCookFixtureEncoder();
    const TextureCookRequest request{.Source = "image.png"};
    RequireStatus(cooker.Cook(request, encoder), TextureCookStatus::Cooked);
    const auto output = fixture.Cooked() / "image.png.ktx2";
    const auto record = fixture.Cooked() / "image.png.cook.json";
    const auto originalOutput = TextureCookFixture::ReadText(output);
    const auto originalRecord = TextureCookFixture::ReadText(record);
    std::string mode;
    SECTION("during encoding")
    {
        // A different input prevents the worker from skipping the encode callback.
        TextureCookFixture::WriteText(fixture.Source("image.png"),
                                      TextureCookFixture::ReadText(fixture.Source("image.png")) +
                                          "edited");
        mode = "interrupt-encoding";
    }
    SECTION("after staging before either public write")
    {
        mode = "interrupt-staged";
    }
    SECTION("between output and record writes")
    {
        mode = "interrupt-output";
    }
    SECTION("after both writes before cleanup")
    {
        mode = "interrupt-record";
    }
    CHECK(RunWorker(fixture, mode) != 0);
    REQUIRE(fs::exists(TransactionPath(fixture)));
    std::string error;
    std::vector<std::string> warnings;
    const auto prepared = cooker.Prepare(request, error, warnings);
    REQUIRE(prepared);
    {
        const auto lock = CookLock::Acquire(prepared->Paths, error);
        REQUIRE(lock);
        REQUIRE(CookPublication::Recover(prepared->Paths, error));
    }
    CHECK_FALSE(fs::exists(TransactionPath(fixture)));
    if (mode != "interrupt-record")
    {
        CHECK(TextureCookFixture::ReadText(output) == originalOutput);
        CHECK(TextureCookFixture::ReadText(record) == originalRecord);
    }
    else
    {
        CHECK(TextureCookFixture::ReadText(output) != originalOutput);
        auto replacement = encoder;
        replacement.Versions.Pipeline += "-replacement";
        RequireStatus(cooker.Cook(request, replacement), TextureCookStatus::Skipped);
    }
    const auto expected = mode == "interrupt-encoding" || mode == "interrupt-record"
                              ? TextureCookStatus::Cooked
                              : TextureCookStatus::Skipped;
    RequireStatus(cooker.Cook(request, encoder), expected);
}

TEST_CASE("An interrupted first publication leaves no output claiming a successful cook",
          "[asset][texture-cooker][incremental]")
{
    TextureCookFixture fixture;
    fixture.WriteImage("image.png");
    CHECK(RunWorker(fixture, "interrupt-output") != 0);
    REQUIRE(fs::exists(fixture.Cooked() / "image.png.ktx2"));
    const auto result = fixture.Cooker().Cook({.Source = "image.png"}, TextureCookEncoder{});
    CHECK(result.Status == TextureCookStatus::Failed);
    CHECK_FALSE(fs::exists(fixture.Cooked() / "image.png.ktx2"));
    CHECK_FALSE(fs::exists(fixture.Cooked() / "image.png.cook.json"));
    CHECK_FALSE(fs::exists(TransactionPath(fixture)));
    RequireStatus(fixture.Cooker().Cook({.Source = "image.png"}, TextureCookFixtureEncoder()),
                  TextureCookStatus::Cooked);
}

TEST_CASE("Publication failures restore both previous files and recovery is idempotent",
          "[asset][texture-cooker][incremental]")
{
    TextureCookFixture fixture;
    fixture.WriteImage("image.png");
    const auto cooker = fixture.Cooker();
    const TextureCookRequest request{.Source = "image.png"};
    auto encoder = TextureCookFixtureEncoder(19);
    RequireStatus(cooker.Cook(request, encoder), TextureCookStatus::Cooked);
    const auto output = fixture.Cooked() / "image.png.ktx2";
    const auto recordPath = fixture.Cooked() / "image.png.cook.json";
    const auto outputBytes = TextureCookFixture::ReadText(output);
    const auto recordBytes = TextureCookFixture::ReadText(recordPath);
    std::string error;
    std::vector<std::string> warnings;
    const auto prepared = cooker.Prepare(request, error, warnings);
    REQUIRE(prepared);
    const auto lock = CookLock::Acquire(prepared->Paths, error);
    REQUIRE(lock);
    const auto publication = CookPublication::Begin(prepared->Paths, error);
    REQUIRE(publication);
    REQUIRE(WriteTextureCookKtxFixture(*prepared, publication->OutputPath(), error, 29));
    encoder.Versions.Pipeline += "-replacement";
    const auto fingerprint = FingerprintTextureCook(*prepared, encoder.Versions, error);
    const auto outputHash = HashCookFile(publication->OutputPath(), error);
    REQUIRE(fingerprint);
    REQUIRE(outputHash);
    REQUIRE(publication->Stage({*fingerprint, *outputHash, encoder.Versions}, error));
    SECTION("output replacement fails")
    {
        fs::remove(publication->OutputPath());
        CHECK_FALSE(publication->PublishOutput(error));
    }
    SECTION("record replacement fails after successful output replacement")
    {
        REQUIRE(publication->PublishOutput(error));
        CHECK(TextureCookFixture::ReadText(output) != outputBytes);
        fs::remove(TransactionPath(fixture) / "record.json");
        CHECK_FALSE(publication->PublishRecord(error));
    }
    REQUIRE(CookPublication::Recover(prepared->Paths, error));
    REQUIRE(CookPublication::Recover(prepared->Paths, error));
    CHECK(TextureCookFixture::ReadText(output) == outputBytes);
    CHECK(TextureCookFixture::ReadText(recordPath) == recordBytes);
    CHECK_FALSE(fs::exists(TransactionPath(fixture)));
}
