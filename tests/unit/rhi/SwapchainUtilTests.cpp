#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_string.hpp>

#include <rhi/vulkan/SwapchainUtil.h>

#include <limits>

using namespace Hikari::Rhi::Vulkan;

/**
 * CPU-only: these are pure functions over what a surface reported, so the
 * surface states that matter — a window system that sizes the surface from the
 * swapchain, and a minimized window with no area at all — can be written down
 * as data instead of being staged on a real display.
 */

namespace
{
constexpr uint32_t kSpecialValue = std::numeric_limits<uint32_t>::max();

vk::SurfaceCapabilitiesKHR MakeCapabilities(vk::Extent2D current, vk::Extent2D min,
                                            vk::Extent2D max)
{
    vk::SurfaceCapabilitiesKHR capabilities{};
    capabilities.currentExtent = current;
    capabilities.minImageExtent = min;
    capabilities.maxImageExtent = max;
    return capabilities;
}

/** A surface format entry, spelled out because both halves matter. */
vk::SurfaceFormatKHR MakeFormat(vk::Format format,
                                vk::ColorSpaceKHR colorSpace = vk::ColorSpaceKHR::eSrgbNonlinear)
{
    return vk::SurfaceFormatKHR{format, colorSpace};
}
} // namespace

TEST_CASE("A surface that knows its size is taken at its word", "[swapchain]")
{
    const vk::SurfaceCapabilitiesKHR capabilities =
        MakeCapabilities({800u, 600u}, {800u, 600u}, {800u, 600u});

    // The window size disagreeing with the surface is normal mid-resize; the
    // surface wins, because it is what the swapchain is validated against.
    const vk::Extent2D extent = ChooseSwapchainExtent(capabilities, {1024u, 768u});

    CHECK(extent == vk::Extent2D{800u, 600u});
    CHECK(CanCreateSwapchain(capabilities, {1024u, 768u}));
}

TEST_CASE("A surface sized by its swapchain takes the framebuffer extent", "[swapchain]")
{
    const vk::SurfaceCapabilitiesKHR capabilities =
        MakeCapabilities({kSpecialValue, kSpecialValue}, {1u, 1u}, {4096u, 4096u});

    CHECK(ChooseSwapchainExtent(capabilities, {1024u, 768u}) == vk::Extent2D{1024u, 768u});
    CHECK(ChooseSwapchainExtent(capabilities, {8192u, 8192u}) == vk::Extent2D{4096u, 4096u});
    CHECK(CanCreateSwapchain(capabilities, {1024u, 768u}));
}

TEST_CASE("A minimized window leaves the surface with no area", "[swapchain]")
{
    // What Win32 reports for a minimized window: the spec requires
    // currentExtent to equal the window size, and allows both to be zero. The
    // framebuffer extent is deliberately non-zero — SDL reports the size the
    // window had before it was minimized, so it cannot be the thing that
    // decides this.
    const vk::SurfaceCapabilitiesKHR capabilities =
        MakeCapabilities({0u, 0u}, {0u, 0u}, {0u, 0u});

    CHECK(ChooseSwapchainExtent(capabilities, {1920u, 1080u}) == vk::Extent2D{0u, 0u});
    CHECK_FALSE(CanCreateSwapchain(capabilities, {1920u, 1080u}));
}

TEST_CASE("A surface sized by its swapchain can also lose its area", "[swapchain]")
{
    // maxImageExtent collapsing to zero is the same condition on a window
    // system that otherwise leaves the size to the swapchain.
    const vk::SurfaceCapabilitiesKHR capabilities =
        MakeCapabilities({kSpecialValue, kSpecialValue}, {0u, 0u}, {0u, 0u});

    CHECK_FALSE(CanCreateSwapchain(capabilities, {1920u, 1080u}));
}

TEST_CASE("The preferred swapchain format wins when the surface offers it", "[swapchain]")
{
    const std::vector formats{MakeFormat(vk::Format::eR8G8B8A8Unorm),
                              MakeFormat(vk::Format::eB8G8R8A8Unorm)};

    // Order in the surface's list does not matter; the preference order does.
    CHECK(ChooseSwapchainFormat(formats).format == vk::Format::eB8G8R8A8Unorm);
}

TEST_CASE("The second preference is taken when the first is absent", "[swapchain]")
{
    const std::vector formats{MakeFormat(vk::Format::eB8G8R8A8Srgb),
                              MakeFormat(vk::Format::eR8G8B8A8Unorm)};

    // Both preferences are UNORM, so falling through to the second cannot
    // change what the hardware writes — which is what makes it a fallback
    // rather than a different image.
    CHECK(ChooseSwapchainFormat(formats).format == vk::Format::eR8G8B8A8Unorm);
}

TEST_CASE("A preferred format in the wrong colour space is not a match", "[swapchain]")
{
    const std::vector formats{
        MakeFormat(vk::Format::eB8G8R8A8Unorm, vk::ColorSpaceKHR::eDisplayP3NonlinearEXT),
        MakeFormat(vk::Format::eR8G8B8A8Unorm)};

    CHECK(ChooseSwapchainFormat(formats).format == vk::Format::eR8G8B8A8Unorm);
}

TEST_CASE("A surface offering neither preference fails, naming what it offered", "[swapchain]")
{
    // The case the old fallback turned into an abort one line later: it
    // returned formats[0], and FromNativeFormat cannot name B8G8R8A8_SRGB.
    const std::vector formats{MakeFormat(vk::Format::eB8G8R8A8Srgb)};

    REQUIRE_THROWS_WITH(ChooseSwapchainFormat(formats),
                        Catch::Matchers::ContainsSubstring("B8G8R8A8Srgb"));
}

TEST_CASE("A surface with no formats at all fails", "[swapchain]")
{
    CHECK_THROWS(ChooseSwapchainFormat({}));
}
