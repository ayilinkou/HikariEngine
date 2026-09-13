#include "d3d12/D3D12Device.h"

#include <algorithm>
#include <array>
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
#include "d3d12/D3D12DeviceFactory.h"
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
    CreateQueues();

    FillDeviceInfo();

    // D3D12's clip space already has Y up, as GLM's projection produces it.
    m_Caps.bFlipClipSpaceY = false;
    m_Caps.ShaderExtension = "dxil";

    // Nothing this device creates can present yet.
    m_Caps.bPresentSupported = false;

    // Every D3D12 device offers compute and copy queues beside the direct one, so the
    // device has them unless it was asked to behave as though it had one queue for
    // every role — which is what these describe, not where the RHI submits.
    m_Caps.bHasDedicatedComputeQueue = !m_bSingleQueue;
    m_Caps.bHasDedicatedCopyQueue = !m_bSingleQueue;

    Core::LogMsg(Core::LogSeverity::Info, LogRhi,
                 "D3D12 device: {} (vendor 0x{:04X}, device 0x{:04X}), driver {}, {}", m_Info.Gpu,
                 m_Info.VendorId, m_Info.DeviceId, m_Info.Driver, m_Info.ApiVersion);
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
    debug->SetEnableGPUBasedValidation(desc.bGpuBasedValidation ? TRUE : FALSE);

    Core::LogMsg(Core::LogSeverity::Info, LogRhi,
                 "D3D12 debug layer enabled, GPU-based validation {}",
                 desc.bGpuBasedValidation ? "on" : "off");
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
    const auto create = [this](D3D12_COMMAND_LIST_TYPE type, const wchar_t* name)
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

        queue->SetName(name);
        return queue;
    };

    m_DirectQueue = create(D3D12_COMMAND_LIST_TYPE_DIRECT, L"Direct Queue");
    if (!m_bSingleQueue)
        m_CopyQueue = create(D3D12_COMMAND_LIST_TYPE_COPY, L"Copy Queue");

    const HRESULT hr = m_Device->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&m_IdleFence));
    if (FAILED(hr))
        throw std::runtime_error(std::format("CreateFence failed ({})", HResultText(hr)));

    // Sized for every rendering target alive at once — a few per frame in flight — with
    // room to spare. Exhaustion throws naming the heap rather than growing.
    m_RenderTargetHeap = std::make_unique<D3D12CpuDescriptorHeap>(
        *m_Device.Get(), D3D12_DESCRIPTOR_HEAP_TYPE_RTV, 256u, L"Render Target Views");
    m_DepthStencilHeap = std::make_unique<D3D12CpuDescriptorHeap>(
        *m_Device.Get(), D3D12_DESCRIPTOR_HEAP_TYPE_DSV, 64u, L"Depth Stencil Views");

    DrainDebugMessages();
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

void D3D12Device::DrainDebugMessages()
{
    if (m_pDebugMessages)
        m_pDebugMessages->Drain();
}

void D3D12Device::ThrowNotImplemented(std::string_view method)
{
    throw std::logic_error(std::format("The D3D12 backend does not implement {} yet", method));
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
    resourceDesc.Width = desc.Size;
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
        const std::wstring name = WideFromUtf8(desc.DebugName);
        buffer.Resource->SetName(name.c_str());
        buffer.Allocation->SetName(name.c_str());
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
        const std::wstring name = WideFromUtf8(desc.DebugName);
        texture.Resource->SetName(name.c_str());
        texture.Allocation->SetName(name.c_str());
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
    if (!m_Textures.IsValid(desc.Texture))
    {
        throw std::runtime_error(std::format(
            "Rhi::IDevice::CreateTextureView('{}'): the texture handle is stale or was never "
            "valid.",
            desc.DebugName));
    }

    return m_TextureViews.Create(D3D12TextureView{.Desc = desc});
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

BindGroupLayoutHandle D3D12Device::CreateBindGroupLayout(const BindGroupLayoutDesc&)
{
    ThrowNotImplemented("CreateBindGroupLayout");
}

void D3D12Device::Destroy(BindGroupLayoutHandle) {}

BindGroupHandle D3D12Device::CreateBindGroup(const BindGroupDesc&)
{
    ThrowNotImplemented("CreateBindGroup");
}

void D3D12Device::Destroy(BindGroupHandle) {}

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

    if (!desc.DebugName.empty())
        fence.Fence->SetName(WideFromUtf8(desc.DebugName).c_str());

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
 * Wait fences become a queue's GPU-side waits, which hold the lists back without
 * blocking the CPU; signal fences are raised after the lists, so they complete once
 * the lists have run.
 *
 * Then the states the lists leave their textures in become the textures' submitted
 * states, in submission order — the order the GPU will run them — so a later list's
 * from-Undefined barrier names what these left. A copy queue's textures instead decay
 * to COMMON once it has run them, whatever they were promoted to.
 */
void D3D12Device::Submit(const SubmitDesc& desc)
{
    if (!desc.WaitSemaphores.empty() || !desc.SignalSemaphores.empty())
    {
        throw std::runtime_error(
            "Rhi::IDevice::Submit: semaphores are Vulkan's; D3D12 orders work with fences.");
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

PipelineLayoutHandle D3D12Device::CreatePipelineLayout(const PipelineLayoutDesc&)
{
    ThrowNotImplemented("CreatePipelineLayout");
}

void D3D12Device::Destroy(PipelineLayoutHandle) {}

ShaderModuleHandle D3D12Device::CreateShaderModule(const ShaderModuleDesc&)
{
    ThrowNotImplemented("CreateShaderModule");
}

void D3D12Device::Destroy(ShaderModuleHandle) {}

GraphicsPipelineHandle D3D12Device::CreateGraphicsPipeline(const GraphicsPipelineDesc&,
                                                           IPipelineCache&)
{
    ThrowNotImplemented("CreateGraphicsPipeline");
}

void D3D12Device::Destroy(GraphicsPipelineHandle) {}

ComputePipelineHandle D3D12Device::CreateComputePipeline(const ComputePipelineDesc&,
                                                         IPipelineCache&)
{
    ThrowNotImplemented("CreateComputePipeline");
}

void D3D12Device::Destroy(ComputePipelineHandle) {}

std::unique_ptr<IPipelineCache> D3D12Device::CreatePipelineCache(const PipelineCacheDesc&)
{
    ThrowNotImplemented("CreatePipelineCache");
}

std::unique_ptr<IPresentTarget> D3D12Device::CreatePresentTarget(const PresentTargetDesc&)
{
    ThrowNotImplemented("CreatePresentTarget");
}

} // namespace Hikari::Rhi::D3D12
