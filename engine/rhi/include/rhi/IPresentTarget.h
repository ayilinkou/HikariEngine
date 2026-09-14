#pragma once

#include <cstdint>
#include <optional>

#include <core/Extent2D.h>
#include <rhi/Barrier.h>
#include <rhi/Handles.h>
#include <rhi/RhiTypes.h>

namespace Hikari::Rhi
{
/**
 * Where a rendered frame ends up. A swapchain is one answer; N images with no
 * surface at all is another, and the renderer is not supposed to be able to
 * tell which it has.
 *
 * This is the seam that makes a headless run possible, so it is deliberately
 * narrower than a swapchain: no present mode, no colour space, no surface
 * capabilities. Anything a caller could only use by knowing it holds a
 * swapchain does not belong here.
 */

struct PresentTargetDesc
{
    /**
     * The size to create at. Pixels, not screen coordinates — the two differ on
     * high-DPI displays, and a swapchain is specified in the former.
     */
    Core::Extent2D Extent;

    /**
     * How many acquires the caller can have outstanding. Sizes what the target
     * keeps per outstanding acquire; it is not the image count, which the target
     * chooses and reports through GetImageCount().
     */
    uint32_t FramesInFlight = 2u;
};

struct AcquiredImage
{
    TextureHandle Texture;
    TextureViewHandle View;

    /**
     * Indexes the target's images, not the caller's frames in flight. The two
     * counts are unrelated and drivers do change the former across a Recreate.
     */
    uint32_t Index = 0u;

    /**
     * The target could not produce an image and the caller should Recreate. Not
     * an error: a resize races every frame and this is the ordinary way to find
     * out. Every other field is unspecified when this is set.
     */
    bool bNeedsRecreate = false;
};

class IPresentTarget
{
public:
    virtual ~IPresentTarget() = default;

    virtual Format GetFormat() const = 0;
    virtual Core::Extent2D GetExtent() const = 0;
    virtual uint32_t GetImageCount() const = 0;

    /**
     * The mode this target actually presents in, or nothing when it does not
     * present — an offscreen target has no display to pace against.
     *
     * Worth reporting rather than assuming: the default is a preference, so a
     * surface without Mailbox silently yields Fifo, and two runs measured under
     * different modes are not comparable.
     */
    virtual std::optional<PresentMode> GetPresentMode() const = 0;

    /**
     * The layout this target needs an image left in when the frame that drew it
     * ends, or TextureLayout::Undefined if it needs none — in which case the
     * caller records no closing barrier at all.
     *
     * A swapchain answers Present, and that is a hard requirement of the present
     * call rather than a convention: the presented subresource must be in
     * VK_IMAGE_LAYOUT_PRESENT_SRC_KHR (VUID-VkPresentInfoKHR-pImageIndices-01430),
     * and D3D12 spells the same idea D3D12_RESOURCE_STATE_PRESENT.
     *
     * A target with no presentation engine requires nothing, and Undefined says
     * so rather than naming a layout nobody asked for. It owns its images, no
     * one reads them between frames, and the next Acquire discards to Undefined
     * regardless — so any real answer would be an invented requirement, and none
     * of them is even free: RenderTarget costs a transition on the frame that
     * captured a screenshot (already in CopySrc by then), CopySrc costs one on
     * every other frame.
     *
     * Undefined is safe to mean "none" precisely because it cannot mean anything
     * else: it is illegal as a barrier destination
     * (VUID-VkImageMemoryBarrier2-newLayout-01198), so a caller who forgets to
     * check and transitions *to* this answer gets a validation error rather than
     * a subtly wrong frame.
     */
    virtual TextureLayout GetRequiredFinalLayout() const = 0;

    /**
     * An image to write, which exactly one submission then names as its
     * SubmitDesc::PresentImage before Present. Naming the image is what orders the
     * write, against the acquire and against whatever wrote the image last, so the
     * caller never handles the objects that do it.
     */
    [[nodiscard]] virtual AcquiredImage Acquire() = 0;

    /**
     * Presents `index`, whose submission must already have been made; presenting
     * an image no submission named throws. False means the target needs
     * recreating, on the same terms as AcquiredImage::bNeedsRecreate. A genuine
     * failure throws.
     */
    virtual bool Present(uint32_t index) = 0;

    /**
     * Rebuilds the target at `newExtent`, waiting for work still in flight
     * before it does. On success it invalidates every handle previously
     * returned by Acquire(), and may change GetImageCount(), so a caller holding
     * per-image state has to rebuild it.
     *
     * False means the target cannot be rebuilt yet and nothing was touched:
     * the existing images stay valid and the caller should ask again. A
     * window with no area puts a swapchain in that state, and a
     * caller cannot test for it itself without knowing it holds one. Not an
     * error, on the same terms as AcquiredImage::bNeedsRecreate; a genuine
     * failure throws.
     *
     * Marked [[nodiscard]] where Present is not, because the two answers fail
     * differently when dropped. A dropped Present costs one frame: the next
     * Acquire reports bNeedsRecreate for the same reason and the loop corrects
     * itself. A dropped Recreate corrects nothing — the caller rebuilds its own
     * per-image state against a target still at the old extent, believes that
     * extent is the window's, and nothing asks again.
     */
    [[nodiscard]] virtual bool Recreate(Core::Extent2D newExtent) = 0;
};
} // namespace Hikari::Rhi
