#pragma once

#include <cstdint>
#include <functional>
#include <span>

#include "vulkan/vulkan.hpp"

#include <rhi/RhiTypes.h>

namespace Hikari::Rhi::Vulkan
{
/**
 * Which Vulkan queue family serves each neutral QueueType role.
 *
 * The roles are neutral and the family indices are not, which is the whole
 * reason this type is backend-internal: D3D12 has three command list types and
 * no notion of a family at all, so the mapping between the two is something
 * only a Vulkan backend can hold.
 *
 * Roles routinely share a family. Any family that supports graphics can also
 * copy, so Copy has somewhere to go whether or not the hardware has a separate
 * DMA engine; IsDedicated is how a caller asks which of the two it got.
 */
struct QueueFamilies
{
    static constexpr uint32_t kInvalid = ~0u;

    uint32_t Graphics = kInvalid;
    uint32_t Compute = kInvalid;
    uint32_t Copy = kInvalid;

    /**
     * The family to submit `role` to. kInvalid only when no family can serve
     * the role at all: possible for Compute on a device whose graphics family
     * omits compute, and for every role when no graphics family was found.
     */
    uint32_t Get(QueueType role) const;

    /**
     * Whether `role` resolved to a family of its own rather than sharing the
     * graphics one. Graphics itself is never dedicated by this definition — it
     * is the family the other two are measured against.
     *
     * This describes the device, not where work is submitted. A caller that
     * wants a queue asks Get(); this answers "would using it buy any
     * parallelism".
     */
    bool IsDedicated(QueueType role) const;
};

/**
 * Whether queue family `familyIndex` can present to the target surface.
 *
 * A callback because that answer needs a surface as well as a physical device,
 * which makes it the one part of the selection below that is not a pure
 * function of the family list. Keeping the remainder pure is what lets the
 * selection rules be tested without a GPU, on the family layouts this machine
 * does not have.
 */
using PresentSupportFn = std::function<bool(uint32_t familyIndex)>;

/**
 * Resolves each role against the families a physical device reports.
 *
 * Graphics is the first family supporting graphics that can also present, when
 * presentation is required.
 *
 * Compute and Copy prefer a family that does not support graphics — the async
 * compute and DMA engines a discrete GPU exposes alongside its universal
 * family — and among those, the one advertising the fewest of graphics/compute/
 * copy. That tie-break keeps copies off the async compute engine on a device
 * that also exposes a transfer-only family, while still putting them there on a
 * device that does not. Both fall back to the graphics family when the device
 * offers nothing better.
 *
 * `bForceSingleQueue` skips that search entirely and resolves every role to the
 * graphics family, so that the arrangement an integrated GPU has — one
 * universal family, nothing to hand a resource over to — is reachable on a
 * device that exposes more. See DeviceDesc::bForceSingleQueue.
 */
QueueFamilies SelectQueueFamilies(std::span<const vk::QueueFamilyProperties> families,
                                  const PresentSupportFn& presentSupported, bool bRequirePresent,
                                  bool bForceSingleQueue);
} // namespace Hikari::Rhi::Vulkan
