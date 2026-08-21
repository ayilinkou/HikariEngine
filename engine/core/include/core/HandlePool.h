#pragma once

#include <cstdint>
#include <stdexcept>
#include <type_traits>
#include <utility>
#include <vector>

#include <core/Handle.h>

// Owns `T` instances in a contiguous slot array addressed by Handle<Tag>.
// Slots are reused after release, and each carries a generation counter that
// is bumped on release, so a handle outliving what it referred to is rejected
// by Get() rather than resolving to the slot's next occupant.
//
// Storage is a single std::vector, so Get() hands out a pointer into it:
//
//     Pointers returned by Get() are invalidated by any Create() that grows
//     Capacity(), exactly like std::vector::push_back. Treat them as valid
//     only until the next Create(); the handle is the thing worth keeping.
//
// The free list is FIFO rather than LIFO. That is what makes eight generation
// bits sufficient: a released slot is not handed out again until every other
// free slot has been, so wrapping a slot's generation back onto a stale
// handle's value takes 256 full cycles through the pool rather than 256
// create/release pairs in one spot. High-churn per-frame resources would
// invalidate that reasoning and want a wider generation field instead.
//
// `T` must be default-constructible and move-assignable: a free slot holds a
// default-constructed `T`, and Release() assigns one back over the payload so
// that whatever the payload owns is freed at Release() rather than lingering
// until the pool itself is destroyed.
template <typename T, typename Tag>
class HandlePool
{
public:
    using HandleType = Handle<Tag>;

    static_assert(std::is_default_constructible_v<T>,
                  "HandlePool<T> requires T to be default-constructible: free slots hold a "
                  "default-constructed T.");
    static_assert(std::is_move_assignable_v<T>,
                  "HandlePool<T> requires T to be move-assignable: Release() assigns T{} over "
                  "the payload to free it eagerly.");

    // One slot short of 2^24 — see Handle::kMaxIndex.
    static constexpr uint32_t kMaxSize = HandleType::kMaxIndex + 1u;

    // Constructs a `T` from `args` in a free slot, or in a newly appended one
    // if none is free, and returns a handle to it. Throws std::length_error if
    // the pool already holds kMaxSize elements.
    template <typename... Args>
    HandleType Create(Args&&... args)
    {
        const uint32_t index = ClaimSlot();

        Slot& slot = m_Slots[index];
        // A free slot's payload is already a default-constructed T (Release()
        // put it there), so the no-argument case has nothing left to do.
        if constexpr (sizeof...(Args) > 0)
            slot.Payload = T(std::forward<Args>(args)...);
        slot.bAlive = true;
        ++m_LiveCount;

        return HandleType::FromIndexAndGeneration(index, slot.Generation);
    }

    // Frees the slot `handle` refers to and bumps its generation, so every
    // outstanding copy of `handle` becomes stale. Returns false, changing
    // nothing, if `handle` was already stale or never valid — so a double
    // release is reported rather than corrupting the free list.
    bool Release(HandleType handle)
    {
        Slot* pSlot = FindLiveSlot(handle);
        if (!pSlot)
            return false;

        pSlot->Payload = T{};
        pSlot->bAlive = false;
        pSlot->Generation = static_cast<uint8_t>(pSlot->Generation + 1u);

        PushFree(handle.Index());
        --m_LiveCount;
        return true;
    }

    // Returns nullptr for a stale, released or default-constructed handle.
    T* Get(HandleType handle)
    {
        Slot* pSlot = FindLiveSlot(handle);
        return pSlot ? &pSlot->Payload : nullptr;
    }

    const T* Get(HandleType handle) const
    {
        const Slot* pSlot = FindLiveSlot(handle);
        return pSlot ? &pSlot->Payload : nullptr;
    }

    bool IsValid(HandleType handle) const { return FindLiveSlot(handle) != nullptr; }

    // Number of live elements — not the number of slots, which stays at its
    // high-water mark so that released indices can be reused.
    uint32_t Size() const { return m_LiveCount; }
    bool Empty() const { return m_LiveCount == 0u; }

    // How many slots can exist before the storage reallocates, which is also
    // how far Create() can be called before pointers from Get() dangle.
    uint32_t Capacity() const { return static_cast<uint32_t>(m_Slots.capacity()); }

    // Grows storage up front so that the first `slotCount` slots can be
    // created without invalidating pointers from Get().
    void Reserve(uint32_t slotCount)
    {
        if (slotCount > kMaxSize)
            throw std::length_error("HandlePool::Reserve() beyond the 24-bit index range.");

        m_Slots.reserve(slotCount);
    }

private:
    static constexpr uint32_t kNoSlot = 0xFFFFFFFF;

    struct Slot
    {
        T Payload{};
        uint32_t NextFree = kNoSlot; // only meaningful while !bAlive
        uint8_t Generation = 0;      // wraps at 256; see the class comment
        bool bAlive = false;
    };

    // Takes the oldest free slot, or appends one when the free list is empty.
    uint32_t ClaimSlot()
    {
        if (m_FreeHead == kNoSlot)
        {
            if (m_Slots.size() >= kMaxSize)
                throw std::length_error("HandlePool is full: the 24-bit index range is "
                                        "exhausted.");

            m_Slots.emplace_back();
            return static_cast<uint32_t>(m_Slots.size() - 1u);
        }

        const uint32_t index = m_FreeHead;
        m_FreeHead = m_Slots[index].NextFree;
        if (m_FreeHead == kNoSlot)
            m_FreeTail = kNoSlot;

        m_Slots[index].NextFree = kNoSlot;
        return index;
    }

    // Appends to the tail, which is what makes reuse FIFO.
    void PushFree(uint32_t index)
    {
        m_Slots[index].NextFree = kNoSlot;

        if (m_FreeTail == kNoSlot)
            m_FreeHead = index;
        else
            m_Slots[m_FreeTail].NextFree = index;

        m_FreeTail = index;
    }

    const Slot* FindLiveSlot(HandleType handle) const
    {
        if (!handle.IsValid())
            return nullptr;

        const uint32_t index = handle.Index();
        if (index >= m_Slots.size())
            return nullptr;

        const Slot& slot = m_Slots[index];
        if (!slot.bAlive || static_cast<uint32_t>(slot.Generation) != handle.Generation())
            return nullptr;

        return &slot;
    }

    Slot* FindLiveSlot(HandleType handle)
    {
        return const_cast<Slot*>(std::as_const(*this).FindLiveSlot(handle));
    }

    std::vector<Slot> m_Slots;
    uint32_t m_FreeHead = kNoSlot;
    uint32_t m_FreeTail = kNoSlot;
    uint32_t m_LiveCount = 0;
};
