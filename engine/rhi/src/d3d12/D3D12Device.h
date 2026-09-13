#pragma once

#include <memory>
#include <mutex>
#include <string>
#include <string_view>
#include <vector>

#include <D3D12MemAlloc.h>
#include <directx/d3d12.h>
#include <dxgi1_6.h>
#include <wrl/client.h>

#include <core/HandlePool.h>

#include <rhi/DeviceDesc.h>
#include <rhi/Diagnostics.h>
#include <rhi/Handles.h>
#include <rhi/IDevice.h>
#include <rhi/Rendering.h>

#include "d3d12/AgilitySdk.h"
#include "d3d12/D3D12BindGroup.h"
#include "d3d12/D3D12Buffer.h"
#include "d3d12/D3D12CpuDescriptorHeap.h"
#include "d3d12/D3D12DebugMessages.h"
#include "d3d12/D3D12Fence.h"
#include "d3d12/D3D12GpuDescriptorHeap.h"
#include "d3d12/D3D12Texture.h"

namespace Hikari::Rhi::D3D12
{
/**
 * The D3D12 device: the adapter it runs on, the runtime it runs on, the debug layer
 * that validates it, and the resources, queues and fences it owns.
 *
 * What it does not implement yet throws, naming the method, so a run that reaches
 * one fails at that call rather than rendering nothing. Destroy methods and live
 * counts never throw: a teardown after a failed start still has to get through them.
 */
class D3D12Device final : public IDevice
{
public:
    explicit D3D12Device(const DeviceDesc& desc);
    ~D3D12Device() override;

    const DeviceCaps& GetCaps() const override { return m_Caps; }
    const DeviceInfo& GetInfo() const override { return m_Info; }
    Diagnostics& GetDiagnostics() override { return *m_pDiagnostics; }
    void WaitIdle() override;

    BufferHandle CreateBuffer(const BufferDesc& desc) override;
    void Destroy(BufferHandle handle) override;
    void* GetMappedData(BufferHandle handle) override;
    uint32_t GetLiveBufferCount() const override { return m_Buffers.Size(); }

    TextureHandle CreateTexture(const TextureDesc& desc) override;
    void Destroy(TextureHandle handle) override;
    TextureViewHandle CreateTextureView(const TextureViewDesc& desc) override;
    void Destroy(TextureViewHandle handle) override;
    SamplerHandle CreateSampler(const SamplerDesc& desc) override;
    void Destroy(SamplerHandle handle) override;
    const TextureDesc* GetTextureDesc(TextureHandle handle) const override;
    uint32_t GetLiveTextureCount() const override { return m_Textures.Size(); }
    uint32_t GetLiveTextureViewCount() const override { return m_TextureViews.Size(); }
    uint32_t GetLiveSamplerCount() const override { return m_Samplers.Size(); }

    std::unique_ptr<IUploadContext> CreateUploadContext(const UploadContextDesc& desc) override;
    std::unique_ptr<ICommandAllocator>
    CreateCommandAllocator(const CommandAllocatorDesc& desc) override;
    bool IsFormatSupported(Format format, TextureUsage usage) const override;

    BindGroupLayoutHandle CreateBindGroupLayout(const BindGroupLayoutDesc& desc) override;
    void Destroy(BindGroupLayoutHandle handle) override;
    BindGroupHandle CreateBindGroup(const BindGroupDesc& desc) override;
    void Destroy(BindGroupHandle handle) override;
    uint32_t GetLiveBindGroupLayoutCount() const override { return m_BindGroupLayouts.Size(); }
    uint32_t GetLiveBindGroupCount() const override { return m_BindGroups.Size(); }

    FenceHandle CreateFence(const FenceDesc& desc) override;
    void Destroy(FenceHandle handle) override;
    uint32_t GetLiveFenceCount() const override { return m_Fences.Size(); }
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

    /** Reports a misuse of the API through Diagnostics, where the backend's own messages go. */
    void ReportError(const std::string& message);

    /** The texture behind `handle`, or null when it is stale. */
    const D3D12Texture* FindTexture(TextureHandle handle) const { return m_Textures.Get(handle); }

    /** The buffer's resource, or null when `handle` is stale. */
    ID3D12Resource* FindBufferResource(BufferHandle handle) const;

    /** The whole-texture state earlier submissions left `handle` in; COMMON when stale. */
    D3D12_RESOURCE_STATES SubmittedStateOf(TextureHandle handle) const;

    /**
     * The native list type a queue role records into. Compute resolves to the direct
     * queue, as Vulkan resolves it to the graphics one; copy is the copy queue unless
     * the device was asked to behave as a single queue.
     */
    D3D12_COMMAND_LIST_TYPE ListTypeFor(QueueType queue) const;

    /**
     * The view's render-target descriptor, written on first use. Throws for a stale
     * view, since there is nothing a list could bind in its place.
     */
    D3D12_CPU_DESCRIPTOR_HANDLE RenderTargetViewFor(TextureViewHandle view);

    /** The view's depth-stencil descriptor, read-only or writable, written on first use. */
    D3D12_CPU_DESCRIPTOR_HANDLE DepthStencilViewFor(TextureViewHandle view, bool bReadOnly);

    /** The format of the texture a view was made from, or Undefined when either is stale. */
    Format TextureFormatOf(TextureViewHandle view) const;

    /** Whether `area` covers the whole of the view's mip level. False when either is stale. */
    bool RenderAreaCoversView(TextureViewHandle view, const Rect2D& area) const;

    /** Binds the device's two shader-visible heaps, which every list binds and never changes. */
    void BindDescriptorHeaps(ID3D12GraphicsCommandList& list) const;

    /** The pipeline layout behind `handle`, or null when it is stale. */
    const D3D12PipelineLayout* FindPipelineLayout(PipelineLayoutHandle handle) const
    {
        return m_PipelineLayouts.Get(handle);
    }

private:
    void EnableDebugLayer(const DeviceDesc& desc);
    void CreateFactory();
    void SelectAdapter(const DeviceDesc& desc);
    void CreateAllocator();
    void CreateQueues();
    void CreateDescriptorHeaps(const DeviceDesc& desc);
    void FillDeviceInfo();

    ID3D12CommandQueue& QueueFor(QueueType queue) const;

    /** A shared sampler range holding `samplers`, found or made. Call under m_BindMutex. */
    size_t AcquireSamplerRange(const std::vector<D3D12_SAMPLER_DESC>& samplers);

    [[noreturn]] static void ThrowNotImplemented(std::string_view method);

    /** Declared before m_pDiagnostics, which may point at it. */
    std::unique_ptr<Diagnostics> m_OwnedDiagnostics;
    Diagnostics* m_pDiagnostics = nullptr;

    Microsoft::WRL::ComPtr<IDXGIFactory4> m_Factory;
    Microsoft::WRL::ComPtr<IDXGIAdapter1> m_Adapter;
    Microsoft::WRL::ComPtr<ID3D12Device> m_Device;

    /** Null when validation is off. Declared after m_Device, which it queries. */
    std::unique_ptr<D3D12DebugMessages> m_pDebugMessages;

    /** One of each queue this backend submits to. The copy queue is null when single-queue. */
    Microsoft::WRL::ComPtr<ID3D12CommandQueue> m_DirectQueue;
    Microsoft::WRL::ComPtr<ID3D12CommandQueue> m_CopyQueue;
    bool m_bSingleQueue = false;

    /** What WaitIdle signals and waits on, one value per call. */
    Microsoft::WRL::ComPtr<ID3D12Fence> m_IdleFence;
    uint64_t m_IdleValue = 0u;

    Core::HandlePool<D3D12Fence, FenceTag> m_Fences;

    /** Guards every texture's SubmittedState, read by recorders and written by Submit. */
    mutable std::mutex m_StateMutex;

    /**
     * Render-target and depth-stencil descriptors, and the mutex that serializes
     * writing them: recorders on job-system threads begin rendering concurrently.
     */
    std::unique_ptr<D3D12CpuDescriptorHeap> m_RenderTargetHeap;
    std::unique_ptr<D3D12CpuDescriptorHeap> m_DepthStencilHeap;
    std::mutex m_ViewMutex;

    /** A sampler range and the groups sharing it, which all asked for these samplers. */
    struct SharedSamplerRange
    {
        std::vector<D3D12_SAMPLER_DESC> Samplers;
        uint32_t Start = 0u;
        uint32_t Users = 0u;
    };

    /**
     * The shader-visible heaps, their shared sampler ranges, and the mutex serializing
     * both: materials are created while assets load.
     */
    std::unique_ptr<D3D12GpuDescriptorHeap> m_ResourceHeap;
    std::unique_ptr<D3D12GpuDescriptorHeap> m_SamplerHeap;
    std::vector<SharedSamplerRange> m_SamplerRanges;
    std::mutex m_BindMutex;

    Core::HandlePool<D3D12BindGroupLayout, BindGroupLayoutTag> m_BindGroupLayouts;
    Core::HandlePool<D3D12BindGroup, BindGroupTag> m_BindGroups;
    Core::HandlePool<D3D12PipelineLayout, PipelineLayoutTag> m_PipelineLayouts;

    /** Declared before every pool, so that the allocations go before their allocator. */
    Microsoft::WRL::ComPtr<D3D12MA::Allocator> m_Allocator;
    Core::HandlePool<D3D12Buffer, BufferTag> m_Buffers;
    Core::HandlePool<D3D12Texture, TextureTag> m_Textures;
    Core::HandlePool<D3D12TextureView, TextureViewTag> m_TextureViews;
    Core::HandlePool<D3D12Sampler, SamplerTag> m_Samplers;

    AgilitySdkInfo m_AgilitySdk;
    DeviceCaps m_Caps{};
    DeviceInfo m_Info{};
};
} // namespace Hikari::Rhi::D3D12
