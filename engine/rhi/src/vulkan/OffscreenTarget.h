#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

#include <rhi/Barrier.h>
#include <rhi/Handles.h>
#include <rhi/IPresentTarget.h>
#include <rhi/RhiTypes.h>

namespace Hikari::Rhi::Vulkan
{
class VulkanDevice;

// IPresentTarget over N images this target owns, with no surface, no swapchain
// and no presentation engine.
//
// The other half of what makes a headless run possible. Everything a swapchain
// gets from the window system is decided here instead: the format, the extent
// and how many images there are. Acquire never fails and never asks to be
// recreated — the two states a swapchain reaches only because a surface can
// change underneath it.
//
// Deliberately not "a swapchain minus presentation": what it drops is the
// presentation engine, and what it therefore has to add is somewhere for the
// caller's render-complete signal to go. See Acquire() for that.
class OffscreenTarget final : public IPresentTarget
{
public:
    // Throws if the device can back none of the formats an offscreen image can
    // be created in, on the same terms as any other unrecoverable init failure.
    OffscreenTarget(VulkanDevice& device, const PresentTargetDesc& desc);
    ~OffscreenTarget() override;

    OffscreenTarget(const OffscreenTarget&) = delete;
    OffscreenTarget& operator=(const OffscreenTarget&) = delete;

    Format GetFormat() const override { return m_Format; }
    Extent2D GetExtent() const override { return m_Extent; }
    uint32_t GetImageCount() const override { return static_cast<uint32_t>(m_Images.size()); }

    // None: this target imposes no layout on the image it hands back, so the
    // frame that drew it records no closing barrier. See the declaration on
    // IPresentTarget for why Undefined is the right way to say that, and why no
    // real layout would be an improvement.
    TextureLayout GetRequiredFinalLayout() const override { return TextureLayout::Undefined; }

    [[nodiscard]] AcquiredImage Acquire() override;
    SemaphoreHandle GetRenderCompleteSemaphore(uint32_t index) const override;
    bool Present(uint32_t index) override;
    [[nodiscard]] bool Recreate(Extent2D newExtent) override;

    // The contents of image `index`, tightly packed and row-major:
    // Width * Height * BytesPerTexel(GetFormat()) bytes, no row padding.
    //
    // Not on IPresentTarget, and not an oversight. A swapchain's images belong
    // to the presentation engine and may only be touched between an acquire and
    // the present that hands them back, so "read that image now" is not a
    // question a swapchain can answer at all — a caller wanting a windowed
    // capture copies the image inside its own frame instead. This target owns
    // its images outright, which is exactly what makes the question answerable.
    //
    // `currentLayout` is the layout the caller's last barrier left the image in.
    // The target records no commands of its own during a frame, so it cannot
    // know; naming it is what keeps the transition correct instead of plausible.
    // The image is left in CopySrc, which costs the caller nothing — the next
    // Acquire transitions from Undefined regardless.
    //
    // Blocks until the copy has completed, and allocates a staging buffer per
    // call. Both are deliberate: this is a capture, taken once or twice in a
    // run, and a buffer kept alive between captures would outlive the extent it
    // was sized for.
    [[nodiscard]] std::vector<std::byte> Readback(uint32_t index, TextureLayout currentLayout);

private:
    struct Image
    {
        TextureHandle Texture;
        TextureViewHandle View;
        SemaphoreHandle RenderComplete;

        // Whether RenderComplete has been signalled by a submit nothing has
        // waited on yet. Set by Present(), which is the caller asserting it
        // signalled the semaphore, and cleared by the Acquire() that hands it
        // back as a wait.
        //
        // A binary semaphore has to be unsignalled when a signal operation
        // reaches the device (VUID-vkQueueSubmit-pSignalSemaphores-00067), so
        // without this the second frame to reach a given image would signal an
        // already-signalled semaphore. There is no presentation engine here to
        // consume the signal, so the target consumes it itself.
        bool bSignalPending = false;
    };

    void Create(Extent2D extent);
    void Destroy();

    // VulkanDevice rather than IDevice, and closer to the latter than it looks:
    // creating and destroying the textures and views, and WaitIdle, are all
    // neutral already. Two things are not, and neither is an oversight.
    //
    //   * Semaphores. IDevice deliberately hands none out — SemaphoreHandle
    //     exists so that a caller can name one a target owns, and widening the
    //     device to create them for one internal user would undo that.
    //   * Asking whether a format can back a colour attachment. There is no
    //     neutral capability query, and adding one is a public-seam decision
    //     rather than something to settle inside a backend class.
    //
    // Worth revisiting when submission moves behind the RHI in Stage 8: the
    // target waits and signals internally from then on, so what a second
    // backend would share is a different shape from this one.
    VulkanDevice& m_Device;

    Extent2D m_Extent{};
    Format m_Format = Format::Undefined;

    // One image per frame in flight, which is the count that lets the caller run
    // as far ahead as it said it would and no further. A swapchain's count is
    // the presentation engine's answer to the same question; with no
    // presentation engine there is nothing else to ask, so this is the answer.
    //
    // Held separately from m_Images.size() because Create() is what fills that,
    // and a Recreate has to know how many to build after Destroy() emptied it.
    uint32_t m_ImageCount = 0u;

    std::vector<Image> m_Images;

    // Which image the next Acquire hands out, before the modulo. Monotonic
    // rather than pre-wrapped so that the first pass over the images is
    // distinguishable from later ones — see Create(), which starts it at zero
    // with every bSignalPending false.
    uint64_t m_AcquireCount = 0u;

    // The storage AcquiredImage::WaitSemaphores points at, so that the span
    // outlives the call that returned it and is invalidated by the next
    // Acquire, exactly as the interface documents. A single handle because a
    // frame can only be waiting on one previous write of one image.
    SemaphoreHandle m_CurrentWait{};
};
} // namespace Hikari::Rhi::Vulkan
