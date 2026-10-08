#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_string.hpp>

#include <optional>

#include <rhi/RhiTypes.h>

#include "d3d12/D3D12PresentMode.h"

using namespace Hikari::Rhi;
using namespace Hikari::Rhi::D3D12;

TEST_CASE("D3D12 defaults to mailbox regardless of tearing support", "[swapchain][d3d12]")
{
    CHECK(ChoosePresentMode(std::nullopt, false) == PresentMode::Mailbox);
    CHECK(ChoosePresentMode(std::nullopt, true) == PresentMode::Mailbox);
}

TEST_CASE("D3D12 honours each explicit mode when supported", "[swapchain][d3d12]")
{
    CHECK(ChoosePresentMode(PresentMode::Immediate, true) == PresentMode::Immediate);
    for (const bool bTearingSupported : {false, true})
    {
        CHECK(ChoosePresentMode(PresentMode::Mailbox, bTearingSupported) == PresentMode::Mailbox);
        CHECK(ChoosePresentMode(PresentMode::Fifo, bTearingSupported) == PresentMode::Fifo);
    }
}

TEST_CASE("D3D12 refuses immediate where tearing is unsupported", "[swapchain][d3d12]")
{
    CHECK_THROWS_WITH(
        ChoosePresentMode(PresentMode::Immediate, false),
        "Requested present mode 'immediate' is unavailable. Available: mailbox, fifo.");
}
