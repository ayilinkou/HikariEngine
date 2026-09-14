#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <vector>

#include <core/Extent2D.h>
#include <rhi/Barrier.h>
#include <rhi/Handles.h>
#include <rhi/IPresentTarget.h>
#include <rhi/RhiTypes.h>

#include "vulkan/VulkanPresentTarget.h"
#include "vulkan/VulkanSemaphore.h"

namespace Hikari::Rhi::Vulkan
{
class VulkanDevice;

/**
 * IPresentTarget over N images this target owns, with no surface, no swapchain
 * and no presentation engine.
 *
 * The other half of what makes a headless run possible. Everything a swapchain
 * gets from the window system is decided here instead: the format, the extent
 * and how many images there are. Acquire never fails and never asks to be
 * recreated — the two states a swapchain reaches only because a surface can
 * change underneath it.
 *
 * Deliberately not "a swapchain minus presentation": what it drops is the
 * presentation engine, and what it therefore has to add is somewhere for the
 * render-complete signal of each write to go. See Acquire() for that.
 */
class OffscreenTarget final : public VulkanPresentTarget
{
public:
    /**
     * Throws if the device can back none of the formats an offscreen image can
     * be created in, on the same terms as any other unrecoverable init failure.
     */
    OffscreenTarget(VulkanDevice& device, const PresentTargetDesc& desc);
    ~OffscreenTarget() override;

    OffscreenTarget(const OffscreenTarget&) = delete;
    OffscreenTarget& operator=(const OffscreenTarget&) = delete;

    Format GetFormat() const override { return m_Format; }
    Core::Extent2D GetExtent() const override { return m_Extent; }
    uint32_t GetImageCount() const override { return static_cast<uint32_t>(m_Images.size()); }

    /** Nothing: an offscreen target writes into images and never presents. */
    std::optional<PresentMode> GetPresentMode() const override { return std::nullopt; }

    /**
     * None: this target imposes no layout on the image it hands back, so the
     * frame that drew it records no closing barrier. See the declaration on
     * IPresentTarget for why Undefined is the right way to say that, and why no
     * real layout would be an improvement.
     */
    TextureLayout GetRequiredFinalLayout() const override { return TextureLayout::Undefined; }

    [[nodiscard]] AcquiredImage Acquire() override;
    bool Present(uint32_t index) override;
    [[nodiscard]] bool Recreate(Core::Extent2D newExtent) override;

    bool BelongsTo(const VulkanDevice& device) const override { return &m_Device == &device; }
    PresentSemaphores TakeSubmitSemaphores(uint32_t index) override;

private:
    struct Image
    {
        TextureHandle Texture;
        TextureViewHandle View;
        SemaphoreHandle RenderComplete;

        /**
         * Whether RenderComplete has been signalled by a submit nothing has
         * waited on yet. Set by Present(), once the image's submission was made,
         * and cleared by the Acquire() that turns it into the next write's wait.
         *
         * A binary semaphore has to be unsignalled when a signal operation
         * reaches the device (VUID-vkQueueSubmit-pSignalSemaphores-00067), so
         * without this the second frame to reach a given image would signal an
         * already-signalled semaphore. There is no presentation engine here to
         * consume the signal, so the target consumes it itself.
         */
        bool bSignalPending = false;

        /** What this image's next write waits on: the previous write, or nothing. */
        SemaphoreHandle PendingWait{};

        PresentImageState State = PresentImageState::Idle;
    };

    void Create(Core::Extent2D extent);
    void Destroy();

    /**
     * VulkanDevice rather than IDevice, and closer to the latter than it looks:
     * creating and destroying the textures and views, and WaitIdle, are all
     * neutral already. Two things are not, and neither is an oversight.
     *
     *   * Semaphores. IDevice hands none out: a submission names the image it
     *     writes, and the device asks this target which semaphores order it.
     *   * Asking whether a format can back a colour attachment. There is no
     *     neutral capability query, and adding one is a public-seam decision
     *     rather than something to settle inside a backend class.
     */
    VulkanDevice& m_Device;

    Core::Extent2D m_Extent{};
    Format m_Format = Format::Undefined;

    /**
     * One image per frame in flight, which is the count that lets the caller run
     * as far ahead as it said it would and no further. A swapchain's count is
     * the presentation engine's answer to the same question; with no
     * presentation engine there is nothing else to ask, so this is the answer.
     *
     * Held separately from m_Images.size() because Create() is what fills that,
     * and a Recreate has to know how many to build after Destroy() emptied it.
     */
    uint32_t m_ImageCount = 0u;

    std::vector<Image> m_Images;

    /**
     * Which image the next Acquire hands out, before the modulo. Monotonic
     * rather than pre-wrapped so that the first pass over the images is
     * distinguishable from later ones — see Create(), which starts it at zero
     * with every bSignalPending false.
     */
    uint64_t m_AcquireCount = 0u;
};
} // namespace Hikari::Rhi::Vulkan
