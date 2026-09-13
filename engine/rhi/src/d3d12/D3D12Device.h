#pragma once

#include <memory>
#include <string_view>

#include <directx/d3d12.h>
#include <dxgi1_6.h>
#include <wrl/client.h>

#include <rhi/DeviceDesc.h>
#include <rhi/Diagnostics.h>
#include <rhi/IDevice.h>

#include "d3d12/AgilitySdk.h"
#include "d3d12/D3D12DebugMessages.h"

namespace Hikari::Rhi::D3D12
{
/**
 * The D3D12 device: the adapter it runs on, the runtime it runs on, and the debug
 * layer that validates it.
 *
 * Today it can create itself and answer who it is, and every method that would
 * create or record something throws instead, naming itself — so a run that
 * reaches one fails at that call rather than rendering nothing. Destroy methods
 * and live counts are the exception: nothing can exist to destroy or count, and
 * a teardown after a failed start still has to get through them.
 */
class D3D12Device final : public IDevice
{
public:
    explicit D3D12Device(const DeviceDesc& desc);
    ~D3D12Device() override = default;

    const DeviceCaps& GetCaps() const override { return m_Caps; }
    const DeviceInfo& GetInfo() const override { return m_Info; }
    Diagnostics& GetDiagnostics() override { return *m_pDiagnostics; }
    void WaitIdle() override;

    BufferHandle CreateBuffer(const BufferDesc& desc) override;
    void Destroy(BufferHandle handle) override;
    void* GetMappedData(BufferHandle handle) override;
    uint32_t GetLiveBufferCount() const override { return 0; }

    TextureHandle CreateTexture(const TextureDesc& desc) override;
    void Destroy(TextureHandle handle) override;
    TextureViewHandle CreateTextureView(const TextureViewDesc& desc) override;
    void Destroy(TextureViewHandle handle) override;
    SamplerHandle CreateSampler(const SamplerDesc& desc) override;
    void Destroy(SamplerHandle handle) override;
    const TextureDesc* GetTextureDesc(TextureHandle handle) const override;
    uint32_t GetLiveTextureCount() const override { return 0; }
    uint32_t GetLiveTextureViewCount() const override { return 0; }
    uint32_t GetLiveSamplerCount() const override { return 0; }

    std::unique_ptr<IUploadContext> CreateUploadContext(const UploadContextDesc& desc) override;
    std::unique_ptr<ICommandAllocator>
    CreateCommandAllocator(const CommandAllocatorDesc& desc) override;
    bool IsFormatSupported(Format format, TextureUsage usage) const override;

    BindGroupLayoutHandle CreateBindGroupLayout(const BindGroupLayoutDesc& desc) override;
    void Destroy(BindGroupLayoutHandle handle) override;
    BindGroupHandle CreateBindGroup(const BindGroupDesc& desc) override;
    void Destroy(BindGroupHandle handle) override;
    uint32_t GetLiveBindGroupLayoutCount() const override { return 0; }
    uint32_t GetLiveBindGroupCount() const override { return 0; }

    FenceHandle CreateFence(const FenceDesc& desc) override;
    void Destroy(FenceHandle handle) override;
    uint32_t GetLiveFenceCount() const override { return 0; }
    void WaitForFence(FenceHandle handle, uint64_t value) override;
    void Submit(const SubmitDesc& desc) override;

    PipelineLayoutHandle CreatePipelineLayout(const PipelineLayoutDesc& desc) override;
    void Destroy(PipelineLayoutHandle handle) override;
    ShaderModuleHandle CreateShaderModule(const ShaderModuleDesc& desc) override;
    void Destroy(ShaderModuleHandle handle) override;
    GraphicsPipelineHandle CreateGraphicsPipeline(const GraphicsPipelineDesc& desc,
                                                  IPipelineCache& cache) override;
    void Destroy(GraphicsPipelineHandle handle) override;
    ComputePipelineHandle CreateComputePipeline(const ComputePipelineDesc& desc,
                                                IPipelineCache& cache) override;
    void Destroy(ComputePipelineHandle handle) override;
    std::unique_ptr<IPipelineCache> CreatePipelineCache(const PipelineCacheDesc& desc) override;

    std::unique_ptr<IPresentTarget> CreatePresentTarget(const PresentTargetDesc& desc) override;

    /** The device itself, for the backend's own objects and for tests of it. */
    ID3D12Device& GetNativeDevice() { return *m_Device.Get(); }

    /**
     * Reports whatever the debug layer stored since the last call; nothing when it
     * is off. Every backend method that can produce a message ends with this.
     */
    void DrainDebugMessages();

private:
    void EnableDebugLayer(const DeviceDesc& desc);
    void CreateFactory();
    void SelectAdapter(const DeviceDesc& desc);
    void FillDeviceInfo();

    [[noreturn]] static void ThrowNotImplemented(std::string_view method);

    /** Declared before m_pDiagnostics, which may point at it. */
    std::unique_ptr<Diagnostics> m_OwnedDiagnostics;
    Diagnostics* m_pDiagnostics = nullptr;

    Microsoft::WRL::ComPtr<IDXGIFactory4> m_Factory;
    Microsoft::WRL::ComPtr<IDXGIAdapter1> m_Adapter;
    Microsoft::WRL::ComPtr<ID3D12Device> m_Device;

    /** Null when validation is off. Declared after m_Device, which it queries. */
    std::unique_ptr<D3D12DebugMessages> m_pDebugMessages;

    AgilitySdkInfo m_AgilitySdk;
    DeviceCaps m_Caps{};
    DeviceInfo m_Info{};
};
} // namespace Hikari::Rhi::D3D12
