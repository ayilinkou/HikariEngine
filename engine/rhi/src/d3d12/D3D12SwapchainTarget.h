#pragma once

#include <cstdint>
#include <optional>
#include <vector>

#include <directx/d3d12.h>
#include <dxgi1_6.h>
#include <wrl/client.h>

#include <core/Extent2D.h>
#include <rhi/Handles.h>
#include <rhi/IPresentTarget.h>
#include <rhi/RhiTypes.h>

#include "d3d12/D3D12PresentTarget.h"

namespace Hikari::Rhi::D3D12
{
class D3D12Device;

/**
 * IPresentTarget over a flip-model DXGI swapchain on the device's direct queue: the
 * D3D12 counterpart of the Vulkan swapchain target.
 *
 * D3D12 needs nothing to order a write against its present: "Present operations occur
 * on the 3D queue provided at swapchain creation", so a present is queued behind the
 * rendering already on that queue. What the target keeps is the seam's contract — one
 * submission per acquired image, before its Present — and the back buffers as textures
 * the rest of the RHI can name.
 *
 * The present modes are the Vulkan ones, by behaviour: Mailbox is sync interval 0,
 * where DXGI's flip model discards a queued frame for a newer one and nothing tears;
 * Immediate is sync interval 0 with tearing allowed; Fifo is sync interval 1.
 * FifoRelaxed has no DXGI counterpart and is never offered. The preference is the
 * Vulkan target's, Mailbox first, and the flip model always offers Mailbox, so that is
 * the mode a D3D12 swapchain runs in — named as a Vulkan one would name the same
 * behaviour, which is what lets a report's presentMode mean one thing on both.
 */
class D3D12SwapchainTarget final : public D3D12PresentTarget
{
public:
    /** `pWindow` is the platform's SDL window, which is asked for its HWND. */
    D3D12SwapchainTarget(D3D12Device& device, void* pWindow, const PresentTargetDesc& desc);
    ~D3D12SwapchainTarget() override;

    D3D12SwapchainTarget(const D3D12SwapchainTarget&) = delete;
    D3D12SwapchainTarget& operator=(const D3D12SwapchainTarget&) = delete;

    Format GetFormat() const override { return m_Format; }
    Core::Extent2D GetExtent() const override { return m_Extent; }
    uint32_t GetImageCount() const override { return static_cast<uint32_t>(m_Images.size()); }
    std::optional<PresentMode> GetPresentMode() const override { return m_PresentMode; }

    /**
     * Required rather than chosen: a back buffer must be in D3D12_RESOURCE_STATE_PRESENT
     * — D3D12_BARRIER_LAYOUT_PRESENT on the enhanced path — when it is presented.
     */
    TextureLayout GetRequiredFinalLayout() const override { return TextureLayout::Present; }

    [[nodiscard]] AcquiredImage Acquire() override;
    bool Present(uint32_t index) override;
    [[nodiscard]] bool Recreate(Core::Extent2D newExtent) override;

    bool BelongsTo(const D3D12Device& device) const override { return &m_Device == &device; }
    void MarkSubmitted(uint32_t index) override;

private:
    struct Image
    {
        TextureHandle Texture;
        TextureViewHandle View;
        PresentImageState State = PresentImageState::Idle;
    };

    void CreateImages();
    void DestroyImages();

    D3D12Device& m_Device;

    Microsoft::WRL::ComPtr<IDXGISwapChain3> m_SwapChain;
    Core::Extent2D m_Extent{};
    Format m_Format = Format::BGRA8Unorm;
    PresentMode m_PresentMode = PresentMode::Mailbox;

    /** The swapchain's creation flags, which ResizeBuffers must be given again. */
    UINT m_SwapChainFlags = 0u;

    std::vector<Image> m_Images;
};
} // namespace Hikari::Rhi::D3D12
