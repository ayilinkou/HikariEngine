#include <catch2/catch_template_test_macros.hpp>
#include <catch2/catch_test_macros.hpp>

#include <atomic>
#include <vector>

#include <core/SerialJobSystem.h>
#include <core/SharedQueueJobSystem.h>

TEMPLATE_TEST_CASE("ParallelFor visits every index in [0, count) exactly once", "[JobSystem]",
                   SharedQueueJobSystem, SerialJobSystem)
{
    TestType jobSystem;

    constexpr uint32_t count = 137u; // deliberately not a multiple of grain
    constexpr uint32_t grain = 16u;

    std::vector<std::atomic<int>> hits(count);
    for (auto& hit : hits)
        hit = 0;

    jobSystem.ParallelFor(count, grain,
                          [&](uint32_t begin, uint32_t end)
                          {
                              for (uint32_t i = begin; i < end; ++i)
                                  hits[i].fetch_add(1, std::memory_order_relaxed);
                          });

    for (uint32_t i = 0; i < count; ++i)
        REQUIRE(hits[i].load() == 1);
}

TEMPLATE_TEST_CASE("ParallelFor with count == 0 never invokes fn", "[JobSystem]",
                   SharedQueueJobSystem, SerialJobSystem)
{
    TestType jobSystem;

    std::atomic<bool> called = false;
    jobSystem.ParallelFor(0u, 4u, [&](uint32_t, uint32_t) { called = true; });

    REQUIRE_FALSE(called.load());
}

TEMPLATE_TEST_CASE("ParallelFor with grain > count never invokes fn", "[JobSystem]",
                   SharedQueueJobSystem, SerialJobSystem)
{
    TestType jobSystem;

    std::atomic<bool> called = false;
    jobSystem.ParallelFor(5u, 10u, [&](uint32_t, uint32_t) { called = true; });

    REQUIRE_FALSE(called.load());
}

TEMPLATE_TEST_CASE(
    "ParallelFor with grain == count runs exactly one chunk covering the whole range",
    "[JobSystem]", SharedQueueJobSystem, SerialJobSystem)
{
    TestType jobSystem;

    std::atomic<int> callCount = 0;
    jobSystem.ParallelFor(8u, 8u,
                          [&](uint32_t begin, uint32_t end)
                          {
                              callCount.fetch_add(1);
                              REQUIRE(begin == 0u);
                              REQUIRE(end == 8u);
                          });

    REQUIRE(callCount.load() == 1);
}

TEMPLATE_TEST_CASE("ParallelFor with grain == 0 is treated as a single whole-range chunk",
                   "[JobSystem]", SharedQueueJobSystem, SerialJobSystem)
{
    TestType jobSystem;

    std::atomic<int> callCount = 0;
    jobSystem.ParallelFor(20u, 0u,
                          [&](uint32_t begin, uint32_t end)
                          {
                              callCount.fetch_add(1);
                              REQUIRE(begin == 0u);
                              REQUIRE(end == 20u);
                          });

    REQUIRE(callCount.load() == 1);
}

TEMPLATE_TEST_CASE("A job that throws does not deadlock the job system, and its exception surfaces "
                   "via the returned future",
                   "[JobSystem]", SharedQueueJobSystem, SerialJobSystem)
{
    TestType jobSystem;

    std::shared_future<void> throwingFuture =
        jobSystem.Submit([] { throw std::runtime_error("deliberate test failure"); });

    REQUIRE_THROWS_AS(throwingFuture.get(), std::runtime_error);

    // The actual "does not deadlock" assertion: if the throwing job had
    // wedged a worker thread or corrupted internal state, this second,
    // ordinary job would hang or never run.
    std::atomic<bool> secondJobRan = false;
    std::shared_future<void> okFuture = jobSystem.Submit([&] { secondJobRan = true; });
    okFuture.get();

    REQUIRE(secondJobRan.load());
}

TEMPLATE_TEST_CASE("Wait() rethrows an exception from a previously submitted job", "[JobSystem]",
                   SharedQueueJobSystem, SerialJobSystem)
{
    TestType jobSystem;

    jobSystem.Submit([] { throw std::runtime_error("deliberate test failure"); });

    REQUIRE_THROWS_AS(jobSystem.Wait(), std::runtime_error);
    REQUIRE_NOTHROW(jobSystem.Wait()); // pending list was drained; second call is a clean no-op
}

TEST_CASE("SharedQueueJobSystem reports a WorkerCount matching the requested thread count",
          "[JobSystem]")
{
    SharedQueueJobSystem jobSystem(3u);
    REQUIRE(jobSystem.WorkerCount() == 3u);
}

TEST_CASE("SharedQueueJobSystem clamps a requested thread count of 0 up to 1", "[JobSystem]")
{
    SharedQueueJobSystem jobSystem(0u);
    REQUIRE(jobSystem.WorkerCount() == 1u);
}

TEST_CASE("SerialJobSystem always reports a WorkerCount of 1", "[JobSystem]")
{
    SerialJobSystem jobSystem;
    REQUIRE(jobSystem.WorkerCount() == 1u);
}
