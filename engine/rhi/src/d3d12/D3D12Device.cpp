#include "d3d12/D3D12Device.h"

#include <algorithm>
#include <array>
#include <cstring>
#include <format>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <core/Log.h>

#include "AdapterName.h"
#include "TextureValidation.h"
#include "d3d12/D3D12CommandAllocator.h"
#include "d3d12/D3D12CommandList.h"
#include "d3d12/D3D12Conversions.h"
#include "d3d12/D3D12DebugName.h"
#include "d3d12/D3D12DeviceFactory.h"
#include "d3d12/D3D12OffscreenTarget.h"
#include "d3d12/D3D12PresentTarget.h"
#include "d3d12/D3D12SwapchainTarget.h"
#include "d3d12/D3D12UploadContext.h"

namespace Hikari::Rhi::D3D12
{
constexpr Core::LogCategory LogRhi("RHI");

namespace
{
using Microsoft::WRL::ComPtr;

/**
 * The lowest feature level this backend accepts. 12_0 guarantees resource binding
 * tier 2 and shader model 6.0 — what the blobs are compiled to — and it is the RX
 * 580's highest level, which the backend has to run on. 11_0 is refused because
 * its binding tier forbids unpopulated descriptor table entries, which the
 * material's optional textures would need a null-descriptor path to avoid.
 */
constexpr D3D_FEATURE_LEVEL kMinimumFeatureLevel = D3D_FEATURE_LEVEL_12_0;

std::string HResultText(HRESULT hr)
{
    return std::format("0x{:08X}", static_cast<uint32_t>(hr));
}

/**
 * D3D12_ERROR_INVALID_REDIST — "the D3D12 SDK version configuration of the host
 * exe is invalid" — is what every D3D12 call returns when the Agility SDK the
 * executable opted into cannot be loaded, most often because D3D12\ beside it is
 * missing. It describes the process rather than any adapter, so it ends selection
 * with its real cause instead of being listed against each adapter as though each
 * had refused.
 */
void ThrowIfInvalidRedist(HRESULT hr)
{
    if (hr != D3D12_ERROR_INVALID_REDIST)
        return;

    throw std::runtime_error(std::format(
        "The D3D12 runtime refused this executable's Agility SDK configuration ({}): "
        "D3D12\\D3D12Core.dll beside the executable is missing or unreadable. The build "
        "deploys it; rebuilding the executable restores it.",
        HResultText(hr)));
}

std::string_view FeatureLevelName(D3D_FEATURE_LEVEL level)
{
    switch (level)
    {
        case D3D_FEATURE_LEVEL_12_0:
            return "12_0";
        case D3D_FEATURE_LEVEL_12_1:
            return "12_1";
        case D3D_FEATURE_LEVEL_12_2:
            return "12_2";
        default:
            return "unknown";
    }
}

/**
 * Why `device` cannot run this backend, or nothing when it can.
 *
 * The feature level is already settled by the time this runs, since
 * D3D12CreateDevice refused anything below the minimum. What remains is the
 * unrestricted buffer-texture copy pitch: without it D3D12 requires a readback's
 * row pitch aligned to 256 bytes and its offsets to 512, so the tightly packed
 * buffers BufferTextureCopyRegion describes would be the wrong size at any width
 * not a multiple of 64. With it, offsets and row pitch need align only to the
 * texel — the contract Vulkan already has, which is why the seam carries none.
 */
std::optional<std::string> FindRefusal(ID3D12Device& device)
{
    D3D12_FEATURE_DATA_D3D12_OPTIONS13 options13{};
    const HRESULT hr =
        device.CheckFeatureSupport(D3D12_FEATURE_D3D12_OPTIONS13, &options13, sizeof(options13));
    if (FAILED(hr))
        return std::format("does not answer D3D12_OPTIONS13 ({})", HResultText(hr));

    if (!options13.UnrestrictedBufferTextureCopyPitchSupported)
        return std::string("no unrestricted buffer-texture copy pitch");

    return std::nullopt;
}

/**
 * The driver's version as Windows displays it. Since WDDM 2.3 every component of a
 * driver package shares one version number, so asking for the IDXGIDevice
 * interface's user-mode driver answers for the D3D12 driver too.
 */
std::string DriverVersion(IDXGIAdapter1& adapter)
{
    LARGE_INTEGER version{};
    if (FAILED(adapter.CheckInterfaceSupport(__uuidof(IDXGIDevice), &version)))
        return "unknown";

    const auto part = [&version](int shift)
    { return static_cast<uint32_t>((version.QuadPart >> shift) & 0xFFFF); };
    return std::format("{}.{}.{}.{}", part(48), part(32), part(16), part(0));
}
} // namespace

std::unique_ptr<IDevice> CreateD3D12Device(const DeviceDesc& desc)
{
    return std::make_unique<D3D12Device>(desc);
}

D3D12Device::D3D12Device(const DeviceDesc& desc)
    : m_OwnedDiagnostics(desc.pDiagnostics ? nullptr : std::make_unique<Diagnostics>()),
      m_pDiagnostics(desc.pDiagnostics ? desc.pDiagnostics : m_OwnedDiagnostics.get())
{
    // Reported and ignored, as the field promises for a name the backend does not
    // recognise: D3D12 has no optional extensions to pretend away.
    for (const std::string& name : desc.DisabledOptionalExtensions)
    {
        Core::LogMsg(Core::LogSeverity::Warning, LogRhi,
                     "Ignoring disabled extension {}: D3D12 has no optional extensions.", name);
    }

    if (desc.bEnableValidation)
        EnableDebugLayer(desc);

    CreateFactory();
    SelectAdapter(desc);

    if (desc.bEnableValidation)
        m_pDebugMessages = std::make_unique<D3D12DebugMessages>(*m_Device.Get(), *m_pDiagnostics);

    CreateAllocator();

    m_bSingleQueue = desc.bForceSingleQueue;
    m_bWindowed = desc.Requirements.bPresent;
    m_pNativeWindow = desc.Requirements.NativeWindowHandle;
    if (m_bWindowed && m_pNativeWindow == nullptr)
    {
        throw std::runtime_error(
            "Rhi::CreateDevice: presentation was required and no window was given to present to.");
    }
    CreateQueues();
    CreateDescriptorHeaps(desc);

    FillDeviceInfo();
    ResolveBarrierPath(desc);

    // D3D12's clip space already has Y up, as GLM's projection produces it.
    m_Caps.bFlipClipSpaceY = false;
    m_Caps.ShaderExtension = "dxil";

    // A windowed device presents through a DXGI swapchain on its direct queue; a
    // headless one renders offscreen.
    m_Caps.bPresentSupported = m_bWindowed;

    // Every D3D12 device offers compute and copy queues beside the direct one, so the
    // device has them unless it was asked to behave as though it had one queue for
    // every role — which is what these describe, not where the RHI submits.
    m_Caps.bHasDedicatedComputeQueue = !m_bSingleQueue;
    m_Caps.bHasDedicatedCopyQueue = !m_bSingleQueue;

    Core::LogMsg(Core::LogSeverity::Info, LogRhi,
                 "D3D12 device: {} (vendor 0x{:04X}, device 0x{:04X}), driver {}, {}, {} barriers",
                 m_Info.Gpu, m_Info.VendorId, m_Info.DeviceId, m_Info.Driver, m_Info.ApiVersion,
                 ToString(*m_Info.BarrierPath));
    Core::LogMsg(Core::LogSeverity::Info, LogRhi, "Agility SDK {} loaded from {}",
                 m_AgilitySdk.Version, m_AgilitySdk.CorePath);

    DrainDebugMessages();
}

/**
 * A hard requirement once validation is asked for, exactly as Vulkan's layer is. On
 * the machine this backend is built on the layer is absent unless the Agility SDK's
 * d3d12SDKLayers.dll is deployed beside its core, so a layer that quietly failed to
 * load would let every D3D12 run pass while checking nothing.
 */
void D3D12Device::EnableDebugLayer(const DeviceDesc& desc)
{
    ComPtr<ID3D12Debug1> debug;
    const HRESULT hr = D3D12GetDebugInterface(IID_PPV_ARGS(&debug));
    ThrowIfInvalidRedist(hr);
    if (FAILED(hr))
    {
        throw std::runtime_error(std::format(
            "Validation was asked for and the D3D12 debug layer did not load ({}). It comes from "
            "d3d12SDKLayers.dll in D3D12\\ beside the executable, which the build deploys; "
            "without it D3D12GetDebugInterface fails with DXGI_ERROR_SDK_COMPONENT_MISSING.",
            HResultText(hr)));
    }

    debug->EnableDebugLayer();

    // Only before a device exists: switching it on an existing device removes the
    // device.
    debug->SetEnableGPUBasedValidation(desc.GpuBasedValidation != GpuBasedValidation::Off ? TRUE
                                                                                          : FALSE);

    // Descriptors keeps the shader patching and drops resource-state tracking, which
    // the flag's documentation says "greatly reduces the performance cost of GPU-based
    // validation", with descriptors and descriptor heaps still validated. Refused
    // rather than run in full when the interface is missing, so a run never reports a
    // level it did not have.
    if (desc.GpuBasedValidation == GpuBasedValidation::Descriptors)
    {
        ComPtr<ID3D12Debug2> debug2;
        if (FAILED(debug.As(&debug2)))
        {
            throw std::runtime_error("GPU-based validation of descriptors alone needs "
                                     "ID3D12Debug2, which this debug layer does not provide.");
        }

        debug2->SetGPUBasedValidationFlags(D3D12_GPU_BASED_VALIDATION_FLAGS_DISABLE_STATE_TRACKING);
    }

    Core::LogMsg(Core::LogSeverity::Info, LogRhi,
                 "D3D12 debug layer enabled, GPU-based validation: {}",
                 ToString(desc.GpuBasedValidation));
}

D3D12Device::~D3D12Device()
{
    // A resource still alive here was never destroyed: not a crash, since the pools
    // release their allocations before the allocator goes, but a leak for as long as
    // the device ran. Reported rather than asserted, so that a shutdown already
    // unwinding from an error is not made worse.
    const std::array<std::pair<const char*, uint32_t>, 4> live{
        std::pair{"buffer", m_Buffers.Size()},
        std::pair{"texture", m_Textures.Size()},
        std::pair{"texture view", m_TextureViews.Size()},
        std::pair{"sampler", m_Samplers.Size()},
    };
    for (const auto& [kind, count] : live)
    {
        if (count == 0u)
            continue;

        Core::LogMsg(Core::LogSeverity::Warning, LogRhi,
                     "Device destroyed with {} {}(s) still alive — each is a resource whose "
                     "owner never released it.",
                     count, kind);
    }
}

/**
 * D3D12MA, which D25 chose as VMA's counterpart. Recommended flags: default pools are
 * not zeroed, since every buffer this engine creates is written before it is read,
 * and MSAA textures are always committed.
 */
void D3D12Device::CreateAllocator()
{
    D3D12MA::ALLOCATOR_DESC allocatorDesc{};
    allocatorDesc.Flags =
        static_cast<D3D12MA::ALLOCATOR_FLAGS>(D3D12MA_RECOMMENDED_ALLOCATOR_FLAGS);
    allocatorDesc.pDevice = m_Device.Get();
    allocatorDesc.pAdapter = m_Adapter.Get();

    const HRESULT hr = D3D12MA::CreateAllocator(&allocatorDesc, &m_Allocator);
    if (FAILED(hr))
        throw std::runtime_error(
            std::format("D3D12MA::CreateAllocator failed ({})", HResultText(hr)));
}

void D3D12Device::ReportError(const std::string& message)
{
    m_pDiagnostics->Report(DiagnosticSeverity::Error, message);
}

/**
 * A direct queue always, and a copy queue unless the device is to behave as though it
 * had one queue: then copies are recorded as direct lists and go to the direct queue,
 * the path an integrated GPU's single universal queue family takes on Vulkan. No
 * compute queue: dispatches go to the direct queue, as they go to the graphics queue
 * on Vulkan, until a pass needs them overlapped with rendering.
 */
void D3D12Device::CreateQueues()
{
    const auto create = [this](D3D12_COMMAND_LIST_TYPE type, const std::string& name)
    {
        D3D12_COMMAND_QUEUE_DESC queueDesc{};
        queueDesc.Type = type;

        Microsoft::WRL::ComPtr<ID3D12CommandQueue> queue;
        const HRESULT hr = m_Device->CreateCommandQueue(&queueDesc, IID_PPV_ARGS(&queue));
        if (FAILED(hr))
        {
            throw std::runtime_error(
                std::format("CreateCommandQueue failed ({})", HResultText(hr)));
        }

        SetDebugName(*queue.Get(), name);
        return queue;
    };

    m_DirectQueue = create(D3D12_COMMAND_LIST_TYPE_DIRECT, "Direct Queue");
    if (!m_bSingleQueue)
        m_CopyQueue = create(D3D12_COMMAND_LIST_TYPE_COPY, "Copy Queue");

    const HRESULT hr = m_Device->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&m_IdleFence));
    if (FAILED(hr))
        throw std::runtime_error(std::format("CreateFence failed ({})", HResultText(hr)));

    SetDebugName(*m_IdleFence.Get(), "Idle Fence");

    // Sized for every rendering target alive at once — a few per frame in flight — with
    // room to spare. Exhaustion throws naming the heap rather than growing.
    m_RenderTargetHeap = std::make_unique<D3D12CpuDescriptorHeap>(
        *m_Device.Get(), D3D12_DESCRIPTOR_HEAP_TYPE_RTV, 256u, "Render Target Views");
    m_DepthStencilHeap = std::make_unique<D3D12CpuDescriptorHeap>(
        *m_Device.Get(), D3D12_DESCRIPTOR_HEAP_TYPE_DSV, 64u, "Depth Stencil Views");

    DrainDebugMessages();
}

void D3D12Device::CreateDescriptorHeaps(const DeviceDesc& desc)
{
    m_ResourceHeap = std::make_unique<D3D12GpuDescriptorHeap>(
        *m_Device.Get(), D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV, desc.ResourceDescriptorCapacity,
        "ResourceDescriptorCapacity", "Resource Descriptors");
    m_SamplerHeap = std::make_unique<D3D12GpuDescriptorHeap>(
        *m_Device.Get(), D3D12_DESCRIPTOR_HEAP_TYPE_SAMPLER, desc.SamplerDescriptorCapacity,
        "SamplerDescriptorCapacity", "Sampler Descriptors");
    DrainDebugMessages();
}

void D3D12Device::BindDescriptorHeaps(ID3D12GraphicsCommandList& list) const
{
    // A copy list has no descriptor heaps to bind.
    ID3D12DescriptorHeap* heaps[] = {m_ResourceHeap->Native(), m_SamplerHeap->Native()};
    list.SetDescriptorHeaps(2, heaps);
}

Format D3D12Device::TextureFormatOf(TextureViewHandle view) const
{
    const D3D12TextureView* pView = m_TextureViews.Get(view);
    const D3D12Texture* pTexture = pView ? m_Textures.Get(pView->Desc.Texture) : nullptr;
    return pTexture ? pTexture->Desc.Format : Format::Undefined;
}

bool D3D12Device::RenderAreaCoversView(TextureViewHandle view, const Rect2D& area) const
{
    const D3D12TextureView* pView = m_TextureViews.Get(view);
    const D3D12Texture* pTexture = pView ? m_Textures.Get(pView->Desc.Texture) : nullptr;
    if (pTexture == nullptr)
        return false;

    const uint32_t mip = pView->Desc.BaseMip;
    const uint32_t width = std::max(pTexture->Desc.Extent.Width >> mip, 1u);
    const uint32_t height = std::max(pTexture->Desc.Extent.Height >> mip, 1u);
    return area.Offset.X <= 0 && area.Offset.Y <= 0 &&
           static_cast<int64_t>(area.Offset.X) + area.Extent.Width >= width &&
           static_cast<int64_t>(area.Offset.Y) + area.Extent.Height >= height;
}

D3D12_CPU_DESCRIPTOR_HANDLE D3D12Device::RenderTargetViewFor(TextureViewHandle view)
{
    const std::lock_guard lock(m_ViewMutex);

    D3D12TextureView* pView = m_TextureViews.Get(view);
    const D3D12Texture* pTexture = pView ? m_Textures.Get(pView->Desc.Texture) : nullptr;
    if (pTexture == nullptr)
    {
        throw std::runtime_error(std::format(
            "Rhi::ICommandList::BeginRendering: view handle {:#010x} or its texture is stale.",
            view.Value));
    }

    if (!pView->RenderTargetSlot)
    {
        const TextureViewDesc& desc = pView->Desc;
        D3D12_RENDER_TARGET_VIEW_DESC viewDesc{};
        viewDesc.Format = ToDxgi(desc.Format);

        if (pTexture->Desc.Dimension == TextureDimension::Texture3D)
        {
            viewDesc.ViewDimension = D3D12_RTV_DIMENSION_TEXTURE3D;
            viewDesc.Texture3D.MipSlice = desc.BaseMip;
            viewDesc.Texture3D.FirstWSlice = desc.BaseLayer;
            viewDesc.Texture3D.WSize = desc.LayerCount;
        }
        else if (pTexture->Desc.ArrayLayers > 1u)
        {
            viewDesc.ViewDimension = D3D12_RTV_DIMENSION_TEXTURE2DARRAY;
            viewDesc.Texture2DArray.MipSlice = desc.BaseMip;
            viewDesc.Texture2DArray.FirstArraySlice = desc.BaseLayer;
            viewDesc.Texture2DArray.ArraySize = desc.LayerCount;
        }
        else
        {
            viewDesc.ViewDimension = D3D12_RTV_DIMENSION_TEXTURE2D;
            viewDesc.Texture2D.MipSlice = desc.BaseMip;
        }

        const uint32_t slot = m_RenderTargetHeap->Allocate("render-target");
        m_Device->CreateRenderTargetView(pTexture->Resource.Get(), &viewDesc,
                                         m_RenderTargetHeap->HandleAt(slot));
        pView->RenderTargetSlot = slot;
        DrainDebugMessages();
    }

    return m_RenderTargetHeap->HandleAt(*pView->RenderTargetSlot);
}

D3D12_CPU_DESCRIPTOR_HANDLE D3D12Device::DepthStencilViewFor(TextureViewHandle view, bool bReadOnly)
{
    const std::lock_guard lock(m_ViewMutex);

    D3D12TextureView* pView = m_TextureViews.Get(view);
    const D3D12Texture* pTexture = pView ? m_Textures.Get(pView->Desc.Texture) : nullptr;
    if (pTexture == nullptr)
    {
        throw std::runtime_error(std::format("Rhi::ICommandList::BeginRendering: depth view handle "
                                             "{:#010x} or its texture is stale.",
                                             view.Value));
    }

    std::optional<uint32_t>& slot =
        bReadOnly ? pView->ReadOnlyDepthStencilSlot : pView->DepthStencilSlot;
    if (!slot)
    {
        const TextureViewDesc& desc = pView->Desc;
        D3D12_DEPTH_STENCIL_VIEW_DESC viewDesc{};
        // The typed depth format, whatever the resource was created as: a sampled
        // depth texture's resource is typeless, and its depth view names the depth.
        viewDesc.Format = ToDxgi(pTexture->Desc.Format);

        const bool bStencil = pTexture->Desc.Format == Format::D24UnormS8Uint ||
                              pTexture->Desc.Format == Format::D32FloatS8Uint;
        if (bReadOnly)
        {
            viewDesc.Flags = bStencil
                                 ? D3D12_DSV_FLAG_READ_ONLY_DEPTH | D3D12_DSV_FLAG_READ_ONLY_STENCIL
                                 : D3D12_DSV_FLAG_READ_ONLY_DEPTH;
        }

        if (pTexture->Desc.ArrayLayers > 1u)
        {
            viewDesc.ViewDimension = D3D12_DSV_DIMENSION_TEXTURE2DARRAY;
            viewDesc.Texture2DArray.MipSlice = desc.BaseMip;
            viewDesc.Texture2DArray.FirstArraySlice = desc.BaseLayer;
            viewDesc.Texture2DArray.ArraySize = desc.LayerCount;
        }
        else
        {
            viewDesc.ViewDimension = D3D12_DSV_DIMENSION_TEXTURE2D;
            viewDesc.Texture2D.MipSlice = desc.BaseMip;
        }

        const uint32_t index = m_DepthStencilHeap->Allocate("depth-stencil");
        m_Device->CreateDepthStencilView(pTexture->Resource.Get(), &viewDesc,
                                         m_DepthStencilHeap->HandleAt(index));
        slot = index;
        DrainDebugMessages();
    }

    return m_DepthStencilHeap->HandleAt(*slot);
}

D3D12_COMMAND_LIST_TYPE D3D12Device::ListTypeFor(QueueType queue) const
{
    switch (queue)
    {
        case QueueType::Graphics:
        case QueueType::Compute:
            return D3D12_COMMAND_LIST_TYPE_DIRECT;
        case QueueType::Copy:
            return m_bSingleQueue ? D3D12_COMMAND_LIST_TYPE_DIRECT : D3D12_COMMAND_LIST_TYPE_COPY;
    }

    return D3D12_COMMAND_LIST_TYPE_DIRECT;
}

ID3D12CommandQueue& D3D12Device::QueueFor(QueueType queue) const
{
    return ListTypeFor(queue) == D3D12_COMMAND_LIST_TYPE_COPY ? *m_CopyQueue.Get()
                                                              : *m_DirectQueue.Get();
}

ID3D12Resource* D3D12Device::FindBufferResource(BufferHandle handle) const
{
    const D3D12Buffer* pBuffer = m_Buffers.Get(handle);
    return pBuffer ? pBuffer->Resource.Get() : nullptr;
}

D3D12_RESOURCE_STATES D3D12Device::SubmittedStateOf(TextureHandle handle) const
{
    const std::lock_guard lock(m_StateMutex);
    const D3D12Texture* pTexture = m_Textures.Get(handle);
    return pTexture ? pTexture->SubmittedState : D3D12_RESOURCE_STATE_COMMON;
}

void D3D12Device::CreateFactory()
{
    const HRESULT hr = CreateDXGIFactory2(0, IID_PPV_ARGS(&m_Factory));
    if (FAILED(hr))
        throw std::runtime_error(std::format("CreateDXGIFactory2 failed ({})", HResultText(hr)));
}

/**
 * The first suitable adapter in DXGI's enumeration order, among those whose name
 * matches DeviceDesc::Gpu when it names one.
 *
 * Suitability needs a device to ask, so each candidate gets one until an adapter
 * passes; D3D12CreateDevice itself refuses anything below the minimum feature
 * level. Every adapter that was passed over is listed in the refusal, with why.
 */
void D3D12Device::SelectAdapter(const DeviceDesc& desc)
{
    std::vector<std::string> considered;
    bool bAgilitySdkVerified = false;

    for (UINT index = 0;; ++index)
    {
        ComPtr<IDXGIAdapter1> adapter;
        const HRESULT enumResult = m_Factory->EnumAdapters1(index, &adapter);
        if (enumResult == DXGI_ERROR_NOT_FOUND)
            break;

        if (FAILED(enumResult))
        {
            throw std::runtime_error(
                std::format("EnumAdapters1({}) failed ({})", index, HResultText(enumResult)));
        }

        DXGI_ADAPTER_DESC1 adapterDesc{};
        adapter->GetDesc1(&adapterDesc);
        const std::string name = Utf8FromWide(adapterDesc.Description);

        if (!AdapterNameMatches(name, desc.Gpu))
        {
            considered.push_back(std::format("{} — does not match the requested name", name));
            continue;
        }

        ComPtr<ID3D12Device> device;
        const HRESULT createResult =
            D3D12CreateDevice(adapter.Get(), kMinimumFeatureLevel, IID_PPV_ARGS(&device));
        ThrowIfInvalidRedist(createResult);
        if (FAILED(createResult))
        {
            considered.push_back(std::format("{} — no device at feature level {} ({})", name,
                                             FeatureLevelName(kMinimumFeatureLevel),
                                             HResultText(createResult)));
            continue;
        }

        // Before any capability is believed: a runtime other than the one the
        // executable opted into answers every query too, with its own answers.
        if (!bAgilitySdkVerified)
        {
            m_AgilitySdk = VerifyLoadedAgilitySdk();
            bAgilitySdkVerified = true;
        }

        if (const std::optional<std::string> refusal = FindRefusal(*device.Get()))
        {
            considered.push_back(std::format("{} — {}", name, *refusal));
            continue;
        }

        m_Adapter = adapter;
        m_Device = device;
        return;
    }

    if (desc.Gpu.empty())
    {
        throw std::runtime_error("No D3D12 adapter meets this backend's requirements:" +
                                 DescribeAdapters(considered));
    }

    throw std::runtime_error(
        std::format("No D3D12 adapter matching \"{}\" meets this backend's requirements:{}",
                    desc.Gpu, DescribeAdapters(considered)));
}

/**
 * The device's identity, for a run report. Read once: nothing about it changes for
 * the life of the device.
 */
void D3D12Device::FillDeviceInfo()
{
    DXGI_ADAPTER_DESC1 adapterDesc{};
    m_Adapter->GetDesc1(&adapterDesc);

    m_Info.Backend = Rhi::Backend::D3D12;
    m_Info.Gpu = Utf8FromWide(adapterDesc.Description);
    m_Info.Driver = DriverVersion(*m_Adapter.Get());
    m_Info.VendorId = adapterDesc.VendorId;
    m_Info.DeviceId = adapterDesc.DeviceId;

    // What the adapter supports rather than the minimum this backend asked for,
    // which is a constant and would say nothing about the machine.
    std::array<D3D_FEATURE_LEVEL, 3> levels = {D3D_FEATURE_LEVEL_12_0, D3D_FEATURE_LEVEL_12_1,
                                               D3D_FEATURE_LEVEL_12_2};
    D3D12_FEATURE_DATA_FEATURE_LEVELS featureLevels{};
    featureLevels.NumFeatureLevels = static_cast<UINT>(levels.size());
    featureLevels.pFeatureLevelsRequested = levels.data();

    const HRESULT hr = m_Device->CheckFeatureSupport(D3D12_FEATURE_FEATURE_LEVELS, &featureLevels,
                                                     sizeof(featureLevels));
    m_Info.ApiVersion = SUCCEEDED(hr)
                            ? std::format("feature level {}",
                                          FeatureLevelName(featureLevels.MaxSupportedFeatureLevel))
                            : std::string("feature level unknown");
}

/**
 * Enhanced barriers are "not currently a hardware or driver requirement", so support is
 * the driver's to report. Asking for them on an adapter without is refused rather than
 * quietly run legacy: a legacy-against-enhanced comparison would otherwise compare
 * legacy with itself and pass.
 */
void D3D12Device::ResolveBarrierPath(const DeviceDesc& desc)
{
    D3D12_FEATURE_DATA_D3D12_OPTIONS12 options12{};
    const bool bSupported = SUCCEEDED(m_Device->CheckFeatureSupport(
                                D3D12_FEATURE_D3D12_OPTIONS12, &options12, sizeof(options12))) &&
                            options12.EnhancedBarriersSupported;

    if (desc.BarrierPath == BarrierPath::Enhanced && !bSupported)
    {
        throw std::runtime_error(std::format(
            "Enhanced barriers were asked for, and '{}' does not support them "
            "(D3D12_FEATURE_DATA_D3D12_OPTIONS12::EnhancedBarriersSupported is false). Use "
            "legacy or auto.",
            m_Info.Gpu));
    }

    const bool bEnhanced = desc.BarrierPath == BarrierPath::Enhanced ||
                           (desc.BarrierPath == BarrierPath::Auto && bSupported);
    m_Info.BarrierPath = bEnhanced ? BarrierPath::Enhanced : BarrierPath::Legacy;
}

void D3D12Device::DrainDebugMessages()
{
    if (m_pDebugMessages)
        m_pDebugMessages->Drain();
}

/**
 * Signals a fresh value on each queue and waits for both: a queue reaches its signal
 * only after everything submitted to it before, so this returns once the device has
 * finished all of it. Drains afterwards, because GPU-based validation reports what it
 * saw only once the GPU has run.
 */
void D3D12Device::WaitIdle()
{
    for (ID3D12CommandQueue* pQueue : {m_DirectQueue.Get(), m_CopyQueue.Get()})
    {
        if (pQueue == nullptr)
            continue;

        const uint64_t value = ++m_IdleValue;
        pQueue->Signal(m_IdleFence.Get(), value);

        if (m_IdleFence->GetCompletedValue() < value)
        {
            const HANDLE completed = CreateEventW(nullptr, FALSE, FALSE, nullptr);
            m_IdleFence->SetEventOnCompletion(value, completed);
            WaitForSingleObject(completed, INFINITE);
            CloseHandle(completed);
        }
    }

    DrainDebugMessages();
}

BufferHandle D3D12Device::CreateBuffer(const BufferDesc& desc)
{
    if (desc.Size == 0u)
        throw std::runtime_error("Rhi::IDevice::CreateBuffer: a buffer must have a non-zero size.");

    // An upload heap's resources must be created in GENERIC_READ and a readback
    // heap's in COPY_DEST, and neither can ever leave that state (D3D12_HEAP_TYPE's
    // reference page). A default heap's start in COMMON, which is what a legacy
    // barrier's before-state assumes of a new buffer.
    D3D12MA::ALLOCATION_DESC allocationDesc{};
    D3D12_RESOURCE_STATES initialState = D3D12_RESOURCE_STATE_COMMON;
    switch (desc.Access)
    {
        case MemoryAccess::GpuOnly:
            allocationDesc.HeapType = D3D12_HEAP_TYPE_DEFAULT;
            break;
        case MemoryAccess::CpuToGpu:
            allocationDesc.HeapType = D3D12_HEAP_TYPE_UPLOAD;
            initialState = D3D12_RESOURCE_STATE_GENERIC_READ;
            break;
        case MemoryAccess::GpuToCpu:
            allocationDesc.HeapType = D3D12_HEAP_TYPE_READBACK;
            initialState = D3D12_RESOURCE_STATE_COPY_DEST;
            break;
    }

    D3D12_RESOURCE_DESC resourceDesc{};
    resourceDesc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
    // A constant buffer view's size must be a multiple of 256 bytes and cannot run past
    // its resource, so a uniform buffer's resource is rounded up to one. The seam's size
    // is what the caller asked for; the padding is never mapped for anyone to see.
    constexpr uint64_t kConstantBufferAlignment = 256u;
    resourceDesc.Width =
        (desc.Usage & BufferUsage::Uniform) != BufferUsage::None
            ? (desc.Size + kConstantBufferAlignment - 1u) & ~(kConstantBufferAlignment - 1u)
            : desc.Size;
    resourceDesc.Height = 1;
    resourceDesc.DepthOrArraySize = 1;
    resourceDesc.MipLevels = 1;
    resourceDesc.Format = DXGI_FORMAT_UNKNOWN;
    resourceDesc.SampleDesc.Count = 1;
    resourceDesc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;

    // Only a GPU-local buffer can be written by a shader. A storage buffer on an
    // upload heap is one a shader only reads, which needs no flag.
    if ((desc.Usage & BufferUsage::Storage) != BufferUsage::None &&
        desc.Access == MemoryAccess::GpuOnly)
    {
        resourceDesc.Flags = D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;
    }

    D3D12Buffer buffer;
    buffer.Desc = desc;

    const HRESULT hr =
        m_Allocator->CreateResource(&allocationDesc, &resourceDesc, initialState, nullptr,
                                    &buffer.Allocation, IID_PPV_ARGS(&buffer.Resource));
    if (FAILED(hr))
    {
        DrainDebugMessages();
        throw std::runtime_error(std::format(
            "Rhi::IDevice::CreateBuffer: D3D12MA failed to allocate '{}' ({} bytes): {}.",
            desc.DebugName, desc.Size, HResultText(hr)));
    }

    if (desc.Access != MemoryAccess::GpuOnly)
    {
        // Nothing is read back through an upload heap's mapping, and saying so lets
        // the runtime skip the read; a readback heap's mapping is read in full.
        const D3D12_RANGE nothingRead{0, 0};
        const HRESULT mapResult = buffer.Resource->Map(
            0, desc.Access == MemoryAccess::CpuToGpu ? &nothingRead : nullptr, &buffer.pMapped);
        if (FAILED(mapResult))
        {
            DrainDebugMessages();
            throw std::runtime_error(
                std::format("Rhi::IDevice::CreateBuffer: mapping '{}' failed: {}.", desc.DebugName,
                            HResultText(mapResult)));
        }
    }

    if (!desc.DebugName.empty())
    {
        SetDebugName(*buffer.Resource.Get(), desc.DebugName);
        buffer.Allocation->SetName(WideFromUtf8(desc.DebugName).c_str());
    }

    const BufferHandle handle = m_Buffers.Create(std::move(buffer));
    DrainDebugMessages();
    return handle;
}

void D3D12Device::Destroy(BufferHandle handle)
{
    if (m_Buffers.Release(handle))
    {
        DrainDebugMessages();
        return;
    }

    // A double destroy or a handle outliving what it named: the bug the generation
    // counter exists to catch, reported rather than ignored, and not fatal since the
    // slot is already free.
    ReportError(std::format("Rhi::IDevice::Destroy(BufferHandle): handle {:#010x} is stale or "
                            "was never valid; it may have been destroyed already.",
                            handle.Value));
}

void* D3D12Device::GetMappedData(BufferHandle handle)
{
    const D3D12Buffer* pBuffer = m_Buffers.Get(handle);
    return pBuffer ? pBuffer->pMapped : nullptr;
}

TextureHandle D3D12Device::CreateTexture(const TextureDesc& desc)
{
    ValidateTextureDesc(desc);

    D3D12_RESOURCE_DESC resourceDesc{};
    resourceDesc.Dimension = desc.Dimension == TextureDimension::Texture3D
                                 ? D3D12_RESOURCE_DIMENSION_TEXTURE3D
                                 : D3D12_RESOURCE_DIMENSION_TEXTURE2D;
    resourceDesc.Width = desc.Extent.Width;
    resourceDesc.Height = desc.Extent.Height;
    // One field for two things: a 3D texture's depth, or a 2D texture's layers.
    resourceDesc.DepthOrArraySize = static_cast<UINT16>(
        desc.Dimension == TextureDimension::Texture3D ? desc.Extent.Depth : desc.ArrayLayers);
    resourceDesc.MipLevels = static_cast<UINT16>(desc.MipLevels);
    resourceDesc.Format = ToDxgiResourceFormat(desc.Format, desc.Usage);
    resourceDesc.SampleDesc.Count = static_cast<UINT>(desc.Samples);
    resourceDesc.Flags = ToResourceFlags(desc.Usage);

    // Textures live only on the default heap: D3D12 refuses a texture on an upload
    // or readback heap. They start in COMMON, the state a legacy barrier's
    // before-state assumes of a texture nothing has used.
    D3D12MA::ALLOCATION_DESC allocationDesc{};
    allocationDesc.HeapType = D3D12_HEAP_TYPE_DEFAULT;

    D3D12Texture texture;
    texture.Desc = desc;

    const HRESULT hr =
        m_Allocator->CreateResource(&allocationDesc, &resourceDesc, D3D12_RESOURCE_STATE_COMMON,
                                    nullptr, &texture.Allocation, IID_PPV_ARGS(&texture.Resource));
    if (FAILED(hr))
    {
        DrainDebugMessages();
        throw std::runtime_error(std::format(
            "Rhi::IDevice::CreateTexture: D3D12MA failed to allocate '{}' ({}x{}x{}): {}.",
            desc.DebugName, desc.Extent.Width, desc.Extent.Height, desc.Extent.Depth,
            HResultText(hr)));
    }

    if (!desc.DebugName.empty())
    {
        SetDebugName(*texture.Resource.Get(), desc.DebugName);
        texture.Allocation->SetName(WideFromUtf8(desc.DebugName).c_str());
    }

    const TextureHandle handle = m_Textures.Create(std::move(texture));
    DrainDebugMessages();
    return handle;
}

void D3D12Device::Destroy(TextureHandle handle)
{
    if (m_Textures.Release(handle))
    {
        DrainDebugMessages();
        return;
    }

    ReportError(std::format("Rhi::IDevice::Destroy(TextureHandle): handle {:#010x} is stale or "
                            "was never valid; it may have been destroyed already.",
                            handle.Value));
}

TextureViewHandle D3D12Device::CreateTextureView(const TextureViewDesc& desc)
{
    const D3D12Texture* pTexture = m_Textures.Get(desc.Texture);
    if (pTexture == nullptr)
    {
        throw std::runtime_error(std::format(
            "Rhi::IDevice::CreateTextureView('{}'): the texture handle is stale or was never "
            "valid.",
            desc.DebugName));
    }

    // Undefined is resolved here, as the Vulkan backend resolves it, because every view
    // description written from this one has to name a format: D3D12 takes UNKNOWN as
    // the resource's own only when no description is passed at all.
    TextureViewDesc resolved = desc;
    if (resolved.Format == Format::Undefined)
        resolved.Format = pTexture->Desc.Format;

    return m_TextureViews.Create(D3D12TextureView{.Desc = std::move(resolved)});
}

void D3D12Device::Destroy(TextureViewHandle handle)
{
    {
        const std::lock_guard lock(m_ViewMutex);
        if (const D3D12TextureView* pView = m_TextureViews.Get(handle))
        {
            if (pView->RenderTargetSlot)
                m_RenderTargetHeap->Free(*pView->RenderTargetSlot);
            if (pView->DepthStencilSlot)
                m_DepthStencilHeap->Free(*pView->DepthStencilSlot);
            if (pView->ReadOnlyDepthStencilSlot)
                m_DepthStencilHeap->Free(*pView->ReadOnlyDepthStencilSlot);
        }
    }

    if (m_TextureViews.Release(handle))
        return;

    ReportError(std::format("Rhi::IDevice::Destroy(TextureViewHandle): handle {:#010x} is stale "
                            "or was never valid; it may have been destroyed already.",
                            handle.Value));
}

SamplerHandle D3D12Device::CreateSampler(const SamplerDesc& desc)
{
    return m_Samplers.Create(D3D12Sampler{.Desc = desc});
}

void D3D12Device::Destroy(SamplerHandle handle)
{
    if (m_Samplers.Release(handle))
        return;

    ReportError(std::format("Rhi::IDevice::Destroy(SamplerHandle): handle {:#010x} is stale or "
                            "was never valid; it may have been destroyed already.",
                            handle.Value));
}

const TextureDesc* D3D12Device::GetTextureDesc(TextureHandle handle) const
{
    const D3D12Texture* pTexture = m_Textures.Get(handle);
    return pTexture ? &pTexture->Desc : nullptr;
}

std::unique_ptr<IUploadContext> D3D12Device::CreateUploadContext(const UploadContextDesc& desc)
{
    return std::make_unique<D3D12UploadContext>(*this, desc);
}

std::unique_ptr<ICommandAllocator>
D3D12Device::CreateCommandAllocator(const CommandAllocatorDesc& desc)
{
    return std::make_unique<D3D12CommandAllocator>(*this, desc, ListTypeFor(desc.Queue));
}

/**
 * Every usage asked for must be supported. A depth format's sampling is asked of its
 * shader view's format, since that is the format a shader reads it through; every
 * other usage is asked of the format itself.
 */
bool D3D12Device::IsFormatSupported(Format format, TextureUsage usage) const
{
    const auto query = [this](DXGI_FORMAT dxgi, D3D12_FEATURE_DATA_FORMAT_SUPPORT& support)
    {
        support.Format = dxgi;
        return SUCCEEDED(
            m_Device->CheckFeatureSupport(D3D12_FEATURE_FORMAT_SUPPORT, &support, sizeof(support)));
    };

    if (format == Format::Undefined)
        return false;

    D3D12_FEATURE_DATA_FORMAT_SUPPORT support{};
    if (!query(ToDxgi(format), support))
        return false;

    const auto has1 = [&support](D3D12_FORMAT_SUPPORT1 flag)
    { return (support.Support1 & flag) == flag; };

    if (!has1(D3D12_FORMAT_SUPPORT1_TEXTURE2D))
        return false;

    const auto wants = [usage](TextureUsage bit) { return (usage & bit) != TextureUsage::None; };

    if (wants(TextureUsage::ColorAttachment) && !has1(D3D12_FORMAT_SUPPORT1_RENDER_TARGET))
        return false;

    if (wants(TextureUsage::DepthStencilAttachment) && !has1(D3D12_FORMAT_SUPPORT1_DEPTH_STENCIL))
        return false;

    if (wants(TextureUsage::Storage) &&
        (support.Support2 & D3D12_FORMAT_SUPPORT2_UAV_TYPED_STORE) == 0)
        return false;

    if (wants(TextureUsage::Sampled))
    {
        D3D12_FEATURE_DATA_FORMAT_SUPPORT viewSupport{};
        if (!query(ToDxgiShaderViewFormat(format), viewSupport) ||
            (viewSupport.Support1 & D3D12_FORMAT_SUPPORT1_SHADER_SAMPLE) == 0)
            return false;
    }

    return true;
}

BindGroupLayoutHandle D3D12Device::CreateBindGroupLayout(const BindGroupLayoutDesc& desc)
{
    D3D12BindGroupLayout layout;

    for (const BindGroupLayoutBinding& binding : desc.Bindings)
    {
        // Registers are unique across classes within a space (plan D29), because
        // Vulkan has one binding namespace per set where HLSL has four.
        for (const D3D12BindGroupLayout::Entry& existing : layout.Entries)
        {
            if (existing.Binding.Slot == binding.Slot)
            {
                throw std::runtime_error(std::format(
                    "Rhi::IDevice::CreateBindGroupLayout('{}'): slot {} is declared twice.",
                    desc.DebugName, binding.Slot));
            }
        }

        const bool bSampler = binding.Type == BindingType::Sampler;
        layout.Entries.push_back(D3D12BindGroupLayout::Entry{
            .Binding = binding, .Offset = bSampler ? layout.SamplerCount : layout.ResourceCount});

        if (bSampler)
        {
            ++layout.SamplerCount;
            layout.SamplerVisibility = layout.SamplerVisibility | binding.Visibility;
        }
        else
        {
            ++layout.ResourceCount;
            layout.ResourceVisibility = layout.ResourceVisibility | binding.Visibility;
        }
    }

    return m_BindGroupLayouts.Create(std::move(layout));
}

void D3D12Device::Destroy(BindGroupLayoutHandle handle)
{
    if (m_BindGroupLayouts.Release(handle))
        return;

    ReportError(std::format("Rhi::IDevice::Destroy(BindGroupLayoutHandle): handle {:#010x} is "
                            "stale or was never valid; it may have been destroyed already.",
                            handle.Value));
}

/**
 * Writes the group's resources into a fresh range of the resource heap, and finds or
 * makes a sampler range holding its samplers. Descriptors are written once here and
 * never again, which is what the seam's immutable bind groups (plan D20) promise and
 * what D3D12 requires of a descriptor a submitted list may reference.
 */
BindGroupHandle D3D12Device::CreateBindGroup(const BindGroupDesc& desc)
{
    const std::lock_guard lock(m_BindMutex);

    const D3D12BindGroupLayout* pLayout = m_BindGroupLayouts.Get(desc.Layout);
    if (pLayout == nullptr)
    {
        throw std::runtime_error(std::format(
            "Rhi::IDevice::CreateBindGroup('{}'): the layout handle is stale or was never valid.",
            desc.DebugName));
    }

    const auto fail = [&desc](const std::string& why)
    {
        throw std::runtime_error(
            std::format("Rhi::IDevice::CreateBindGroup('{}'): {}", desc.DebugName, why));
    };

    D3D12BindGroup group;
    group.Layout = desc.Layout;
    group.ResourceCount = pLayout->ResourceCount;
    if (group.ResourceCount > 0u)
        group.ResourceStart = m_ResourceHeap->Allocate(group.ResourceCount);

    std::vector<D3D12_SAMPLER_DESC> samplers(pLayout->SamplerCount);

    try
    {
        for (const D3D12BindGroupLayout::Entry& entry : pLayout->Entries)
        {
            const BindGroupBinding* pBinding = nullptr;
            for (const BindGroupBinding& candidate : desc.Bindings)
            {
                if (candidate.Slot == entry.Binding.Slot)
                    pBinding = &candidate;
            }

            if (pBinding != nullptr && pBinding->Type != entry.Binding.Type)
                fail(std::format("slot {} is bound as a different type than its layout declares.",
                                 entry.Binding.Slot));

            const D3D12_CPU_DESCRIPTOR_HANDLE target =
                m_ResourceHeap->CpuHandle(group.ResourceStart + entry.Offset);

            switch (entry.Binding.Type)
            {
                case BindingType::UniformBuffer:
                {
                    const D3D12Buffer* pBuffer =
                        pBinding ? m_Buffers.Get(pBinding->Buffer) : nullptr;
                    if (pBuffer == nullptr)
                        fail(std::format("uniform buffer slot {} has no live buffer; resource "
                                         "binding tier 2 allows no unpopulated constant buffer.",
                                         entry.Binding.Slot));

                    D3D12_CONSTANT_BUFFER_VIEW_DESC view{};
                    view.BufferLocation = pBuffer->Resource->GetGPUVirtualAddress();
                    view.SizeInBytes = static_cast<UINT>(pBuffer->Resource->GetDesc().Width);
                    m_Device->CreateConstantBufferView(&view, target);
                    break;
                }
                case BindingType::Texture:
                {
                    const D3D12TextureView* pView =
                        pBinding ? m_TextureViews.Get(pBinding->View) : nullptr;
                    const D3D12Texture* pTexture =
                        pView ? m_Textures.Get(pView->Desc.Texture) : nullptr;
                    if (pTexture == nullptr)
                    {
                        if (!entry.Binding.bOptional)
                            fail(std::format("texture slot {} has no live view.",
                                             entry.Binding.Slot));

                        // An optional texture left empty gets a null descriptor, so the
                        // table is fully populated whatever the binding tier.
                        D3D12_SHADER_RESOURCE_VIEW_DESC null{};
                        null.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
                        null.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
                        null.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
                        null.Texture2D.MipLevels = 1;
                        m_Device->CreateShaderResourceView(nullptr, &null, target);
                        break;
                    }

                    const D3D12_SHADER_RESOURCE_VIEW_DESC view = ToShaderResourceView(pView->Desc);
                    m_Device->CreateShaderResourceView(pTexture->Resource.Get(), &view, target);
                    break;
                }
                case BindingType::UnorderedAccessTexture:
                {
                    const D3D12TextureView* pView =
                        pBinding ? m_TextureViews.Get(pBinding->View) : nullptr;
                    const D3D12Texture* pTexture =
                        pView ? m_Textures.Get(pView->Desc.Texture) : nullptr;
                    if (pTexture == nullptr)
                        fail(std::format("unordered-access slot {} has no live view; resource "
                                         "binding tier 2 allows no unpopulated one.",
                                         entry.Binding.Slot));

                    const D3D12_UNORDERED_ACCESS_VIEW_DESC view =
                        ToUnorderedAccessView(pView->Desc);
                    m_Device->CreateUnorderedAccessView(pTexture->Resource.Get(), nullptr, &view,
                                                        target);
                    break;
                }
                case BindingType::Sampler:
                {
                    const D3D12Sampler* pSampler =
                        pBinding ? m_Samplers.Get(pBinding->Sampler) : nullptr;
                    if (pSampler == nullptr)
                        fail(std::format("sampler slot {} has no live sampler.",
                                         entry.Binding.Slot));

                    samplers[entry.Offset] = ToD3D12Sampler(pSampler->Desc);
                    break;
                }
            }
        }

        if (!samplers.empty())
            group.SamplerRange = AcquireSamplerRange(samplers);
    }
    catch (...)
    {
        m_ResourceHeap->Free(group.ResourceStart, group.ResourceCount);
        DrainDebugMessages();
        throw;
    }

    const BindGroupHandle handle = m_BindGroups.Create(std::move(group));
    DrainDebugMessages();
    return handle;
}

/**
 * A range holding exactly these samplers: an existing one shared with every group that
 * asked for the same, or a new one. Materials all sample through the same sampler, so a
 * scene of hundreds of them needs one range rather than hundreds of the 2,048 sampler
 * descriptors D3D12 guarantees.
 */
size_t D3D12Device::AcquireSamplerRange(const std::vector<D3D12_SAMPLER_DESC>& samplers)
{
    const auto same = [&samplers](const SharedSamplerRange& range)
    {
        return range.Users > 0u && range.Samplers.size() == samplers.size() &&
               std::memcmp(range.Samplers.data(), samplers.data(),
                           samplers.size() * sizeof(D3D12_SAMPLER_DESC)) == 0;
    };

    for (size_t i = 0; i < m_SamplerRanges.size(); ++i)
    {
        if (same(m_SamplerRanges[i]))
        {
            ++m_SamplerRanges[i].Users;
            return i;
        }
    }

    const uint32_t count = static_cast<uint32_t>(samplers.size());
    const uint32_t start = m_SamplerHeap->Allocate(count);
    for (uint32_t i = 0u; i < count; ++i)
        m_Device->CreateSampler(&samplers[i], m_SamplerHeap->CpuHandle(start + i));

    SharedSamplerRange range{.Samplers = samplers, .Start = start, .Users = 1u};

    // Reuse a slot a released range left behind, so indices held by live groups stay put.
    for (size_t i = 0; i < m_SamplerRanges.size(); ++i)
    {
        if (m_SamplerRanges[i].Users == 0u)
        {
            m_SamplerRanges[i] = std::move(range);
            return i;
        }
    }

    m_SamplerRanges.push_back(std::move(range));
    return m_SamplerRanges.size() - 1u;
}

std::optional<D3D12Device::BindGroupTables> D3D12Device::FindBindGroupTables(BindGroupHandle handle)
{
    // Under the mutex, because a shared sampler range may move while another thread
    // creates a group.
    const std::lock_guard lock(m_BindMutex);

    const D3D12BindGroup* pGroup = m_BindGroups.Get(handle);
    if (pGroup == nullptr)
        return std::nullopt;

    BindGroupTables tables;
    if (pGroup->ResourceCount > 0u)
        tables.Resources = m_ResourceHeap->GpuHandle(pGroup->ResourceStart);
    if (pGroup->SamplerRange)
        tables.Samplers = m_SamplerHeap->GpuHandle(m_SamplerRanges[*pGroup->SamplerRange].Start);

    return tables;
}

NativeDescriptor D3D12Device::AllocateResourceDescriptor()
{
    const std::lock_guard lock(m_BindMutex);
    const uint32_t index = m_ResourceHeap->Allocate(1u);
    return NativeDescriptor{.Cpu = m_ResourceHeap->CpuHandle(index),
                            .Gpu = m_ResourceHeap->GpuHandle(index)};
}

void D3D12Device::FreeResourceDescriptor(D3D12_GPU_DESCRIPTOR_HANDLE descriptor)
{
    const std::lock_guard lock(m_BindMutex);
    m_ResourceHeap->Free(m_ResourceHeap->IndexOf(descriptor), 1u);
}

void D3D12Device::Destroy(BindGroupHandle handle)
{
    const std::lock_guard lock(m_BindMutex);

    if (const D3D12BindGroup* pGroup = m_BindGroups.Get(handle))
    {
        m_ResourceHeap->Free(pGroup->ResourceStart, pGroup->ResourceCount);

        if (pGroup->SamplerRange)
        {
            SharedSamplerRange& range = m_SamplerRanges[*pGroup->SamplerRange];
            if (--range.Users == 0u)
            {
                m_SamplerHeap->Free(range.Start, static_cast<uint32_t>(range.Samplers.size()));
                range.Samplers.clear();
            }
        }

        m_BindGroups.Release(handle);
        return;
    }

    ReportError(std::format("Rhi::IDevice::Destroy(BindGroupHandle): handle {:#010x} is stale or "
                            "was never valid; it may have been destroyed already.",
                            handle.Value));
}

FenceHandle D3D12Device::CreateFence(const FenceDesc& desc)
{
    D3D12Fence fence;
    const HRESULT hr =
        m_Device->CreateFence(desc.InitialValue, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&fence.Fence));
    if (FAILED(hr))
    {
        DrainDebugMessages();
        throw std::runtime_error(std::format("Rhi::IDevice::CreateFence('{}') failed ({})",
                                             desc.DebugName, HResultText(hr)));
    }

    SetDebugName(*fence.Fence.Get(), desc.DebugName);

    return m_Fences.Create(std::move(fence));
}

void D3D12Device::Destroy(FenceHandle handle)
{
    if (m_Fences.Release(handle))
        return;

    ReportError(std::format("Rhi::IDevice::Destroy(FenceHandle): handle {:#010x} is stale or was "
                            "never valid; it may have been destroyed already.",
                            handle.Value));
}

void D3D12Device::WaitForFence(FenceHandle handle, uint64_t value)
{
    const D3D12Fence* pFence = m_Fences.Get(handle);
    if (pFence == nullptr)
    {
        ReportError(
            std::format("Rhi::IDevice::WaitForFence: handle {:#010x} is stale or was never valid.",
                        handle.Value));
        return;
    }

    if (pFence->Fence->GetCompletedValue() < value)
    {
        const HANDLE completed = CreateEventW(nullptr, FALSE, FALSE, nullptr);
        if (completed == nullptr)
            throw std::runtime_error("Rhi::IDevice::WaitForFence: CreateEvent failed.");

        pFence->Fence->SetEventOnCompletion(value, completed);
        WaitForSingleObject(completed, INFINITE);
        CloseHandle(completed);
    }

    // The GPU has run what the fence waited on, so whatever GPU-based validation saw
    // there is stored now.
    DrainDebugMessages();
}

/**
 * Everything that can refuse the submission is checked before the queue is touched:
 * the lists' queue, then the present image, whose target records the write.
 *
 * Wait fences become a queue's GPU-side waits, which hold the lists back without
 * blocking the CPU; signal fences are raised after the lists, so they complete once
 * the lists have run. A present image needs nothing more: the queue runs its lists in
 * submission order, so a write is already ordered after the image's last one.
 *
 * Then the states the lists leave their textures in become the textures' submitted
 * states, in submission order — the order the GPU will run them — so a later list's
 * from-Undefined barrier names what these left. A copy queue's textures instead decay
 * to COMMON once it has run them, whatever they were promoted to.
 */
void D3D12Device::Submit(const SubmitDesc& desc)
{
    std::vector<ID3D12CommandList*> lists;
    lists.reserve(desc.CommandLists.size());
    for (ICommandList* pList : desc.CommandLists)
    {
        const auto* pD3D12List = static_cast<const D3D12CommandList*>(pList);
        if (pD3D12List->Queue() != desc.Queue)
        {
            throw std::runtime_error(
                "Rhi::IDevice::Submit: a command list allocated for one queue type was submitted "
                "to another. Its allocator's QueueType must match the submission's.");
        }

        lists.push_back(pD3D12List->Native());
    }

    if (desc.PresentImage.pTarget != nullptr)
    {
        auto* pTarget = dynamic_cast<D3D12PresentTarget*>(desc.PresentImage.pTarget);
        if (pTarget == nullptr || !pTarget->BelongsTo(*this))
        {
            throw std::runtime_error("Rhi::IDevice::Submit: the present image belongs to a target "
                                     "another device created.");
        }

        pTarget->MarkSubmitted(desc.PresentImage.Index);
    }

    ID3D12CommandQueue& queue = QueueFor(desc.Queue);

    for (const FenceOperation& wait : desc.WaitFences)
    {
        if (const D3D12Fence* pFence = m_Fences.Get(wait.Fence))
        {
            queue.Wait(pFence->Fence.Get(), wait.Value);
            continue;
        }

        ReportError(std::format("Rhi::IDevice::Submit: wait fence {:#010x} is stale or was never "
                                "valid.",
                                wait.Fence.Value));
    }

    if (!lists.empty())
        queue.ExecuteCommandLists(static_cast<UINT>(lists.size()), lists.data());

    for (const FenceOperation& signal : desc.SignalFences)
    {
        if (const D3D12Fence* pFence = m_Fences.Get(signal.Fence))
        {
            queue.Signal(pFence->Fence.Get(), signal.Value);
            continue;
        }

        ReportError(std::format("Rhi::IDevice::Submit: signal fence {:#010x} is stale or was never "
                                "valid.",
                                signal.Fence.Value));
    }

    {
        const std::lock_guard lock(m_StateMutex);
        for (ICommandList* pList : desc.CommandLists)
        {
            const auto* pD3D12List = static_cast<const D3D12CommandList*>(pList);

            for (const auto& [texture, state] : pD3D12List->Transitions())
            {
                if (D3D12Texture* pTexture = m_Textures.Get(texture))
                    pTexture->SubmittedState = state;
            }

            for (const TextureHandle texture : pD3D12List->CopiedTextures())
            {
                if (D3D12Texture* pTexture = m_Textures.Get(texture))
                    pTexture->SubmittedState = D3D12_RESOURCE_STATE_COMMON;
            }
        }
    }

    DrainDebugMessages();
}

/**
 * A root signature with a table per bind group per heap — resources and samplers — and
 * the push constants as root constants.
 *
 * Bind group N is register space N and a binding's slot is its register (plan D29), so
 * the tables say exactly what the shaders declare. Push constants are at register b0 in
 * space 7, the space the shaders' PUSH_CONSTANT macro reserves (shaders/registers.slangh):
 * root constants appear to a shader as a constant buffer, and the root signature is what
 * makes that register hold constants rather than a table.
 */
PipelineLayoutHandle D3D12Device::CreatePipelineLayout(const PipelineLayoutDesc& desc)
{
    constexpr UINT kPushConstantRegister = 0u;
    constexpr UINT kPushConstantSpace = 7u;

    const auto toVisibility = [](ShaderStage stages)
    {
        if (stages == ShaderStage::Vertex)
            return D3D12_SHADER_VISIBILITY_VERTEX;
        if (stages == ShaderStage::Pixel)
            return D3D12_SHADER_VISIBILITY_PIXEL;
        return D3D12_SHADER_VISIBILITY_ALL;
    };

    D3D12PipelineLayout layout;
    const size_t groupCount = desc.BindGroupLayouts.size();
    layout.ResourceTableParameters.resize(groupCount);
    layout.SamplerTableParameters.resize(groupCount);

    // Reserved up front: each parameter points into its range vector, so neither may
    // reallocate once a pointer has been taken.
    std::vector<std::vector<D3D12_DESCRIPTOR_RANGE>> ranges(groupCount * 2u);
    std::vector<D3D12_ROOT_PARAMETER> parameters;
    parameters.reserve(groupCount * 2u + desc.PushConstantRanges.size());

    for (size_t group = 0; group < groupCount; ++group)
    {
        const D3D12BindGroupLayout* pGroup = m_BindGroupLayouts.Get(desc.BindGroupLayouts[group]);
        if (pGroup == nullptr)
        {
            throw std::runtime_error(std::format(
                "Rhi::IDevice::CreatePipelineLayout('{}'): bind group layout {} is stale.",
                desc.DebugName, group));
        }

        std::vector<D3D12_DESCRIPTOR_RANGE>& resourceRanges = ranges[group * 2u];
        std::vector<D3D12_DESCRIPTOR_RANGE>& samplerRanges = ranges[group * 2u + 1u];

        for (const D3D12BindGroupLayout::Entry& entry : pGroup->Entries)
        {
            D3D12_DESCRIPTOR_RANGE range{};
            range.NumDescriptors = 1;
            range.BaseShaderRegister = entry.Binding.Slot;
            range.RegisterSpace = static_cast<UINT>(group);
            range.OffsetInDescriptorsFromTableStart = entry.Offset;

            switch (entry.Binding.Type)
            {
                case BindingType::UniformBuffer:
                    range.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_CBV;
                    resourceRanges.push_back(range);
                    break;
                case BindingType::Texture:
                    range.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
                    resourceRanges.push_back(range);
                    break;
                case BindingType::UnorderedAccessTexture:
                    range.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_UAV;
                    resourceRanges.push_back(range);
                    break;
                case BindingType::Sampler:
                    range.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SAMPLER;
                    samplerRanges.push_back(range);
                    break;
            }
        }

        const auto addTable = [&](const std::vector<D3D12_DESCRIPTOR_RANGE>& tableRanges,
                                  ShaderStage visibility) -> std::optional<UINT>
        {
            if (tableRanges.empty())
                return std::nullopt;

            D3D12_ROOT_PARAMETER parameter{};
            parameter.ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
            parameter.DescriptorTable.NumDescriptorRanges = static_cast<UINT>(tableRanges.size());
            parameter.DescriptorTable.pDescriptorRanges = tableRanges.data();
            parameter.ShaderVisibility = toVisibility(visibility);
            parameters.push_back(parameter);
            return static_cast<UINT>(parameters.size() - 1u);
        };

        layout.ResourceTableParameters[group] =
            addTable(resourceRanges, pGroup->ResourceVisibility);
        layout.SamplerTableParameters[group] = addTable(samplerRanges, pGroup->SamplerVisibility);
    }

    if (desc.PushConstantRanges.size() > 1u)
    {
        throw std::runtime_error(std::format(
            "Rhi::IDevice::CreatePipelineLayout('{}'): D3D12 takes one push constant block, at "
            "b0 in space 7, and {} were given.",
            desc.DebugName, desc.PushConstantRanges.size()));
    }

    for (const PushConstantRange& pushRange : desc.PushConstantRanges)
    {
        D3D12_ROOT_PARAMETER parameter{};
        parameter.ParameterType = D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS;
        parameter.Constants.ShaderRegister = kPushConstantRegister;
        parameter.Constants.RegisterSpace = kPushConstantSpace;
        parameter.Constants.Num32BitValues = (pushRange.Offset + pushRange.Size + 3u) / 4u;
        parameter.ShaderVisibility = toVisibility(pushRange.Stages);
        parameters.push_back(parameter);
        layout.PushConstantParameter = static_cast<UINT>(parameters.size() - 1u);
    }

    D3D12_ROOT_SIGNATURE_DESC rootDesc{};
    rootDesc.NumParameters = static_cast<UINT>(parameters.size());
    rootDesc.pParameters = parameters.data();
    rootDesc.Flags = D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT;

    Microsoft::WRL::ComPtr<ID3DBlob> serialized;
    Microsoft::WRL::ComPtr<ID3DBlob> errors;
    HRESULT hr =
        D3D12SerializeRootSignature(&rootDesc, D3D_ROOT_SIGNATURE_VERSION_1, &serialized, &errors);
    if (FAILED(hr))
    {
        const std::string why =
            errors ? std::string(static_cast<const char*>(errors->GetBufferPointer()),
                                 errors->GetBufferSize())
                   : HResultText(hr);
        throw std::runtime_error(std::format(
            "Rhi::IDevice::CreatePipelineLayout('{}'): serializing the root signature failed: {}",
            desc.DebugName, why));
    }

    hr = m_Device->CreateRootSignature(0, serialized->GetBufferPointer(),
                                       serialized->GetBufferSize(),
                                       IID_PPV_ARGS(&layout.RootSignature));
    if (FAILED(hr))
    {
        DrainDebugMessages();
        throw std::runtime_error(std::format(
            "Rhi::IDevice::CreatePipelineLayout('{}'): CreateRootSignature failed ({}).",
            desc.DebugName, HResultText(hr)));
    }

    SetDebugName(*layout.RootSignature.Get(), desc.DebugName);

    const PipelineLayoutHandle handle = m_PipelineLayouts.Create(std::move(layout));
    DrainDebugMessages();
    return handle;
}

void D3D12Device::Destroy(PipelineLayoutHandle handle)
{
    if (m_PipelineLayouts.Release(handle))
        return;

    ReportError(std::format("Rhi::IDevice::Destroy(PipelineLayoutHandle): handle {:#010x} is stale "
                            "or was never valid; it may have been destroyed already.",
                            handle.Value));
}

ShaderModuleHandle D3D12Device::CreateShaderModule(const ShaderModuleDesc& desc)
{
    if (desc.Bytes.empty())
    {
        throw std::runtime_error(std::format(
            "Rhi::IDevice::CreateShaderModule('{}'): no shader bytes.", desc.DebugName));
    }

    // DXIL goes into a pipeline state object as it is, so the module is the bytes.
    return m_ShaderModules.Create(
        D3D12ShaderModule{.Bytes = std::vector<std::byte>(desc.Bytes.begin(), desc.Bytes.end())});
}

void D3D12Device::Destroy(ShaderModuleHandle handle)
{
    if (m_ShaderModules.Release(handle))
        return;

    ReportError(
        std::format("Rhi::IDevice::Destroy(ShaderModuleHandle): handle {:#010x} is stale or "
                    "was never valid; it may have been destroyed already.",
                    handle.Value));
}

/**
 * A pipeline state object built from the description, field for field with what the
 * Vulkan backend builds, so the same description renders the same way:
 *
 * - counter-clockwise triangles are front-facing, as on Vulkan. Both decide winding on
 *   the render target, and Vulkan's negated projection Y and D3D12's viewport put every
 *   vertex in the same place there, so the same flag means the same faces;
 * - depth is clipped rather than clamped, as Vulkan's depthClampEnable left off;
 * - every render target blends independently, as Vulkan's independentBlend feature
 *   (which the device requires) allows.
 *
 * Vertex inputs are matched to the shader by semantic, which VertexAttribute carries
 * for this backend alone.
 */
GraphicsPipelineHandle D3D12Device::CreateGraphicsPipeline(const GraphicsPipelineDesc& desc,
                                                           IPipelineCache&)
{
    const auto fail = [&desc](const std::string& why)
    {
        throw std::runtime_error(
            std::format("Rhi::IDevice::CreateGraphicsPipeline('{}'): {}", desc.DebugName, why));
    };

    const D3D12PipelineLayout* pLayout = m_PipelineLayouts.Get(desc.Layout);
    const D3D12ShaderModule* pVertex = m_ShaderModules.Get(desc.VertexShader.Module);
    const D3D12ShaderModule* pPixel = m_ShaderModules.Get(desc.PixelShader.Module);
    if (pLayout == nullptr || pVertex == nullptr)
        fail("the layout or the vertex shader handle is stale.");

    if (desc.RenderTargetFormats.size() > D3D12_SIMULTANEOUS_RENDER_TARGET_COUNT ||
        desc.RenderTargetBlends.size() != desc.RenderTargetFormats.size())
        fail("each render target needs exactly one blend, and D3D12 allows at most eight.");

    D3D12GraphicsPipeline pipeline;
    pipeline.Layout = desc.Layout;

    std::vector<D3D12_INPUT_ELEMENT_DESC> inputs;
    inputs.reserve(desc.VertexAttributes.size());
    for (const VertexAttribute& attribute : desc.VertexAttributes)
    {
        D3D12_INPUT_CLASSIFICATION classification = D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA;
        UINT stepRate = 0u;
        for (const VertexBufferLayout& buffer : desc.VertexBuffers)
        {
            if (buffer.Slot == attribute.Slot && buffer.Rate == VertexInputRate::Instance)
            {
                classification = D3D12_INPUT_CLASSIFICATION_PER_INSTANCE_DATA;
                stepRate = 1u;
            }
        }

        if (attribute.SemanticName == nullptr || attribute.SemanticName[0] == '\0')
            fail(std::format("vertex attribute {} has no semantic, which D3D12 binds by.",
                             attribute.Location));

        inputs.push_back(D3D12_INPUT_ELEMENT_DESC{.SemanticName = attribute.SemanticName,
                                                  .SemanticIndex = attribute.SemanticIndex,
                                                  .Format = ToDxgi(attribute.AttributeFormat),
                                                  .InputSlot = attribute.Slot,
                                                  .AlignedByteOffset = attribute.Offset,
                                                  .InputSlotClass = classification,
                                                  .InstanceDataStepRate = stepRate});
    }

    for (const VertexBufferLayout& buffer : desc.VertexBuffers)
    {
        if (buffer.Slot >= pipeline.Strides.size())
            fail(std::format("vertex buffer slot {} is past D3D12's slots.", buffer.Slot));
        pipeline.Strides[buffer.Slot] = buffer.Stride;
    }

    D3D12_GRAPHICS_PIPELINE_STATE_DESC state{};
    state.pRootSignature = pLayout->RootSignature.Get();
    state.VS = {pVertex->Bytes.data(), pVertex->Bytes.size()};
    if (pPixel != nullptr)
        state.PS = {pPixel->Bytes.data(), pPixel->Bytes.size()};

    // Every field a disabled feature ignores still gets d3dx12's default rather than
    // zero, which is not a valid blend, stencil operation or comparison.
    state.BlendState.IndependentBlendEnable = TRUE;
    for (D3D12_RENDER_TARGET_BLEND_DESC& target : state.BlendState.RenderTarget)
    {
        target =
            D3D12_RENDER_TARGET_BLEND_DESC{.BlendEnable = FALSE,
                                           .LogicOpEnable = FALSE,
                                           .SrcBlend = D3D12_BLEND_ONE,
                                           .DestBlend = D3D12_BLEND_ZERO,
                                           .BlendOp = D3D12_BLEND_OP_ADD,
                                           .SrcBlendAlpha = D3D12_BLEND_ONE,
                                           .DestBlendAlpha = D3D12_BLEND_ZERO,
                                           .BlendOpAlpha = D3D12_BLEND_OP_ADD,
                                           .LogicOp = D3D12_LOGIC_OP_NOOP,
                                           .RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_ALL};
    }

    for (size_t i = 0; i < desc.RenderTargetBlends.size(); ++i)
    {
        const RenderTargetBlend& blend = desc.RenderTargetBlends[i];
        D3D12_RENDER_TARGET_BLEND_DESC& target = state.BlendState.RenderTarget[i];
        target.BlendEnable = blend.bEnable ? TRUE : FALSE;
        target.SrcBlend = ToBlend(blend.SrcColor, false);
        target.DestBlend = ToBlend(blend.DstColor, false);
        target.BlendOp = ToBlendOp(blend.ColorOp);
        target.SrcBlendAlpha = ToBlend(blend.SrcAlpha, true);
        target.DestBlendAlpha = ToBlend(blend.DstAlpha, true);
        target.BlendOpAlpha = ToBlendOp(blend.AlphaOp);
        target.LogicOp = D3D12_LOGIC_OP_NOOP;
        target.RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_ALL;
        state.RTVFormats[i] = ToDxgi(desc.RenderTargetFormats[i]);
    }

    state.SampleMask = UINT_MAX;
    state.RasterizerState.FillMode = D3D12_FILL_MODE_SOLID;
    state.RasterizerState.CullMode = ToCullMode(desc.Cull);
    state.RasterizerState.FrontCounterClockwise = TRUE;
    state.RasterizerState.DepthClipEnable = TRUE;

    state.DepthStencilState.DepthEnable = desc.Depth.bTest ? TRUE : FALSE;
    state.DepthStencilState.DepthWriteMask =
        desc.Depth.bWrite ? D3D12_DEPTH_WRITE_MASK_ALL : D3D12_DEPTH_WRITE_MASK_ZERO;
    state.DepthStencilState.DepthFunc = ToComparisonFunc(desc.Depth.Compare);
    state.DepthStencilState.StencilEnable = FALSE;
    state.DepthStencilState.StencilReadMask = D3D12_DEFAULT_STENCIL_READ_MASK;
    state.DepthStencilState.StencilWriteMask = D3D12_DEFAULT_STENCIL_WRITE_MASK;
    const D3D12_DEPTH_STENCILOP_DESC keep{.StencilFailOp = D3D12_STENCIL_OP_KEEP,
                                          .StencilDepthFailOp = D3D12_STENCIL_OP_KEEP,
                                          .StencilPassOp = D3D12_STENCIL_OP_KEEP,
                                          .StencilFunc = D3D12_COMPARISON_FUNC_ALWAYS};
    state.DepthStencilState.FrontFace = keep;
    state.DepthStencilState.BackFace = keep;

    state.InputLayout = {inputs.data(), static_cast<UINT>(inputs.size())};
    state.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
    state.NumRenderTargets = static_cast<UINT>(desc.RenderTargetFormats.size());
    state.DSVFormat = ToDxgi(desc.DepthFormat);
    state.SampleDesc.Count = 1;

    const HRESULT hr = m_Device->CreateGraphicsPipelineState(&state, IID_PPV_ARGS(&pipeline.State));
    DrainDebugMessages();
    if (FAILED(hr))
        fail(std::format("CreateGraphicsPipelineState failed ({}); the debug layer's messages say "
                         "why.",
                         HResultText(hr)));

    SetDebugName(*pipeline.State.Get(), desc.DebugName);

    return m_GraphicsPipelines.Create(std::move(pipeline));
}

void D3D12Device::Destroy(GraphicsPipelineHandle handle)
{
    if (m_GraphicsPipelines.Release(handle))
        return;

    ReportError(std::format("Rhi::IDevice::Destroy(GraphicsPipelineHandle): handle {:#010x} is "
                            "stale or was never valid; it may have been destroyed already.",
                            handle.Value));
}

ComputePipelineHandle D3D12Device::CreateComputePipeline(const ComputePipelineDesc& desc,
                                                         IPipelineCache&)
{
    const D3D12PipelineLayout* pLayout = m_PipelineLayouts.Get(desc.Layout);
    const D3D12ShaderModule* pShader = m_ShaderModules.Get(desc.Shader.Module);
    if (pLayout == nullptr || pShader == nullptr)
    {
        throw std::runtime_error(std::format(
            "Rhi::IDevice::CreateComputePipeline('{}'): the layout or shader handle is stale.",
            desc.DebugName));
    }

    D3D12ComputePipeline pipeline;
    pipeline.Layout = desc.Layout;

    D3D12_COMPUTE_PIPELINE_STATE_DESC state{};
    state.pRootSignature = pLayout->RootSignature.Get();
    state.CS = {pShader->Bytes.data(), pShader->Bytes.size()};

    const HRESULT hr = m_Device->CreateComputePipelineState(&state, IID_PPV_ARGS(&pipeline.State));
    DrainDebugMessages();
    if (FAILED(hr))
    {
        throw std::runtime_error(std::format(
            "Rhi::IDevice::CreateComputePipeline('{}'): CreateComputePipelineState failed ({}); "
            "the debug layer's messages say why.",
            desc.DebugName, HResultText(hr)));
    }

    SetDebugName(*pipeline.State.Get(), desc.DebugName);

    return m_ComputePipelines.Create(std::move(pipeline));
}

void D3D12Device::Destroy(ComputePipelineHandle handle)
{
    if (m_ComputePipelines.Release(handle))
        return;

    ReportError(std::format("Rhi::IDevice::Destroy(ComputePipelineHandle): handle {:#010x} is "
                            "stale or was never valid; it may have been destroyed already.",
                            handle.Value));
}

std::unique_ptr<IPipelineCache> D3D12Device::CreatePipelineCache(const PipelineCacheDesc&)
{
    return std::make_unique<D3D12PipelineCache>();
}

/**
 * A windowed device presents through a swapchain on the window it was created for, and
 * a device with no window renders into images of its own, as the Vulkan backend's
 * does; the caller cannot tell which it has.
 */
std::unique_ptr<IPresentTarget> D3D12Device::CreatePresentTarget(const PresentTargetDesc& desc)
{
    if (m_bWindowed)
        return std::make_unique<D3D12SwapchainTarget>(*this, m_pNativeWindow, desc);

    return std::make_unique<D3D12OffscreenTarget>(*this, desc);
}

TextureHandle D3D12Device::RegisterExternalTexture(ComPtr<ID3D12Resource> resource,
                                                   const TextureDesc& desc)
{
    if (resource == nullptr)
        throw std::runtime_error("Rhi::D3D12::RegisterExternalTexture: null resource.");

    ValidateTextureDesc(desc);

    SetDebugName(*resource.Get(), desc.DebugName);

    D3D12Texture texture;
    texture.Resource = std::move(resource);
    texture.Desc = desc;
    return m_Textures.Create(std::move(texture));
}

} // namespace Hikari::Rhi::D3D12
