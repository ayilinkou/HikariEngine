#include <catch2/catch_test_macros.hpp>

#include <core/Handle.h>
#include <core/HandlePool.h>

#include <cstdint>
#include <memory>
#include <type_traits>
#include <vector>

using namespace Hikari::Core;

using TestHandle = Handle<struct TestTag>;
using IntPool = HandlePool<int, TestTag>;

TEST_CASE("Handle default-constructs to the invalid value", "[Handle]")
{
    TestHandle handle;

    REQUIRE(handle.Value == TestHandle::kInvalid);
    REQUIRE_FALSE(handle.IsValid());
}

TEST_CASE("Handle packs the index in the low 24 bits and the generation in the high 8", "[Handle]")
{
    const TestHandle handle = TestHandle::FromIndexAndGeneration(0x00ABCDEF, 0x42);

    REQUIRE(handle.Index() == 0x00ABCDEFu);
    REQUIRE(handle.Generation() == 0x42u);
    REQUIRE(handle.Value == 0x42ABCDEFu);
    REQUIRE(handle.IsValid());
}

TEST_CASE("Handle stays valid at the extremes of both fields", "[Handle]")
{
    // kMaxIndex exists precisely so that no valid handle can collide with
    // kInvalid, including at the highest generation.
    const TestHandle highest =
        TestHandle::FromIndexAndGeneration(TestHandle::kMaxIndex, TestHandle::kGenerationMask);
    REQUIRE(highest.IsValid());
    REQUIRE(highest.Value != TestHandle::kInvalid);

    const TestHandle lowest = TestHandle::FromIndexAndGeneration(0, 0);
    REQUIRE(lowest.IsValid());
    REQUIRE(lowest.Value == 0u);
}

TEST_CASE("Handles compare by value and handles of different tags are distinct types", "[Handle]")
{
    REQUIRE(TestHandle::FromIndexAndGeneration(3, 1) == TestHandle::FromIndexAndGeneration(3, 1));
    REQUIRE(TestHandle::FromIndexAndGeneration(3, 1) != TestHandle::FromIndexAndGeneration(3, 2));
    REQUIRE(TestHandle::FromIndexAndGeneration(3, 1) < TestHandle::FromIndexAndGeneration(4, 1));

    static_assert(!std::is_same_v<Handle<struct ATag>, Handle<struct BTag>>,
                  "The tag exists to stop handles to different resource kinds converting.");
}

TEST_CASE("HandlePool Create returns a handle resolving to the stored value", "[HandlePool]")
{
    IntPool pool;
    REQUIRE(pool.Empty());

    const TestHandle handle = pool.Create(7);

    REQUIRE(handle.IsValid());
    REQUIRE(pool.IsValid(handle));
    REQUIRE(pool.Size() == 1);
    REQUIRE(pool.Get(handle) != nullptr);
    REQUIRE(*pool.Get(handle) == 7);
}

TEST_CASE("HandlePool Create with no arguments default-constructs the payload", "[HandlePool]")
{
    IntPool pool;

    const TestHandle handle = pool.Create();

    REQUIRE(pool.Get(handle) != nullptr);
    REQUIRE(*pool.Get(handle) == 0);
}

TEST_CASE("HandlePool Get on a default-constructed handle returns nullptr", "[HandlePool]")
{
    IntPool pool;
    pool.Create(1); // so the pool is non-empty and index 0 exists

    const TestHandle invalid;

    REQUIRE(pool.Get(invalid) == nullptr);
    REQUIRE_FALSE(pool.IsValid(invalid));
    REQUIRE_FALSE(pool.Release(invalid));
}

TEST_CASE("HandlePool Get on a handle whose index was never allocated returns nullptr",
          "[HandlePool]")
{
    IntPool pool;
    pool.Create(1);

    REQUIRE(pool.Get(TestHandle::FromIndexAndGeneration(5, 0)) == nullptr);
}

TEST_CASE("HandlePool Size counts live elements while released slots stay allocated",
          "[HandlePool]")
{
    IntPool pool;
    const TestHandle first = pool.Create(1);
    pool.Create(2);
    REQUIRE(pool.Size() == 2);

    REQUIRE(pool.Release(first));

    REQUIRE(pool.Size() == 1);
    REQUIRE_FALSE(pool.Empty());
    // The slot itself was not given back, so it can be reused without growing.
    REQUIRE(pool.Capacity() >= 2);
}

TEST_CASE("HandlePool rejects a released handle rather than resolving it to the slot's next "
          "occupant",
          "[HandlePool]")
{
    IntPool pool;
    const TestHandle stale = pool.Create(1);
    REQUIRE(pool.Release(stale));

    REQUIRE(pool.Get(stale) == nullptr);
    REQUIRE_FALSE(pool.IsValid(stale));

    // Reuses the same slot, so only the generation distinguishes the two.
    const TestHandle reused = pool.Create(2);
    REQUIRE(reused.Index() == stale.Index());
    REQUIRE(reused.Generation() != stale.Generation());

    REQUIRE(pool.Get(stale) == nullptr);
    REQUIRE(*pool.Get(reused) == 2);
}

TEST_CASE("HandlePool Release of an already released handle reports failure and leaves the pool "
          "untouched",
          "[HandlePool]")
{
    IntPool pool;
    const TestHandle handle = pool.Create(1);
    const TestHandle survivor = pool.Create(2);

    REQUIRE(pool.Release(handle));
    REQUIRE_FALSE(pool.Release(handle)); // a double release must not corrupt the free list

    REQUIRE(pool.Size() == 1);
    REQUIRE(*pool.Get(survivor) == 2);

    // One free slot went in, so exactly one comes back out before the pool
    // starts appending fresh slots again.
    const TestHandle reused = pool.Create(3);
    const TestHandle fresh = pool.Create(4);
    REQUIRE(reused.Index() == handle.Index());
    REQUIRE(fresh.Index() != handle.Index());
    REQUIRE(fresh.Index() != survivor.Index());
}

TEST_CASE("HandlePool reuses released slots in the order they were released", "[HandlePool]")
{
    IntPool pool;
    const TestHandle first = pool.Create(1);
    const TestHandle second = pool.Create(2);
    const TestHandle third = pool.Create(3);

    // Released out of order: the free list is FIFO, so reuse follows release
    // order rather than reversing it. That ordering is what keeps eight
    // generation bits sufficient.
    REQUIRE(pool.Release(second));
    REQUIRE(pool.Release(first));
    REQUIRE(pool.Release(third));

    REQUIRE(pool.Create(4).Index() == second.Index());
    REQUIRE(pool.Create(5).Index() == first.Index());
    REQUIRE(pool.Create(6).Index() == third.Index());

    // Free list exhausted: the next one is a fresh slot.
    REQUIRE(pool.Create(7).Index() == 3u);
    REQUIRE(pool.Size() == 4);
}

TEST_CASE("HandlePool generation wraps after 256 reuses of one slot", "[HandlePool]")
{
    IntPool pool;
    TestHandle handle = pool.Create(1);
    const TestHandle original = handle;
    REQUIRE(original.Generation() == 0u);

    // One slot, so every cycle reuses it and bumps its generation. Eight bits
    // wrap after 256 — this documents the limit rather than endorsing it; see
    // the FIFO reasoning in HandlePool.h for why it is acceptable in practice.
    for (uint32_t cycle = 0; cycle < TestHandle::kGenerationMask; ++cycle)
    {
        REQUIRE(pool.Release(handle));
        handle = pool.Create(2);

        REQUIRE(handle.Index() == original.Index());
        REQUIRE(handle.Generation() == cycle + 1u);
        REQUIRE(handle != original);
        REQUIRE(pool.Get(original) == nullptr);
    }

    // The 256th release takes the generation back to where it started, and the
    // long-dead original handle becomes indistinguishable from the live one.
    REQUIRE(pool.Release(handle));
    handle = pool.Create(3);

    REQUIRE(handle.Generation() == 0u);
    REQUIRE(handle == original);
    REQUIRE(pool.IsValid(original));
}

TEST_CASE("HandlePool creates within its reserved slots without reallocating", "[HandlePool]")
{
    IntPool pool;
    pool.Reserve(4);

    const uint32_t reserved = pool.Capacity();
    REQUIRE(reserved >= 4u);

    std::vector<TestHandle> handles;
    for (int value = 0; value < 4; ++value)
        handles.push_back(pool.Create(value));

    REQUIRE(pool.Capacity() == reserved);

    // Pointers from Get() are only promised to survive while Capacity() does.
    const int* pFirst = pool.Get(handles[0]);
    REQUIRE(pFirst != nullptr);
    REQUIRE(*pFirst == 0);
}

TEST_CASE("HandlePool grows past its reserved capacity and keeps every live handle resolvable",
          "[HandlePool]")
{
    IntPool pool;
    pool.Reserve(4);
    const uint32_t reserved = pool.Capacity();

    std::vector<TestHandle> handles;
    for (int value = 0; value < static_cast<int>(reserved) + 1; ++value)
        handles.push_back(pool.Create(value));

    REQUIRE(pool.Capacity() > reserved);
    REQUIRE(pool.Size() == static_cast<uint32_t>(handles.size()));

    for (size_t i = 0; i < handles.size(); ++i)
    {
        const int* pValue = pool.Get(handles[i]);
        REQUIRE(pValue != nullptr);
        REQUIRE(*pValue == static_cast<int>(i));
    }
}

TEST_CASE("HandlePool Release frees what the payload owned instead of waiting for teardown",
          "[HandlePool]")
{
    HandlePool<std::shared_ptr<int>, TestTag> pool;

    std::shared_ptr<int> shared = std::make_shared<int>(7);
    REQUIRE(shared.use_count() == 1);

    const TestHandle handle = pool.Create(shared);
    REQUIRE(shared.use_count() == 2);

    REQUIRE(pool.Release(handle));
    REQUIRE(shared.use_count() == 1);
}
