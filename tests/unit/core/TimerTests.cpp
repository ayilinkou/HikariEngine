#include <catch2/catch_test_macros.hpp>

#include <chrono>
#include <thread>

#include <core/Timer.h>

#include "CaptureStream.h"

// Timer has two shapes and the difference between them is whether anything is
// logged, so these tests capture the stream rather than assert on durations. A
// wall-clock assertion would be the flaky kind of test; "did it print" is not.

TEST_CASE("A named timer logs when it goes out of scope", "[Timer]")
{
    const std::string output = CaptureStream(stdout,
                                             [&]
                                             {
                                                 Timer timer("Named scope");
                                             });

    REQUIRE(output.find("Named scope took") != std::string::npos);
}

TEST_CASE("A nameless timer is a stopwatch and logs nothing", "[Timer]")
{
    const std::string output = CaptureStream(stdout,
                                             [&]
                                             {
                                                 Timer timer;
                                                 (void)timer.ElapsedMs();
                                             });

    REQUIRE(output.empty());
}

TEST_CASE("EndTimer() logs immediately, and only once", "[Timer]")
{
    const std::string output = CaptureStream(stdout,
                                             [&]
                                             {
                                                 Timer timer("Early");
                                                 timer.EndTimer();
                                                 timer.EndTimer();
                                             });

    // Once for the explicit call, and not a second time when the destructor
    // runs — the message would otherwise double for every caller that wanted to
    // report before the end of its scope.
    const size_t first = output.find("Early took");
    REQUIRE(first != std::string::npos);
    REQUIRE(output.find("Early took", first + 1) == std::string::npos);
}

TEST_CASE("InvalidateTimer() suppresses the message but keeps the elapsed time", "[Timer]")
{
    float elapsedMs = -1.f;

    const std::string output = CaptureStream(stdout,
                                             [&]
                                             {
                                                 Timer timer("Suppressed");
                                                 timer.InvalidateTimer();
                                                 elapsedMs = timer.ElapsedMs();
                                             });

    REQUIRE(output.empty());
    REQUIRE(elapsedMs >= 0.f);
}

TEST_CASE("Elapsed time keeps running until the timer ends, then freezes", "[Timer]")
{
    Timer timer;

    const float first = timer.ElapsedMs();
    std::this_thread::sleep_for(std::chrono::milliseconds(2));
    const float second = timer.ElapsedMs();

    REQUIRE(second > first);

    timer.EndTimer();
    const float atEnd = timer.ElapsedMs();
    std::this_thread::sleep_for(std::chrono::milliseconds(2));

    // The interval a caller reads after ending has to be the one that was
    // measured, not the time since. InvalidateTimer() leaves the clock running
    // precisely because it is not an end.
    REQUIRE(timer.ElapsedMs() == atEnd);
}
