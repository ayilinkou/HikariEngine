#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <optional>
#include <span>
#include <string>

#include <platform/CommandLine.h>

#include <rhi/Backend.h>

#include <engine/EngineConfig.h>
#include <engine/ParseEngineOptions.h>
#include <engine/RunSpec.h>

using namespace Hikari::Engine;
using namespace Hikari::Platform;
using Hikari::Rhi::Backend;
using Hikari::Rhi::ValidationPolicy;

namespace
{
/** One option as the CommandLine would have handed it over. */
CommandLineOption Option(std::string flag, std::optional<std::string> value = std::nullopt)
{
    return CommandLineOption{.Flag = std::move(flag), .Value = std::move(value)};
}
} // namespace

TEST_CASE("An engine flag is claimed and applied", "[ParseEngineOption]")
{
    RunSpec spec;
    EngineConfig config;

    REQUIRE(ParseEngineOption(Option("--scene", "scenes/other.map"), spec, config));
    REQUIRE(spec.ScenePath == "scenes/other.map");
}

TEST_CASE("A flag the engine does not know is declined and changes nothing", "[ParseEngineOption]")
{
    RunSpec spec;
    EngineConfig config;
    spec.ScenePath = "untouched";

    // The app's own flags reach here first, so declining has to leave the run
    // description exactly as it was.
    REQUIRE_FALSE(ParseEngineOption(Option("--screenshot", "shot.png"), spec, config));
    REQUIRE_FALSE(ParseEngineOption(Option("--resolution", "1920x1080"), spec, config));
    REQUIRE(spec.ScenePath == "untouched");
    REQUIRE_FALSE(spec.bCaptureFinalFrame);
}

TEST_CASE("A valueless --frames means the default rather than zero", "[ParseEngineOption]")
{
    RunSpec spec;
    EngineConfig config;

    REQUIRE(ParseEngineOption(Option("--frames"), spec, config));
    REQUIRE(spec.Frames == 1000u);

    RunSpec explicitSpec;
    REQUIRE(ParseEngineOption(Option("--frames", "30"), explicitSpec, config));
    REQUIRE(explicitSpec.Frames == 30u);
}

TEST_CASE("The --validation-policy names map to the policy, and nothing else does",
          "[ParseEngineOption]")
{
    RunSpec spec;
    EngineConfig config;

    REQUIRE(ParseEngineOption(Option("--validation-policy", "ignore"), spec, config));
    REQUIRE(spec.ValidationPolicy == ValidationPolicy::Ignore);

    REQUIRE(ParseEngineOption(Option("--validation-policy", "failfast"), spec, config));
    REQUIRE(spec.ValidationPolicy == ValidationPolicy::FailFast);

    REQUIRE_THROWS_AS(ParseEngineOption(Option("--validation-policy", "strict"), spec, config),
                      CommandLineError);
}

TEST_CASE("The --frames-in-flight flag sizes the engine and refuses a count below one",
          "[ParseEngineOption]")
{
    RunSpec spec;
    EngineConfig config;

    REQUIRE(ParseEngineOption(Option("--frames-in-flight", "3"), spec, config));
    REQUIRE(config.FramesInFlight == 3u);

    // Zero would size every per-frame resource to nothing, which fails far from
    // the flag that asked for it.
    REQUIRE_THROWS_AS(ParseEngineOption(Option("--frames-in-flight", "0"), spec, config),
                      CommandLineError);
    REQUIRE(config.FramesInFlight == 3u);
}

TEST_CASE("The --vk-disable-extension flag accumulates, since it is repeatable",
          "[ParseEngineOption]")
{
    RunSpec spec;
    EngineConfig config;

    REQUIRE(
        ParseEngineOption(Option("--vk-disable-extension", "VK_KHR_maintenance9"), spec, config));
    REQUIRE(
        ParseEngineOption(Option("--vk-disable-extension", "VK_EXT_debug_utils"), spec, config));

    REQUIRE(spec.DisabledVulkanExtensions ==
            std::vector<std::string>{"VK_KHR_maintenance9", "VK_EXT_debug_utils"});
}

TEST_CASE("A flag given a value it cannot take is rejected", "[ParseEngineOption]")
{
    RunSpec spec;
    EngineConfig config;

    REQUIRE_THROWS_AS(ParseEngineOption(Option("--fixed-dt", "yes"), spec, config),
                      CommandLineError);
}

TEST_CASE("The --backend flag selects a backend this build contains", "[ParseEngineOption]")
{
    RunSpec spec;
    EngineConfig config;

    REQUIRE(spec.Backend == Backend::Vulkan);
    REQUIRE(ParseEngineOption(Option("--backend", "vulkan"), spec, config));
    CHECK(spec.Backend == Backend::Vulkan);
}

TEST_CASE("The --backend flag refuses what the build does not contain", "[ParseEngineOption]")
{
    RunSpec spec;
    EngineConfig config;

    SECTION("a word that names no backend")
    {
        CHECK_THROWS_AS(ParseEngineOption(Option("--backend", "metal"), spec, config),
                        CommandLineError);
    }

    SECTION("a backend that exists but was not compiled in")
    {
        // Conditional on the build rather than on the platform: this case is
        // about a backend missing from *this* binary, and on a Windows build
        // that contains D3D12 the flag is expected to be accepted instead.
        const std::span<const Backend> available = Hikari::Rhi::AvailableBackends();
        const bool bHasD3D12 = std::ranges::find(available, Backend::D3D12) != available.end();

        if (bHasD3D12)
        {
            REQUIRE(ParseEngineOption(Option("--backend", "d3d12"), spec, config));
            CHECK(spec.Backend == Backend::D3D12);
        }
        else
        {
            CHECK_THROWS_AS(ParseEngineOption(Option("--backend", "d3d12"), spec, config),
                            CommandLineError);
        }
    }

    SECTION("the message names what the build does have")
    {
        try
        {
            ParseEngineOption(Option("--backend", "metal"), spec, config);
            FAIL("expected a CommandLineError");
        }
        catch (const CommandLineError& e)
        {
            const std::string message = e.what();
            CHECK(message.find("metal") != std::string::npos);
            CHECK(message.find("Vulkan") != std::string::npos);
        }
    }
}

TEST_CASE("The --backend flag with no value is rejected", "[ParseEngineOption]")
{
    RunSpec spec;
    EngineConfig config;

    CHECK_THROWS_AS(ParseEngineOption(Option("--backend"), spec, config), CommandLineError);
}
