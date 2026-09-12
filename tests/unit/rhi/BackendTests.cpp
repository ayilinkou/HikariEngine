#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <optional>
#include <span>
#include <string>
#include <string_view>

#include <rhi/Backend.h>

using Hikari::Rhi::AvailableBackends;
using Hikari::Rhi::Backend;
using Hikari::Rhi::BackendFromString;
using Hikari::Rhi::ToString;

namespace
{
/** Every enumerator, so a new backend cannot be added without appearing here. */
constexpr Backend kAll[] = {Backend::Vulkan, Backend::D3D12};
} // namespace

TEST_CASE("Vulkan is always available", "[rhi][backend]")
{
    // Permanent, not "until D3D12 reaches parity" (plan D25): a bug report, a
    // baseline capture and a run report mean the same thing whoever produced
    // them only because one backend is always the default and always there.
    const std::span<const Backend> available = AvailableBackends();

    REQUIRE_FALSE(available.empty());
    CHECK(std::ranges::find(available, Backend::Vulkan) != available.end());
}

TEST_CASE("Availability lists each backend once", "[rhi][backend]")
{
    const std::span<const Backend> available = AvailableBackends();

    for (const Backend backend : kAll)
    {
        const auto count = std::ranges::count(available, backend);
        INFO("backend: " << ToString(backend));
        CHECK(count <= 1);
    }
}

TEST_CASE("Every backend has a spelling, and it round-trips", "[rhi][backend]")
{
    for (const Backend backend : kAll)
    {
        const std::string_view name = ToString(backend);
        INFO("backend: " << name);

        CHECK(name != "unknown");
        CHECK(BackendFromString(name) == backend);

        // Lower case, because the command line and the run report share these
        // strings and --backend is typed by hand.
        CHECK(name == std::string(name.begin(), name.end()));
        CHECK(std::ranges::none_of(name, [](char c) { return c >= 'A' && c <= 'Z'; }));
    }
}

TEST_CASE("No two backends share a spelling", "[rhi][backend]")
{
    // The whole point of one table: a duplicate would make --backend ambiguous
    // and a run report's "backend" field unreadable.
    for (const Backend a : kAll)
    {
        for (const Backend b : kAll)
        {
            if (a == b)
                continue;

            CHECK(ToString(a) != ToString(b));
        }
    }
}

TEST_CASE("A word that names no backend is rejected", "[rhi][backend]")
{
    CHECK_FALSE(BackendFromString("").has_value());
    CHECK_FALSE(BackendFromString("metal").has_value());
    CHECK_FALSE(BackendFromString("Vulkan").has_value());
    CHECK_FALSE(BackendFromString("unknown").has_value());
}
