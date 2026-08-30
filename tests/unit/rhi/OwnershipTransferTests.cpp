#include <catch2/catch_test_macros.hpp>

#include "vulkan/OwnershipTransfer.h"

using namespace Hikari;

using namespace Hikari::Rhi::Vulkan;

namespace
{
// A device that has maintenance9 and will hand an optimal image to any family.
// Every test below starts from one of these two and changes the one thing it is
// about.
constexpr OwnershipTransferRules kPermissive{.bMaintenance9 = true,
                                             .OptimalImageTransferToQueueFamilies = ~0u};

// A device without the extension, which is the majority of hardware in the
// field and the path this rule exists to keep reachable.
constexpr OwnershipTransferRules kNoMaintenance9{.bMaintenance9 = false,
                                                 .OptimalImageTransferToQueueFamilies = 0u};

constexpr vk::ImageUsageFlags kSampled =
    vk::ImageUsageFlagBits::eSampled | vk::ImageUsageFlagBits::eTransferDst;

constexpr uint32_t kCopyFamily = 1u;
constexpr uint32_t kGraphicsFamily = 0u;
} // namespace

TEST_CASE("One queue family never transfers a resource to itself", "[RhiOwnershipTransfer]")
{
    // True whatever the rules say: the specification defines a barrier whose two
    // family indices are equal as performing no transfer at all, so asking for
    // one would be a barrier that does nothing but cost.
    REQUIRE_FALSE(BufferRequiresOwnershipTransfer(kNoMaintenance9, kCopyFamily, kCopyFamily));
    REQUIRE_FALSE(ImageRequiresOwnershipTransfer(kNoMaintenance9, vk::ImageTiling::eOptimal,
                                                 kSampled, kCopyFamily, kCopyFamily));
}

TEST_CASE("Without maintenance9 every resource needs an explicit transfer",
          "[RhiOwnershipTransfer]")
{
    REQUIRE(BufferRequiresOwnershipTransfer(kNoMaintenance9, kCopyFamily, kGraphicsFamily));
    REQUIRE(ImageRequiresOwnershipTransfer(kNoMaintenance9, vk::ImageTiling::eOptimal, kSampled,
                                           kCopyFamily, kGraphicsFamily));
    REQUIRE(ImageRequiresOwnershipTransfer(kNoMaintenance9, vk::ImageTiling::eLinear, kSampled,
                                           kCopyFamily, kGraphicsFamily));

    // The property is meaningless without the feature, so a permissive mask must
    // not rescue a device that cannot honour it.
    constexpr OwnershipTransferRules propertyWithoutFeature{
        .bMaintenance9 = false, .OptimalImageTransferToQueueFamilies = ~0u};
    REQUIRE(ImageRequiresOwnershipTransfer(propertyWithoutFeature, vk::ImageTiling::eOptimal,
                                           kSampled, kCopyFamily, kGraphicsFamily));
}

TEST_CASE("maintenance9 exempts buffers unconditionally", "[RhiOwnershipTransfer]")
{
    // Buffers carry no per-resource conditions at all — not tiling, not usage,
    // not the queue family property. An empty mask is what proves the buffer
    // answer does not consult it.
    constexpr OwnershipTransferRules noOptimalImages{.bMaintenance9 = true,
                                                     .OptimalImageTransferToQueueFamilies = 0u};

    REQUIRE_FALSE(BufferRequiresOwnershipTransfer(kPermissive, kCopyFamily, kGraphicsFamily));
    REQUIRE_FALSE(BufferRequiresOwnershipTransfer(noOptimalImages, kCopyFamily, kGraphicsFamily));
}

TEST_CASE("maintenance9 exempts linear images regardless of the family property",
          "[RhiOwnershipTransfer]")
{
    constexpr OwnershipTransferRules noOptimalImages{.bMaintenance9 = true,
                                                     .OptimalImageTransferToQueueFamilies = 0u};

    REQUIRE_FALSE(ImageRequiresOwnershipTransfer(noOptimalImages, vk::ImageTiling::eLinear,
                                                 kSampled, kCopyFamily, kGraphicsFamily));
}

TEST_CASE("An optimal image is exempt only for a destination family the property names",
          "[RhiOwnershipTransfer]")
{
    // The mask belongs to the *source* family and its bits are destination
    // family indices, so a mask naming only family 2 must not exempt a hand-over
    // to family 0.
    constexpr OwnershipTransferRules onlyFamilyTwo{.bMaintenance9 = true,
                                                   .OptimalImageTransferToQueueFamilies = 1u << 2};

    REQUIRE_FALSE(ImageRequiresOwnershipTransfer(kPermissive, vk::ImageTiling::eOptimal, kSampled,
                                                 kCopyFamily, kGraphicsFamily));
    REQUIRE(ImageRequiresOwnershipTransfer(onlyFamilyTwo, vk::ImageTiling::eOptimal, kSampled,
                                           kCopyFamily, kGraphicsFamily));
    REQUIRE_FALSE(
        ImageRequiresOwnershipTransfer(onlyFamilyTwo, vk::ImageTiling::eOptimal, kSampled,
                                       kCopyFamily, /*dstFamily*/ 2u));
}

TEST_CASE("An optimal image with any attachment usage always needs a transfer",
          "[RhiOwnershipTransfer]")
{
    // These are the usages the specification excludes from maintenance9's
    // promise, and the reason is the reason the transfer exists at all: an
    // attachment is what an implementation is most likely to keep in a form
    // another engine does not understand. A permissive mask must not override
    // them.
    const vk::ImageUsageFlags attachments[] = {
        vk::ImageUsageFlagBits::eColorAttachment,
        vk::ImageUsageFlagBits::eDepthStencilAttachment,
        vk::ImageUsageFlagBits::eTransientAttachment,
        vk::ImageUsageFlagBits::eInputAttachment,
        vk::ImageUsageFlagBits::eAttachmentFeedbackLoopEXT,
        vk::ImageUsageFlagBits::eFragmentShadingRateAttachmentKHR,
    };

    for (const vk::ImageUsageFlags usage : attachments)
    {
        REQUIRE(ImageRequiresOwnershipTransfer(kPermissive, vk::ImageTiling::eOptimal,
                                               kSampled | usage, kCopyFamily, kGraphicsFamily));
    }

    // A storage image carries none of them and stays exempt, so the test above
    // is checking the listed usages rather than "anything beyond sampled".
    REQUIRE_FALSE(ImageRequiresOwnershipTransfer(kPermissive, vk::ImageTiling::eOptimal,
                                                 kSampled | vk::ImageUsageFlagBits::eStorage,
                                                 kCopyFamily, kGraphicsFamily));
}

TEST_CASE("A destination family the 32-bit property cannot describe falls back to transferring",
          "[RhiOwnershipTransfer]")
{
    // optimalImageTransferToQueueFamilies is 32 bits wide, so it says nothing
    // about family 32 and above. No such device is known, but the unrepresentable
    // case has to resolve to doing the work rather than to a shift with
    // undefined behaviour.
    REQUIRE(ImageRequiresOwnershipTransfer(kPermissive, vk::ImageTiling::eOptimal, kSampled,
                                           kCopyFamily, /*dstFamily*/ 32u));
    REQUIRE(ImageRequiresOwnershipTransfer(kPermissive, vk::ImageTiling::eOptimal, kSampled,
                                           kCopyFamily, /*dstFamily*/ 64u));
}
