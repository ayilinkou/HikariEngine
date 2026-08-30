#pragma once

#include <compare>
#include <cstdint>

namespace Hikari::Core
{

/**
 * A typed 32-bit identity for a resource owned by a HandlePool: the slot the
 * resource lives in, plus the generation that slot was on when the handle was
 * issued.
 *
 * `Tag` is never defined — it exists only to make handles to different kinds of
 * resource distinct, incompatible types, so a buffer handle cannot be passed
 * where a texture handle is expected:
 *
 *     using BufferHandle = Handle<struct BufferTag>;
 *
 * The generation half is the reason for preferring this over a bare index:
 * HandlePool bumps a slot's generation when it is released, so a handle kept
 * past the lifetime of what it referred to stops matching its slot. A
 * use-after-free becomes a detectable mismatch that can be logged, instead of
 * silently addressing whatever was created in that slot next.
 */
template <typename Tag>
struct Handle
{
    static constexpr uint32_t kIndexBits = 24;
    static constexpr uint32_t kGenerationBits = 8;

    static constexpr uint32_t kIndexMask = (1u << kIndexBits) - 1u;           // 0x00FFFFFF
    static constexpr uint32_t kGenerationMask = (1u << kGenerationBits) - 1u; // 0x000000FF

    static constexpr uint32_t kInvalid = 0xFFFFFFFF;

    /**
     * The one index a valid handle may never carry: index kIndexMask at
     * generation kGenerationMask *is* kInvalid, so excluding that index keeps
     * "valid handle" and kInvalid disjoint at every generation. Costs one slot
     * out of 16.7 million, and removes the need to special-case a generation.
     */
    static constexpr uint32_t kMaxIndex = kIndexMask - 1u;

    uint32_t Value = kInvalid; // index:24 | generation:8

    constexpr uint32_t Index() const { return Value & kIndexMask; }
    constexpr uint32_t Generation() const { return Value >> kIndexBits; }
    constexpr bool IsValid() const { return Value != kInvalid; }
    constexpr auto operator<=>(const Handle&) const = default;

    /**
     * Packs the two halves. The bit layout is deliberately known only here, so
     * that HandlePool never open-codes it and the two cannot drift apart.
     * `index` must be <= kMaxIndex and `generation` <= kGenerationMask; both
     * are masked rather than checked, since the only caller derives them from
     * its own storage.
     */
    static constexpr Handle FromIndexAndGeneration(uint32_t index, uint32_t generation)
    {
        return Handle{((generation & kGenerationMask) << kIndexBits) | (index & kIndexMask)};
    }
};
} // namespace Hikari::Core
