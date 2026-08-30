#pragma once

#include <cstdint>
#include <vector>

#include "vulkan/vulkan_raii.hpp"

#include <rhi/Handles.h>
#include <rhi/IPresentTarget.h>
#include <rhi/RhiTypes.h>

namespace Hikari::Rhi::Vulkan
{
class VulkanDevice;

// IPresentTarget over a VkSwapchainKHR.
//
// Owns the swapchain, the handles naming its images and views, and both rings of
// binary semaphores the present path needs. That last part is the reason the
// class exists rather than the swapchain staying in the renderer: the images and
// the semaphores that order access to them have the same lifetime, and splitting
// them is what let the two disagree about how many there were.
class SwapchainTarget final : public IPresentTarget
{
public:
    SwapchainTarget(VulkanDevice& device, const PresentTargetDesc& desc);
    ~SwapchainTarget() override;

    SwapchainTarget(const SwapchainTarget&) = delete;
    SwapchainTarget& operator=(const SwapchainTarget&) = delete;

    Format GetFormat() const override { return m_Format; }
    Extent2D GetExtent() const override;
    uint32_t GetImageCount() const override { return static_cast<uint32_t>(m_Images.size()); }

    // Required rather than chosen: vkQueuePresentKHR rejects an image in any
    // other layout (VUID-VkPresentInfoKHR-pImageIndices-01430).
    TextureLayout GetRequiredFinalLayout() const override { return TextureLayout::Present; }

    [[nodiscard]] AcquiredImage Acquire() override;
    SemaphoreHandle GetRenderCompleteSemaphore(uint32_t index) const override;
    bool Present(uint32_t index) override;
    [[nodiscard]] bool Recreate(Extent2D newExtent) override;

private:
    // One swapchain image, and the semaphore signalled when work targeting it is
    // finished. Kept together so that a resize cannot rebuild one without the
    // other — the defect this class was extracted to make impossible.
    struct Image
    {
        TextureHandle Texture;
        TextureViewHandle View;
        SemaphoreHandle RenderComplete;
    };

    void Create(Extent2D extent);
    void Destroy();

    VulkanDevice& m_Device;

    vk::raii::SwapchainKHR m_Swapchain = nullptr;
    vk::SurfaceFormatKHR m_SurfaceFormat{};
    vk::Extent2D m_Extent{};
    Format m_Format = Format::Undefined;

    std::vector<Image> m_Images;

    // Sized by frames in flight rather than by image count: a semaphore passed
    // to vkAcquireNextImageKHR cannot be reused until the acquire that used it
    // has completed, and what bounds that is how many frames the caller keeps in
    // flight, not how many images the driver gave us.
    std::vector<SemaphoreHandle> m_AcquireSemaphores;
    uint32_t m_AcquireIndex = 0u;

    uint32_t m_FramesInFlight = 0u;
};
} // namespace Hikari::Rhi::Vulkan
