#include <catch2/catch_test_macros.hpp>

#include <windows.h>

#include <array>
#include <cstdint>
#include <cstring>
#include <memory>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

#include <directx/d3d12.h>
#include <wrl/client.h>

#include <rhi/BindGroup.h>
#include <rhi/BufferDesc.h>
#include <rhi/Diagnostics.h>
#include <rhi/ICommandAllocator.h>
#include <rhi/IDevice.h>
#include <rhi/SamplerDesc.h>
#include <rhi/TextureDesc.h>
#include <rhi/UniqueHandle.h>

#include "d3d12/D3D12CommandList.h"
#include "d3d12/D3D12Device.h"

#include "GpuReadback.h"
#include "RhiTestFixture.h"
#include "ValidationGuard.h"

/**
 * What only the D3D12 backend promises: its identity, the runtime it runs on, and
 * that its validation can actually speak. Tagged [d3d12], so only D3D12's
 * registration of the binary runs them.
 */
using namespace Hikari::Rhi;
using Microsoft::WRL::ComPtr;

namespace
{
/**
 * The shared device as the backend's own type. Skips rather than casting when the
 * process runs another backend, which only a run by hand without the registration's
 * test spec can reach.
 */
D3D12::D3D12Device& RequireD3D12Device()
{
    IDevice& device = RhiTest::RequireDevice();
    if (device.GetInfo().Backend != Backend::D3D12)
    {
        SKIP("A D3D12 case, in a process running " +
             std::string(ToString(device.GetInfo().Backend)));
    }

    return static_cast<D3D12::D3D12Device&>(device);
}

/** Where a loaded module came from, or empty when it is not loaded. */
std::wstring LoadedModulePath(const wchar_t* module)
{
    const HMODULE handle = GetModuleHandleW(module);
    if (handle == nullptr)
        return {};

    std::wstring path(MAX_PATH, L'\0');
    path.resize(GetModuleFileNameW(handle, path.data(), static_cast<DWORD>(path.size())));
    return path;
}

/** `file` inside D3D12\ beside this test binary, where the build deploys the SDK. */
std::wstring DeployedSdkPath(const wchar_t* file)
{
    std::wstring executable(MAX_PATH, L'\0');
    executable.resize(
        GetModuleFileNameW(nullptr, executable.data(), static_cast<DWORD>(executable.size())));
    return executable.substr(0, executable.find_last_of(L"\\/")) + L"\\D3D12\\" + file;
}
} // namespace

TEST_CASE("A D3D12 device reads DXIL, and neither flips clip-space Y nor presents",
          "[rhi][gpu][device][d3d12]")
{
    D3D12::D3D12Device& device = RequireD3D12Device();
    const RhiTest::ValidationGuard guard(device);

    // D3D12's clip space has Y up, as GLM produces it; a backend reporting a flip
    // renders upside down rather than failing.
    CHECK_FALSE(device.GetCaps().bFlipClipSpaceY);
    CHECK_FALSE(device.GetCaps().bPresentSupported);
    CHECK(std::string_view(device.GetCaps().ShaderExtension) == "dxil");

    CHECK(device.GetInfo().Backend == Backend::D3D12);

    // The floor the backend refuses below, spelled so a feature level never reads
    // as a version number.
    CHECK(device.GetInfo().ApiVersion.starts_with("feature level 12_"));
}

/**
 * Every failure to load the Agility SDK is silent — the in-box runtime answers
 * instead, with no debug layer on the Windows 10 machine this backend is built on —
 * so the tests prove which runtime they got rather than trusting the deployment.
 */
TEST_CASE("The D3D12 runtime and its debug layer are the Agility SDK beside the binary",
          "[rhi][gpu][device][d3d12]")
{
    RequireD3D12Device();

    CHECK(_wcsicmp(LoadedModulePath(L"D3D12Core.dll").c_str(),
                   DeployedSdkPath(L"D3D12Core.dll").c_str()) == 0);

    // The fixture enables validation, which is what loads the layers at all.
    CHECK(_wcsicmp(LoadedModulePath(L"d3d12SDKLayers.dll").c_str(),
                   DeployedSdkPath(L"d3d12SDKLayers.dll").c_str()) == 0);
}

namespace
{
/** The bytes an object holds under `guid`, empty when it holds none. */
std::vector<char> PrivateData(ID3D12Object& object, const GUID& guid)
{
    UINT size = 0u;
    if (FAILED(object.GetPrivateData(guid, &size, nullptr)) || size == 0u)
        return {};

    std::vector<char> data(size);
    if (FAILED(object.GetPrivateData(guid, &size, data.data())))
        return {};

    return data;
}

/** The UTF-8 name the debug layer's lifetime messages print, without its terminator. */
std::string NarrowDebugName(ID3D12Object& object)
{
    const std::vector<char> data = PrivateData(object, WKPDID_D3DDebugObjectName);
    std::string name(data.begin(), data.end());
    while (!name.empty() && name.back() == '\0')
        name.pop_back();

    return name;
}

/** The UTF-16 name SetName stores and the debug layer's errors print, without its terminator. */
std::wstring WideDebugName(ID3D12Object& object)
{
    const std::vector<char> data = PrivateData(object, WKPDID_D3DDebugObjectNameW);
    std::wstring name(data.size() / sizeof(wchar_t), L'\0');
    std::memcpy(name.data(), data.data(), name.size() * sizeof(wchar_t));
    while (!name.empty() && name.back() == L'\0')
        name.pop_back();

    return name;
}
} // namespace

/**
 * The debug layer reads an object's name in two encodings: its errors print the UTF-16
 * one SetName stores, and its object lifetime messages print the UTF-8 one, cutting the
 * UTF-16 name to eight bytes followed by stray memory when that is all there is. So a
 * name longer than eight bytes has to arrive in both, including on the lists an
 * allocator creates for itself.
 */
TEST_CASE("A D3D12 debug name is given in both encodings the debug layer reads",
          "[rhi][gpu][device][d3d12]")
{
    D3D12::D3D12Device& device = RequireD3D12Device();
    const RhiTest::ValidationGuard guard(device);

    const UniqueHandle<BufferHandle> buffer(
        device, device.CreateBuffer(BufferDesc{.Size = 64u,
                                               .Usage = BufferUsage::Storage,
                                               .DebugName = "A Buffer Named Past Eight Bytes"}));
    const D3D12::D3D12Buffer* pBuffer = device.FindBuffer(buffer.Get());
    REQUIRE(pBuffer != nullptr);

    CHECK(NarrowDebugName(*pBuffer->Resource.Get()) == "A Buffer Named Past Eight Bytes");
    CHECK(WideDebugName(*pBuffer->Resource.Get()) == L"A Buffer Named Past Eight Bytes");

    // Numbered by acquisition, as the Vulkan backend numbers its command buffers.
    const std::unique_ptr<ICommandAllocator> allocator = device.CreateCommandAllocator(
        CommandAllocatorDesc{.Queue = QueueType::Graphics, .DebugName = "Named Allocator"});
    ID3D12CommandList* pList = static_cast<D3D12::D3D12CommandList&>(allocator->Acquire()).Native();
    REQUIRE(pList != nullptr);

    CHECK(NarrowDebugName(*pList) == "Named Allocator [0]");
    CHECK(WideDebugName(*pList) == L"Named Allocator [0]");
}

namespace
{
/**
 * A D3D12 device of its own with the given descriptor capacities, for the cases that
 * need a heap small enough to fill. Skips where the process runs another backend.
 */
std::unique_ptr<IDevice> MakeD3D12DeviceWithCapacity(Diagnostics& diagnostics, uint32_t resources,
                                                     uint32_t samplers)
{
    RequireD3D12Device();

    DeviceDesc desc = RhiTest::Detail::MakeDesc(RhiTest::DeviceConfig::Default, diagnostics);
    desc.ResourceDescriptorCapacity = resources;
    desc.SamplerDescriptorCapacity = samplers;
    return CreateDevice(desc);
}
} // namespace

/**
 * Identical samplers share their descriptors, which is what keeps a scene of many
 * materials inside the sampler heap: sixteen groups with the same sampler fit a heap
 * of four sampler descriptors only if they share.
 */
TEST_CASE("Bind groups with identical samplers share their sampler descriptors",
          "[rhi][gpu][bindgroups][d3d12]")
{
    Diagnostics diagnostics;
    const std::unique_ptr<IDevice> device = MakeD3D12DeviceWithCapacity(diagnostics, 64u, 4u);

    const BindGroupLayoutBinding binding{.Slot = 0u,
                                         .Type = BindingType::Sampler,
                                         .Visibility = ShaderStage::Pixel,
                                         .bOptional = false};
    const BindGroupLayoutHandle layout = device->CreateBindGroupLayout(BindGroupLayoutDesc{
        .Bindings = std::span<const BindGroupLayoutBinding>(&binding, 1), .DebugName = "Layout"});

    std::vector<SamplerHandle> samplers;
    std::vector<BindGroupHandle> groups;
    for (int i = 0; i < 16; ++i)
    {
        // Separate sampler objects describing the same sampler.
        samplers.push_back(device->CreateSampler(SamplerDesc{.DebugName = "Same"}));
        const BindGroupBinding entry{.Slot = 0u,
                                     .Type = BindingType::Sampler,
                                     .Buffer = {},
                                     .View = {},
                                     .Sampler = samplers.back()};
        groups.push_back(device->CreateBindGroup(
            BindGroupDesc{.Layout = layout,
                          .Bindings = std::span<const BindGroupBinding>(&entry, 1),
                          .DebugName = "Group"}));
    }

    CHECK(device->GetLiveBindGroupCount() == 16u);
    CHECK(diagnostics.ErrorCount() == 0u);

    for (const BindGroupHandle group : groups)
        device->Destroy(group);
    for (const SamplerHandle sampler : samplers)
        device->Destroy(sampler);
    device->Destroy(layout);
}

/**
 * The heap never grows, so running out has to say which number to raise rather than
 * failing somewhere inside the driver.
 */
TEST_CASE("A full descriptor heap refuses a bind group, naming its capacity field",
          "[rhi][gpu][bindgroups][d3d12]")
{
    Diagnostics diagnostics;
    const std::unique_ptr<IDevice> device = MakeD3D12DeviceWithCapacity(diagnostics, 4u, 4u);

    const std::array bindings{
        BindGroupLayoutBinding{.Slot = 0u,
                               .Type = BindingType::UniformBuffer,
                               .Visibility = ShaderStage::Vertex,
                               .bOptional = false},
        BindGroupLayoutBinding{.Slot = 1u,
                               .Type = BindingType::UniformBuffer,
                               .Visibility = ShaderStage::Vertex,
                               .bOptional = false},
    };
    const BindGroupLayoutHandle layout = device->CreateBindGroupLayout(
        BindGroupLayoutDesc{.Bindings = bindings, .DebugName = "Two Buffers"});
    const BufferHandle buffer = device->CreateBuffer(BufferDesc{.Size = 256u,
                                                                .Usage = BufferUsage::Uniform,
                                                                .Access = MemoryAccess::CpuToGpu,
                                                                .DebugName = "Buffer"});

    const std::array entries{
        BindGroupBinding{
            .Slot = 0u, .Type = BindingType::UniformBuffer, .Buffer = buffer, .View = {}, .Sampler = {}},
        BindGroupBinding{
            .Slot = 1u, .Type = BindingType::UniformBuffer, .Buffer = buffer, .View = {}, .Sampler = {}},
    };
    const BindGroupDesc groupDesc{.Layout = layout, .Bindings = entries, .DebugName = "Group"};

    // Two groups of two fill four descriptors exactly; the third has nowhere to go.
    const BindGroupHandle first = device->CreateBindGroup(groupDesc);
    const BindGroupHandle second = device->CreateBindGroup(groupDesc);

    try
    {
        const BindGroupHandle third = device->CreateBindGroup(groupDesc);
        device->Destroy(third);
        FAIL("a third group was created in a heap already full");
    }
    catch (const std::runtime_error& error)
    {
        CHECK(std::string(error.what()).find("ResourceDescriptorCapacity") != std::string::npos);
    }

    // Released ranges are reused.
    device->Destroy(first);
    const BindGroupHandle reused = device->CreateBindGroup(groupDesc);

    device->Destroy(reused);
    device->Destroy(second);
    device->Destroy(buffer);
    device->Destroy(layout);
}

/**
 * The positive control for every D3D12 case that asserts validation stayed quiet: a
 * legacy transition whose before-state is wrong, which the debug layer reports at
 * ExecuteCommandLists (measured on the RX 580 and NuGet WARP).
 *
 * Raw D3D12 rather than the RHI, because the error has to be committed on purpose
 * and the RHI's barriers exist to make it hard to commit. What it proves about the
 * RHI is the part that is easy to get wrong silently: that the backend's polling
 * carries the layer's message into Diagnostics as an error.
 */
TEST_CASE("The D3D12 debug layer reports a barrier from the wrong before-state",
          "[rhi][gpu][validation][d3d12]")
{
    D3D12::D3D12Device& device = RequireD3D12Device();
    Diagnostics& diagnostics = device.GetDiagnostics();

    // Not ValidationGuard: this case wants an error rather than its absence, so it
    // manages the counters itself and clears them on the way out.
    diagnostics.Reset();

    {
        ID3D12Device& native = device.GetNativeDevice();

        D3D12_HEAP_PROPERTIES heap{};
        heap.Type = D3D12_HEAP_TYPE_DEFAULT;

        D3D12_RESOURCE_DESC textureDesc{};
        textureDesc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
        textureDesc.Width = 16;
        textureDesc.Height = 16;
        textureDesc.DepthOrArraySize = 1;
        textureDesc.MipLevels = 1;
        textureDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
        textureDesc.SampleDesc.Count = 1;
        textureDesc.Flags = D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET;

        ComPtr<ID3D12Resource> texture;
        REQUIRE(SUCCEEDED(native.CreateCommittedResource(&heap, D3D12_HEAP_FLAG_NONE, &textureDesc,
                                                         D3D12_RESOURCE_STATE_COMMON, nullptr,
                                                         IID_PPV_ARGS(&texture))));

        D3D12_COMMAND_QUEUE_DESC queueDesc{};
        queueDesc.Type = D3D12_COMMAND_LIST_TYPE_DIRECT;
        ComPtr<ID3D12CommandQueue> queue;
        REQUIRE(SUCCEEDED(native.CreateCommandQueue(&queueDesc, IID_PPV_ARGS(&queue))));

        ComPtr<ID3D12CommandAllocator> allocator;
        REQUIRE(SUCCEEDED(native.CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT,
                                                        IID_PPV_ARGS(&allocator))));

        ComPtr<ID3D12GraphicsCommandList> list;
        REQUIRE(SUCCEEDED(native.CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT,
                                                   allocator.Get(), nullptr,
                                                   IID_PPV_ARGS(&list))));

        // The deliberate error: the texture was created in COMMON, and the barrier
        // claims it is in RENDER_TARGET.
        D3D12_RESOURCE_BARRIER barrier{};
        barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
        barrier.Transition.pResource = texture.Get();
        barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_RENDER_TARGET;
        barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
        barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
        list->ResourceBarrier(1, &barrier);
        REQUIRE(SUCCEEDED(list->Close()));

        ID3D12CommandList* lists[] = {list.Get()};
        queue->ExecuteCommandLists(1, lists);

        // GPU-based validation reports what it sees after the GPU executes, so the
        // drain below waits for that; a report arriving later would land in
        // whichever case drained next.
        ComPtr<ID3D12Fence> fence;
        REQUIRE(SUCCEEDED(native.CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&fence))));
        REQUIRE(SUCCEEDED(queue->Signal(fence.Get(), 1)));

        const HANDLE completed = CreateEventW(nullptr, FALSE, FALSE, nullptr);
        REQUIRE(completed != nullptr);
        REQUIRE(SUCCEEDED(fence->SetEventOnCompletion(1, completed)));
        const DWORD waited = WaitForSingleObject(completed, 30'000);
        CloseHandle(completed);
        REQUIRE(waited == WAIT_OBJECT_0);
    }

    device.DrainDebugMessages();

    std::string recent;
    for (const std::string& message : diagnostics.RecentMessages())
        recent += "\n  " + message;
    INFO("messages:" << recent);

    const uint64_t errors = diagnostics.ErrorCount();
    diagnostics.Reset();

    CHECK(errors >= 1u);
}

/**
 * Auto takes the adapter's best barrier path, so the shared device says which this
 * adapter has. Legacy is always given when named; enhanced is given where the adapter
 * has it and refused where it does not, naming the capability, rather than quietly
 * running legacy — which would make a legacy-against-enhanced comparison compare legacy
 * with itself.
 */
TEST_CASE("A named barrier path is the path taken, and enhanced is refused where unsupported",
          "[rhi][gpu][device][d3d12]")
{
    const bool bEnhancedSupported =
        RequireD3D12Device().GetInfo().BarrierPath == BarrierPath::Enhanced;

    Diagnostics diagnostics;

    SECTION("legacy")
    {
        DeviceDesc desc = RhiTest::Detail::MakeDesc(RhiTest::DeviceConfig::Default, diagnostics);
        desc.BarrierPath = BarrierPath::Legacy;

        const std::unique_ptr<IDevice> device = CreateDevice(desc);
        CHECK(device->GetInfo().BarrierPath == BarrierPath::Legacy);
    }

    SECTION("enhanced")
    {
        DeviceDesc desc = RhiTest::Detail::MakeDesc(RhiTest::DeviceConfig::Default, diagnostics);
        desc.BarrierPath = BarrierPath::Enhanced;

        if (bEnhancedSupported)
        {
            const std::unique_ptr<IDevice> device = CreateDevice(desc);
            CHECK(device->GetInfo().BarrierPath == BarrierPath::Enhanced);
        }
        else
        {
            try
            {
                const std::unique_ptr<IDevice> device = CreateDevice(desc);
                FAIL("enhanced barriers were given on an adapter that reports no support");
            }
            catch (const std::runtime_error& error)
            {
                CHECK(std::string_view(error.what()).find("EnhancedBarriersSupported") !=
                      std::string_view::npos);
            }
        }
    }
}

/**
 * The enhanced path's positive control, recorded through the RHI: a barrier naming a
 * layout the texture is not in. The debug layer tracks enhanced layouts itself, so a
 * clean run of every other case proves something only if this one fails.
 */
TEST_CASE("The D3D12 debug layer reports an enhanced barrier from the wrong layout",
          "[rhi][gpu][validation][d3d12]")
{
    D3D12::D3D12Device& device = RequireD3D12Device();
    if (!device.UsesEnhancedBarriers())
        SKIP("This adapter records legacy barriers.");

    Diagnostics& diagnostics = device.GetDiagnostics();
    diagnostics.Reset();

    {
        const UniqueHandle<TextureHandle> texture(
            device, device.CreateTexture(
                        TextureDesc{.Format = Format::RGBA8Unorm,
                                    .Extent = {16u, 16u, 1u},
                                    .Usage = TextureUsage::ColorAttachment | TextureUsage::Sampled,
                                    .DebugName = "Wrong Layout Texture"}));

        // The deliberate error: a new texture is in COMMON, and the barrier claims it
        // is a render target.
        RhiTest::RunGraphicsCommands(
            device,
            [&](ICommandList& list)
            {
                list.Barrier(TextureBarrier{.Texture = texture.Get(),
                                            .SrcStage = PipelineStage::RenderTarget,
                                            .SrcAccess = AccessFlags::RenderTargetWrite,
                                            .DstStage = PipelineStage::PixelStage,
                                            .DstAccess = AccessFlags::ShaderRead,
                                            .OldLayout = TextureLayout::RenderTarget,
                                            .NewLayout = TextureLayout::ShaderResource});
            });
    }

    device.DrainDebugMessages();

    std::string recent;
    for (const std::string& message : diagnostics.RecentMessages())
        recent += "\n  " + message;
    INFO("messages:" << recent);

    const uint64_t errors = diagnostics.ErrorCount();
    diagnostics.Reset();

    CHECK(errors >= 1u);
}
