#include <catch2/catch_test_macros.hpp>

#include <rhi/RhiTypes.h>

#include "vulkan/QueueFamilies.h"

#include <vector>

// CPU-only: no instance, no physical device, no ICD needed. SelectQueueFamilies
// takes the family list as data precisely so that the layouts this machine's GPU
// does not expose — a transfer-only DMA family, a graphics family without
// compute, two graphics families of which only one can present — can be tested
// anyway. Those are exactly the layouts where a wrong choice shows up as a
// failure on somebody else's hardware.
using namespace Rhi;
using namespace Rhi::Vulkan;

namespace
{
constexpr vk::QueueFlags kGraphics = vk::QueueFlagBits::eGraphics;
constexpr vk::QueueFlags kCompute = vk::QueueFlagBits::eCompute;
constexpr vk::QueueFlags kTransfer = vk::QueueFlagBits::eTransfer;
constexpr vk::QueueFlags kSparse = vk::QueueFlagBits::eSparseBinding;
constexpr vk::QueueFlags kVideoDecode = vk::QueueFlagBits::eVideoDecodeKHR;

vk::QueueFamilyProperties Family(vk::QueueFlags capabilities, uint32_t queueCount = 1)
{
    return vk::QueueFamilyProperties{.queueFlags = capabilities, .queueCount = queueCount};
}

// Every family presents. The common case, and the one that leaves the graphics
// choice down to capabilities alone.
bool AllPresent(uint32_t)
{
    return true;
}

QueueFamilies Select(const std::vector<vk::QueueFamilyProperties>& families,
                     const PresentSupportFn& presentSupported = AllPresent,
                     bool bRequirePresent = true)
{
    return SelectQueueFamilies(families, presentSupported, bRequirePresent);
}
} // namespace

TEST_CASE("A single universal family serves every role", "[rhi][queues]")
{
    const QueueFamilies families = Select({Family(kGraphics | kCompute | kTransfer | kSparse, 16)});

    REQUIRE(families.Graphics == 0);
    REQUIRE(families.Compute == 0);
    REQUIRE(families.Copy == 0);

    REQUIRE_FALSE(families.IsDedicated(QueueType::Graphics));
    REQUIRE_FALSE(families.IsDedicated(QueueType::Compute));
    REQUIRE_FALSE(families.IsDedicated(QueueType::Copy));
}

TEST_CASE("A discrete GPU's async compute and DMA families are picked up", "[rhi][queues]")
{
    // The layout a typical discrete GPU reports: one universal family, one
    // transfer-only DMA family, one compute family.
    const QueueFamilies families = Select({
        Family(kGraphics | kCompute | kTransfer | kSparse, 16),
        Family(kTransfer | kSparse, 2),
        Family(kCompute | kTransfer | kSparse, 8),
    });

    REQUIRE(families.Graphics == 0);
    REQUIRE(families.Compute == 2);
    REQUIRE(families.Copy == 1);

    REQUIRE(families.IsDedicated(QueueType::Compute));
    REQUIRE(families.IsDedicated(QueueType::Copy));
}

TEST_CASE("Copy prefers the narrowest family that can serve it", "[rhi][queues]")
{
    // Both non-graphics families can copy. The transfer-only one is the DMA
    // engine; taking the compute one instead would put uploads on the engine
    // that async compute work wants.
    const QueueFamilies families = Select({
        Family(kGraphics | kCompute | kTransfer),
        Family(kCompute | kTransfer),
        Family(kTransfer),
    });

    REQUIRE(families.Copy == 2);
    REQUIRE(families.Compute == 1);
}

TEST_CASE("Copy falls back to the compute family when there is no DMA one", "[rhi][queues]")
{
    const QueueFamilies families = Select({
        Family(kGraphics | kCompute | kTransfer),
        Family(kCompute | kTransfer),
    });

    REQUIRE(families.Copy == 1);
    REQUIRE(families.Compute == 1);
    REQUIRE(families.IsDedicated(QueueType::Copy));
}

TEST_CASE("A video family does not outrank a transfer-only one", "[rhi][queues]")
{
    // The video family advertises transfer as well, and has the same number of
    // core capabilities as the DMA family — so the tie-break has to be decided
    // on the core bits alone, not on how many flags the family sets in total.
    const QueueFamilies families = Select({
        Family(kGraphics | kCompute | kTransfer),
        Family(kVideoDecode | kTransfer),
        Family(kTransfer | kSparse),
    });

    REQUIRE(families.Copy == 1);
}

TEST_CASE("A graphics family covers copy when the device has nothing else", "[rhi][queues]")
{
    // Advertising graphics is enough to be able to copy, so a family that omits
    // the transfer bit still serves the role.
    const QueueFamilies families = Select({Family(kGraphics)});

    REQUIRE(families.Graphics == 0);
    REQUIRE(families.Copy == 0);
}

TEST_CASE("Compute stays unresolved when no family can dispatch", "[rhi][queues]")
{
    const QueueFamilies families = Select({Family(kGraphics | kTransfer)});

    REQUIRE(families.Graphics == 0);
    REQUIRE(families.Copy == 0);
    REQUIRE(families.Compute == QueueFamilies::kInvalid);
    REQUIRE_FALSE(families.IsDedicated(QueueType::Compute));
}

TEST_CASE("Presentation narrows the graphics choice", "[rhi][queues]")
{
    const std::vector<vk::QueueFamilyProperties> families = {
        Family(kGraphics | kCompute | kTransfer),
        Family(kGraphics | kCompute | kTransfer),
    };

    const auto secondPresentsOnly = [](uint32_t index) { return index == 1; };

    REQUIRE(Select(families, secondPresentsOnly).Graphics == 1);

    // Without the requirement the surface is never consulted, so the first
    // graphics family wins — which is the headless path Stage 6 turns on.
    REQUIRE(Select(families, nullptr, false).Graphics == 0);
}

TEST_CASE("Nothing resolves when no family supports graphics", "[rhi][queues]")
{
    const QueueFamilies families = Select({Family(kCompute | kTransfer), Family(kTransfer)});

    REQUIRE(families.Graphics == QueueFamilies::kInvalid);

    // The two dedicated families were still found, but with no graphics family
    // to fall back from there is nothing for the caller to do with them: device
    // creation fails on the missing graphics family instead.
    REQUIRE(families.Compute == 0);
    REQUIRE(families.Copy == 1);
}

TEST_CASE("An empty family list resolves to nothing", "[rhi][queues]")
{
    const QueueFamilies families = Select({});

    REQUIRE(families.Graphics == QueueFamilies::kInvalid);
    REQUIRE(families.Compute == QueueFamilies::kInvalid);
    REQUIRE(families.Copy == QueueFamilies::kInvalid);
}

TEST_CASE("Get() covers every role", "[rhi][queues]")
{
    const QueueFamilies families = Select({
        Family(kGraphics | kCompute | kTransfer),
        Family(kTransfer),
    });

    for (const QueueType role : kAllQueueTypes)
        REQUIRE(families.Get(role) != QueueFamilies::kInvalid);

    REQUIRE(families.Get(QueueType::Graphics) == families.Graphics);
    REQUIRE(families.Get(QueueType::Compute) == families.Compute);
    REQUIRE(families.Get(QueueType::Copy) == families.Copy);
}
