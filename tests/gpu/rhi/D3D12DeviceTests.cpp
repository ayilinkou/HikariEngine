#include <catch2/catch_test_macros.hpp>

#include <windows.h>

#include <cstdint>
#include <string>
#include <string_view>

#include <directx/d3d12.h>
#include <wrl/client.h>

#include <rhi/Diagnostics.h>
#include <rhi/IDevice.h>

#include "d3d12/D3D12Device.h"

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
