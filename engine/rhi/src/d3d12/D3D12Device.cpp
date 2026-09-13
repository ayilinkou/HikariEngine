#include "d3d12/D3D12Device.h"

#include <array>
#include <format>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

#include <core/Log.h>

#include "AdapterName.h"
#include "d3d12/D3D12DeviceFactory.h"

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

    FillDeviceInfo();

    // D3D12's clip space already has Y up, as GLM's projection produces it.
    m_Caps.bFlipClipSpaceY = false;
    m_Caps.ShaderExtension = "dxil";

    // Nothing this device creates can present, reach a queue or be recorded yet, so
    // it claims none of the three rather than describing a device that could.
    m_Caps.bPresentSupported = false;
    m_Caps.bHasDedicatedComputeQueue = false;
    m_Caps.bHasDedicatedCopyQueue = false;

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

// Nothing has been submitted, so there is nothing to wait for.
void D3D12Device::WaitIdle() {}

BufferHandle D3D12Device::CreateBuffer(const BufferDesc&)
{
    ThrowNotImplemented("CreateBuffer");
}

void D3D12Device::Destroy(BufferHandle) {}

void* D3D12Device::GetMappedData(BufferHandle)
{
    return nullptr;
}

TextureHandle D3D12Device::CreateTexture(const TextureDesc&)
{
    ThrowNotImplemented("CreateTexture");
}

void D3D12Device::Destroy(TextureHandle) {}

TextureViewHandle D3D12Device::CreateTextureView(const TextureViewDesc&)
{
    ThrowNotImplemented("CreateTextureView");
}

void D3D12Device::Destroy(TextureViewHandle) {}

SamplerHandle D3D12Device::CreateSampler(const SamplerDesc&)
{
    ThrowNotImplemented("CreateSampler");
}

void D3D12Device::Destroy(SamplerHandle) {}

const TextureDesc* D3D12Device::GetTextureDesc(TextureHandle) const
{
    return nullptr;
}

std::unique_ptr<IUploadContext> D3D12Device::CreateUploadContext(const UploadContextDesc&)
{
    ThrowNotImplemented("CreateUploadContext");
}

std::unique_ptr<ICommandAllocator> D3D12Device::CreateCommandAllocator(const CommandAllocatorDesc&)
{
    ThrowNotImplemented("CreateCommandAllocator");
}

bool D3D12Device::IsFormatSupported(Format, TextureUsage) const
{
    ThrowNotImplemented("IsFormatSupported");
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

FenceHandle D3D12Device::CreateFence(const FenceDesc&)
{
    ThrowNotImplemented("CreateFence");
}

void D3D12Device::Destroy(FenceHandle) {}

void D3D12Device::WaitForFence(FenceHandle, uint64_t)
{
    ThrowNotImplemented("WaitForFence");
}

void D3D12Device::Submit(const SubmitDesc&)
{
    ThrowNotImplemented("Submit");
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
