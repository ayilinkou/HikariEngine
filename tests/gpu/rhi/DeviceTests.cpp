#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <cstring>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>

#include "vulkan/vulkan_raii.hpp"

#include <rhi/IDevice.h>
#include <rhi/vulkan/VulkanNative.h>

#include "RhiTestFixture.h"
#include "ValidationGuard.h"

/**
 * What a device promises the moment it exists, before anything is asked of it.
 *
 * These need a real ICD, which is what the "gpu" label is for. Everything that
 * can be decided without one — the conversion tables, the queue-family
 * selection rules, the ownership-transfer rule — is a unit test instead, and
 * stays that way: a check that only runs on a machine with a GPU is a check CI
 * does not perform.
 */
using namespace Hikari::Rhi;

TEST_CASE("A headless device is created and reports no presentation", "[rhi][gpu][device][vulkan]")
{
    IDevice& device = RhiTest::RequireDevice();
    const RhiTest::ValidationGuard guard(device);

    // The tests ask for Requirements.bPresent = false, so this is the answer
    // that says the non-present path was actually taken rather than a window
    // having been found somewhere.
    REQUIRE_FALSE(device.GetCaps().bPresentSupported);

    // Vulkan's clip space has Y pointing down relative to GLM's convention. The
    // renderer negates a projection matrix row off this flag, so a backend
    // reporting it wrongly renders upside down rather than failing.
    REQUIRE(device.GetCaps().bFlipClipSpaceY);
}

/**
 * A created device is one that passed IsPhysicalDeviceSuitable, so it must
 * report everything that check demands. Written out rather than trusted because
 * the renderer depends on every one of them — dynamic rendering and
 * synchronization2 in particular are what the whole barrier vocabulary is built
 * on — and loosening the suitability check would otherwise surface as a driver
 * crash somewhere unrelated.
 */
TEST_CASE("The device supports the features the renderer requires", "[rhi][gpu][device][vulkan]")
{
    IDevice& device = RhiTest::RequireDevice();
    const RhiTest::ValidationGuard guard(device);

    vk::raii::PhysicalDevice& physicalDevice = Vulkan::GetPhysicalDevice(device);

    REQUIRE(physicalDevice.getProperties().apiVersion >= vk::ApiVersion13);

    const auto features =
        physicalDevice.getFeatures2<vk::PhysicalDeviceFeatures2, vk::PhysicalDeviceVulkan12Features,
                                    vk::PhysicalDeviceVulkan13Features,
                                    vk::PhysicalDeviceExtendedDynamicStateFeaturesEXT>();

    CHECK(features.get<vk::PhysicalDeviceFeatures2>().features.samplerAnisotropy);
    CHECK(features.get<vk::PhysicalDeviceFeatures2>().features.independentBlend);
    // The neutral fence is a monotonic counter (plan D5), which on this backend
    // is a timeline semaphore and nothing else.
    CHECK(features.get<vk::PhysicalDeviceVulkan12Features>().timelineSemaphore);
    CHECK(features.get<vk::PhysicalDeviceVulkan13Features>().dynamicRendering);
    CHECK(features.get<vk::PhysicalDeviceVulkan13Features>().synchronization2);
    CHECK(features.get<vk::PhysicalDeviceExtendedDynamicStateFeaturesEXT>().extendedDynamicState);

    const std::vector<vk::ExtensionProperties> available =
        physicalDevice.enumerateDeviceExtensionProperties();
    const bool bHasDescriptorIndexing =
        std::ranges::any_of(available, [](const vk::ExtensionProperties& properties)
                            { return std::strcmp(properties.extensionName,
                                                 vk::EXTDescriptorIndexingExtensionName) == 0; });
    CHECK(bHasDescriptorIndexing);
}

/**
 * The escape hatch ImGui reaches through (plan D9). Every field of it is handed
 * to a C API that cannot check them, so a null one is a crash inside somebody
 * else's library.
 */
TEST_CASE("The native handles the ImGui hatch exposes are all real", "[rhi][gpu][device][vulkan]")
{
    IDevice& device = RhiTest::RequireDevice();
    const RhiTest::ValidationGuard guard(device);

    const Vulkan::NativeDevice native = Vulkan::GetNative(device);

    CHECK(native.Instance != VK_NULL_HANDLE);
    CHECK(native.PhysicalDevice != VK_NULL_HANDLE);
    CHECK(native.Device != VK_NULL_HANDLE);
    CHECK(native.GraphicsQueue != VK_NULL_HANDLE);
    CHECK(native.GraphicsQueueFamily != ~0u);
    CHECK(native.ApiVersion >= VK_API_VERSION_1_3);
}

/**
 * DeviceDesc::bForceSingleQueue is the lever the upload round-trips use to
 * reach the arrangement an integrated GPU has. If it stopped collapsing the
 * roles, those tests would keep passing while silently covering one fewer path
 * — so the lever gets its own check rather than being trusted.
 */
TEST_CASE("Forcing a single queue removes the dedicated copy queue", "[rhi][gpu][device][queues]")
{
    IDevice& forced = RhiTest::RequireDevice(RhiTest::DeviceConfig::SingleQueue);
    const RhiTest::ValidationGuard guard(forced);

    REQUIRE_FALSE(forced.GetCaps().bHasDedicatedCopyQueue);
    REQUIRE_FALSE(forced.GetCaps().bHasDedicatedComputeQueue);
}

/**
 * The adapter's PCI identity is what lets reports from two backends be recognised
 * as the same adapter, so a device that left it at zero would make every such pair
 * look like two different machines. No real adapter reports vendor zero: a PCI
 * vendor has its PCI ID, and anything else a Khronos ID from 0x10000 up.
 */
TEST_CASE("The device reports its adapter's vendor", "[rhi][gpu][device]")
{
    IDevice& device = RhiTest::RequireDevice();

    CHECK(device.GetInfo().VendorId != 0u);
    CHECK_FALSE(device.GetInfo().Gpu.empty());
}

/**
 * A name that matches nothing is refused rather than ignored: silently running on
 * another adapter would measure the wrong machine. The refusal lists what was found,
 * which is the only way a user learns the spelling this backend wants.
 */
TEST_CASE("Asking for an adapter no one has is refused, naming the adapters found",
          "[rhi][gpu][device]")
{
    // Only meaningful where a device can exist at all; skips with the fixture's
    // reason otherwise.
    const IDevice& existing = RhiTest::RequireDevice();

    Diagnostics diagnostics;
    DeviceDesc desc = RhiTest::Detail::MakeDesc(RhiTest::DeviceConfig::Default, diagnostics);
    desc.Gpu = "no adapter is called this";

    try
    {
        const std::unique_ptr<IDevice> device = CreateDevice(desc);
        FAIL("a device was created for an adapter name nothing matches");
    }
    catch (const std::runtime_error& error)
    {
        const std::string message = error.what();
        CHECK(message.find("no adapter is called this") != std::string::npos);
        CHECK(message.find(existing.GetInfo().Gpu) != std::string::npos);
    }
}

/**
 * A new device owns nothing, under every configuration.
 *
 * The counters are what the upload round-trips compare against to prove they
 * left nothing behind, and what the device's destructor reports a leak from —
 * so a counter that started out wrong would make both of those lie. It has to
 * be checked here rather than trusted, because the destructor's report arrives
 * long after ctest has called the run a pass.
 */
TEST_CASE("A new device owns no resources", "[rhi][gpu][device]")
{
    for (const RhiTest::DeviceConfig config : RhiTest::AllDeviceConfigs())
    {
        INFO("device configuration: " << RhiTest::Describe(config));

        IDevice& device = RhiTest::RequireDevice(config);

        CHECK(device.GetLiveBufferCount() == 0u);
        CHECK(device.GetLiveTextureCount() == 0u);
        CHECK(device.GetLiveTextureViewCount() == 0u);
        CHECK(device.GetLiveSamplerCount() == 0u);
    }
}
