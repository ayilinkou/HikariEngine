#include <catch2/catch_test_macros.hpp>

#include <cstddef>
#include <cstdint>
#include <memory>
#include <span>
#include <vector>

#include <rhi/IDevice.h>
#include <rhi/UniqueHandle.h>
#include <rhi/UploadContext.h>

#include "GpuReadback.h"
#include "RhiTestFixture.h"
#include "ValidationGuard.h"

// What an upload actually put on the GPU, read back and compared byte for byte.
//
// Every case runs under all four device configurations, because the upload path
// is the one part of the RHI whose behaviour depends on the shape of the device
// rather than only on what it is asked to do: whether a resource crosses queue
// families, and whether crossing them needs an explicit hand-over, is decided
// per device. Three of the four arrangements are unreachable on any one machine
// without the levers RhiTestFixture pulls, and the ones this GPU is not are the
// ones most hardware in the field is.
using namespace Hikari::Rhi;

namespace
{
// Deterministic and different at every byte, so a copy that lands one texel or
// one layer off shows up rather than matching by luck. Not std::iota: a
// 256-byte cycle would make an offset of exactly 256 invisible.
std::vector<std::byte> MakePattern(size_t byteCount, uint32_t seed)
{
    std::vector<std::byte> bytes(byteCount);

    uint32_t state = seed * 2654435761u + 1u;
    for (std::byte& value : bytes)
    {
        state = state * 1664525u + 1013904223u;
        value = static_cast<std::byte>((state >> 16) & 0xFFu);
    }

    return bytes;
}

// The resource counts a test starts from, so it can prove it left none behind.
// The device is per-process here — catch_discover_tests runs one case per
// invocation — but a leak still matters: it is the same mistake the renderer
// would make, and the device only reports one in its destructor, long after
// ctest has called the run a pass.
struct LiveCounts
{
    uint32_t Buffers = 0u;
    uint32_t Textures = 0u;

    bool operator==(const LiveCounts&) const = default;
};

LiveCounts Live(IDevice& device)
{
    return LiveCounts{device.GetLiveBufferCount(), device.GetLiveTextureCount()};
}
} // namespace

TEST_CASE("A buffer upload round-trips byte for byte", "[rhi][gpu][upload]")
{
    for (const RhiTest::DeviceConfig config : RhiTest::kAllDeviceConfigs)
    {
        INFO("device configuration: " << RhiTest::Describe(config));

        IDevice& device = RhiTest::RequireDevice(config);
        const RhiTest::ValidationGuard guard(device);
        const LiveCounts before = Live(device);

        // Deliberately not a multiple of anything: a size the copy has to carry
        // exactly rather than round up to.
        const std::vector<std::byte> source = MakePattern(3079u, 1u);

        {
            const UniqueHandle<BufferHandle> destination(
                device, device.CreateBuffer(BufferDesc{
                            .Size = source.size(),
                            .Usage = BufferUsage::CopyDst | BufferUsage::CopySrc,
                            .Access = MemoryAccess::GpuOnly,
                            .DebugName = "Round-trip Destination"}));

            const std::unique_ptr<IUploadContext> context =
                device.CreateUploadContext(UploadContextDesc{.DebugName = "Round-trip"});

            context->UploadBuffer(destination.Get(), 0u, source);
            context->Flush();

            const std::vector<std::byte> readBack =
                RhiTest::ReadBuffer(device, destination.Get(), source.size());

            REQUIRE(readBack == source);
        }

        REQUIRE(Live(device) == before);
    }
}

// The batching IUploadContext exists for: many copies recorded into one command
// list and submitted once. Two uploads into one buffer at different offsets are
// also what catches a context that ignores destinationOffset — which would look
// perfect in the single-upload case above.
TEST_CASE("Several uploads batch into one flush and keep their own offsets", "[rhi][gpu][upload]")
{
    for (const RhiTest::DeviceConfig config : RhiTest::kAllDeviceConfigs)
    {
        INFO("device configuration: " << RhiTest::Describe(config));

        IDevice& device = RhiTest::RequireDevice(config);
        const RhiTest::ValidationGuard guard(device);
        const LiveCounts before = Live(device);

        constexpr uint64_t kHalf = 512u;
        const std::vector<std::byte> first = MakePattern(kHalf, 2u);
        const std::vector<std::byte> second = MakePattern(kHalf, 3u);
        const std::vector<std::byte> other = MakePattern(kHalf, 4u);

        {
            const UniqueHandle<BufferHandle> split(
                device, device.CreateBuffer(BufferDesc{
                            .Size = kHalf * 2u,
                            .Usage = BufferUsage::CopyDst | BufferUsage::CopySrc,
                            .Access = MemoryAccess::GpuOnly,
                            .DebugName = "Split Destination"}));

            const UniqueHandle<BufferHandle> separate(
                device, device.CreateBuffer(BufferDesc{
                            .Size = kHalf,
                            .Usage = BufferUsage::CopyDst | BufferUsage::CopySrc,
                            .Access = MemoryAccess::GpuOnly,
                            .DebugName = "Separate Destination"}));

            const std::unique_ptr<IUploadContext> context =
                device.CreateUploadContext(UploadContextDesc{.DebugName = "Batched"});

            context->UploadBuffer(split.Get(), 0u, first);
            context->UploadBuffer(split.Get(), kHalf, second);
            context->UploadBuffer(separate.Get(), 0u, other);
            context->Flush();

            const UploadStats& stats = context->GetStats();
            CHECK(stats.Uploads == 3u);
            CHECK(stats.Bytes == kHalf * 3u);

            // One submission, or two where the copies were handed back from a
            // queue of their own. Three would mean the batching stopped
            // happening, which is the regression this number exists to catch.
            CHECK(stats.Submits <= 2u);

            const std::vector<std::byte> splitBytes =
                RhiTest::ReadBuffer(device, split.Get(), kHalf * 2u);

            REQUIRE(std::vector<std::byte>(splitBytes.begin(), splitBytes.begin() + kHalf) == first);
            REQUIRE(std::vector<std::byte>(splitBytes.begin() + kHalf, splitBytes.end()) == second);
            REQUIRE(RhiTest::ReadBuffer(device, separate.Get(), kHalf) == other);
        }

        REQUIRE(Live(device) == before);
    }
}

TEST_CASE("A texture upload round-trips byte for byte", "[rhi][gpu][upload]")
{
    for (const RhiTest::DeviceConfig config : RhiTest::kAllDeviceConfigs)
    {
        INFO("device configuration: " << RhiTest::Describe(config));

        IDevice& device = RhiTest::RequireDevice(config);
        const RhiTest::ValidationGuard guard(device);
        const LiveCounts before = Live(device);

        // Non-square and neither dimension a power of two, so a copy that
        // confuses width with height, or assumes a padded row pitch, produces
        // different bytes rather than the same ones rearranged.
        constexpr Extent3D kExtent{5u, 3u, 1u};
        const std::vector<std::byte> source =
            MakePattern(kExtent.Width * kExtent.Height * 4u, 5u);

        {
            const UniqueHandle<TextureHandle> texture(
                device, device.CreateTexture(TextureDesc{
                            .Format = Format::RGBA8Unorm,
                            .Extent = kExtent,
                            .Usage = TextureUsage::Sampled | TextureUsage::CopyDst |
                                     TextureUsage::CopySrc,
                            .DebugName = "Round-trip Texture"}));

            const std::unique_ptr<IUploadContext> context =
                device.CreateUploadContext(UploadContextDesc{.DebugName = "Texture Round-trip"});

            context->UploadTexture(texture.Get(), TextureUpload{.Data = source, .Extent = kExtent});
            context->Flush();

            const std::vector<std::vector<std::byte>> layers =
                RhiTest::ReadTextureLayers(device, texture.Get());

            REQUIRE(layers.size() == 1u);
            REQUIRE(layers[0] == source);
        }

        REQUIRE(Live(device) == before);
    }
}

// The case the whole handle-based texture path was most likely to get wrong: a
// copy or a layout transition that hardcodes one array layer fills layer 0 six
// times and leaves the other five undefined. Nothing renders visibly wrong —
// the skybox samples a cube whose faces all happen to agree — so only a
// readback that looks at each layer separately can say.
TEST_CASE("Every cubemap face lands on its own layer", "[rhi][gpu][upload]")
{
    for (const RhiTest::DeviceConfig config : RhiTest::kAllDeviceConfigs)
    {
        INFO("device configuration: " << RhiTest::Describe(config));

        IDevice& device = RhiTest::RequireDevice(config);
        const RhiTest::ValidationGuard guard(device);
        const LiveCounts before = Live(device);

        constexpr uint32_t kFaceCount = 6u;
        constexpr Extent3D kExtent{4u, 4u, 1u};
        constexpr size_t kFaceBytes = kExtent.Width * kExtent.Height * 4u;

        std::vector<std::vector<std::byte>> faces;
        for (uint32_t face = 0; face < kFaceCount; face++)
            faces.push_back(MakePattern(kFaceBytes, 10u + face));

        {
            const UniqueHandle<TextureHandle> cubemap(
                device, device.CreateTexture(TextureDesc{
                            .Format = Format::RGBA8Unorm,
                            .Extent = kExtent,
                            .ArrayLayers = kFaceCount,
                            .Usage = TextureUsage::Sampled | TextureUsage::CopyDst |
                                     TextureUsage::CopySrc,
                            .bCubeCompatible = true,
                            .DebugName = "Round-trip Cubemap"}));

            // All six in one call, which IUploadContext requires: it transitions
            // the texture from Undefined, so a second batch would discard what
            // the first wrote.
            std::vector<TextureUpload> subresources;
            for (uint32_t face = 0; face < kFaceCount; face++)
            {
                subresources.push_back(
                    TextureUpload{.Data = faces[face], .BaseLayer = face, .Extent = kExtent});
            }

            const std::unique_ptr<IUploadContext> context =
                device.CreateUploadContext(UploadContextDesc{.DebugName = "Cubemap Round-trip"});

            context->UploadTexture(cubemap.Get(), subresources);
            context->Flush();

            const std::vector<std::vector<std::byte>> readBack =
                RhiTest::ReadTextureLayers(device, cubemap.Get());

            REQUIRE(readBack.size() == kFaceCount);

            for (uint32_t face = 0; face < kFaceCount; face++)
            {
                INFO("face " << face);
                REQUIRE(readBack[face] == faces[face]);
            }

            // The faces differing from one another is what says the assertions
            // above compared six distinct things. Six identical faces would
            // satisfy them just as well against a source that was also
            // identical, and that is exactly the bug being looked for.
            for (uint32_t face = 1; face < kFaceCount; face++)
                REQUIRE(readBack[face] != readBack[0]);
        }

        REQUIRE(Live(device) == before);
    }
}
