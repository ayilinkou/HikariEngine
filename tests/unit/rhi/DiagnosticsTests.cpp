#include <catch2/catch_test_macros.hpp>

#include <rhi/Diagnostics.h>

#include <atomic>
#include <format>
#include <string>
#include <thread>
#include <vector>

/**
 * CPU-only: no instance, no device, no ICD needed. That is the reason Diagnostics
 * is a neutral class rather than a Vulkan one — a clean run emits no validation
 * messages at all, so without these tests the counting, the threshold and the
 * ring buffer would have no coverage on any machine, and a miswired diagnostic
 * path would look exactly like a healthy one.
 */
using namespace Hikari::Rhi;

namespace
{
/**
 * Collects what the sink was handed, so a test can assert the message survives
 * the trip verbatim rather than only that a counter moved.
 */
struct Sink
{
    std::vector<std::pair<DiagnosticSeverity, std::string>> Received;

    Diagnostics::Desc Desc(ValidationPolicy policy = ValidationPolicy::Count,
                           DiagnosticSeverity minSeverity = DiagnosticSeverity::Info)
    {
        return Diagnostics::Desc{
            .Policy = policy,
            .MinSeverity = minSeverity,
            .OnMessage = [this](DiagnosticSeverity severity, std::string_view message)
            { Received.emplace_back(severity, std::string(message)); }};
    }
};
} // namespace

/**
 * The threshold in Report(), and the messenger severity flags VulkanDevice
 * derives from MinSeverity, are both `<` comparisons on these enumerators. A
 * reorder would silently drop every message or none, which is invisible on a run
 * that produces no messages either way.
 */
TEST_CASE("Neutral severity ordering is ascending", "[rhi][diagnostics]")
{
    STATIC_REQUIRE(DiagnosticSeverity::Info < DiagnosticSeverity::Warning);
    STATIC_REQUIRE(DiagnosticSeverity::Warning < DiagnosticSeverity::Error);
}

TEST_CASE("Messages are counted per severity", "[rhi][diagnostics]")
{
    Sink sink;
    Diagnostics diagnostics(sink.Desc());

    diagnostics.Report(DiagnosticSeverity::Info, "info");
    diagnostics.Report(DiagnosticSeverity::Warning, "warning");
    diagnostics.Report(DiagnosticSeverity::Warning, "warning again");
    diagnostics.Report(DiagnosticSeverity::Error, "error");

    REQUIRE(diagnostics.InfoCount() == 1);
    REQUIRE(diagnostics.WarningCount() == 2);
    REQUIRE(diagnostics.ErrorCount() == 1);
    REQUIRE(diagnostics.DroppedMessageCount() == 0);
}

TEST_CASE("The sink receives the severity and message verbatim", "[rhi][diagnostics]")
{
    Sink sink;
    Diagnostics diagnostics(sink.Desc());

    diagnostics.Report(DiagnosticSeverity::Error, "Type: {Validation}. Msg: something went wrong");

    REQUIRE(sink.Received.size() == 1);
    REQUIRE(sink.Received[0].first == DiagnosticSeverity::Error);
    REQUIRE(sink.Received[0].second == "Type: {Validation}. Msg: something went wrong");
}

TEST_CASE("Ignore drops everything", "[rhi][diagnostics]")
{
    Sink sink;
    Diagnostics diagnostics(sink.Desc(ValidationPolicy::Ignore));

    diagnostics.Report(DiagnosticSeverity::Error, "error");
    diagnostics.Report(DiagnosticSeverity::Warning, "warning");

    REQUIRE(diagnostics.ErrorCount() == 0);
    REQUIRE(diagnostics.WarningCount() == 0);
    REQUIRE(diagnostics.RecentMessages().empty());
    REQUIRE(sink.Received.empty());
}

TEST_CASE("MinSeverity drops anything below the threshold", "[rhi][diagnostics]")
{
    Sink sink;
    Diagnostics diagnostics(sink.Desc(ValidationPolicy::Count, DiagnosticSeverity::Warning));

    diagnostics.Report(DiagnosticSeverity::Info, "info");
    diagnostics.Report(DiagnosticSeverity::Warning, "warning");
    diagnostics.Report(DiagnosticSeverity::Error, "error");

    REQUIRE(diagnostics.InfoCount() == 0);
    REQUIRE(diagnostics.WarningCount() == 1);
    REQUIRE(diagnostics.ErrorCount() == 1);
    REQUIRE(sink.Received.size() == 2);
}

TEST_CASE("A Diagnostics with no sink still counts", "[rhi][diagnostics]")
{
    // The device creates exactly this when the caller supplies none, so
    // GetDiagnostics() is always valid.
    Diagnostics diagnostics;

    diagnostics.Report(DiagnosticSeverity::Error, "error");

    REQUIRE(diagnostics.ErrorCount() == 1);
    REQUIRE(diagnostics.Policy() == ValidationPolicy::Count);
    REQUIRE(diagnostics.MinSeverity() == DiagnosticSeverity::Info);
}

TEST_CASE("Capture keeps messages in order below capacity", "[rhi][diagnostics]")
{
    Diagnostics diagnostics;

    diagnostics.Report(DiagnosticSeverity::Error, "first");
    diagnostics.Report(DiagnosticSeverity::Error, "second");
    diagnostics.Report(DiagnosticSeverity::Error, "third");

    const std::vector<std::string> recent = diagnostics.RecentMessages();
    REQUIRE(recent == std::vector<std::string>{"first", "second", "third"});
    REQUIRE(diagnostics.DroppedMessageCount() == 0);
}

TEST_CASE("Capture keeps the most recent messages once full", "[rhi][diagnostics]")
{
    Diagnostics diagnostics;

    constexpr size_t kOverflow = 10;
    const size_t total = Diagnostics::kCaptureCapacity + kOverflow;
    for (size_t i = 0; i < total; ++i)
        diagnostics.Report(DiagnosticSeverity::Error, std::format("message {}", i));

    const std::vector<std::string> recent = diagnostics.RecentMessages();

    REQUIRE(recent.size() == Diagnostics::kCaptureCapacity);
    REQUIRE(diagnostics.DroppedMessageCount() == kOverflow);

    // Every message is still counted; only the capture is bounded.
    REQUIRE(diagnostics.ErrorCount() == total);

    // Oldest first, and the oldest survivor is the one after those dropped.
    REQUIRE(recent.front() == std::format("message {}", kOverflow));
    REQUIRE(recent.back() == std::format("message {}", total - 1));
}

TEST_CASE("Reset clears counters and capture but keeps the policy", "[rhi][diagnostics]")
{
    Sink sink;
    Diagnostics diagnostics(sink.Desc(ValidationPolicy::Count, DiagnosticSeverity::Warning));

    for (size_t i = 0; i < Diagnostics::kCaptureCapacity + 5; ++i)
        diagnostics.Report(DiagnosticSeverity::Error, "error");

    diagnostics.Reset();

    REQUIRE(diagnostics.ErrorCount() == 0);
    REQUIRE(diagnostics.WarningCount() == 0);
    REQUIRE(diagnostics.DroppedMessageCount() == 0);
    REQUIRE(diagnostics.RecentMessages().empty());
    REQUIRE(diagnostics.Policy() == ValidationPolicy::Count);
    REQUIRE(diagnostics.MinSeverity() == DiagnosticSeverity::Warning);

    // Still usable afterwards, and the ring starts over rather than resuming
    // mid-buffer.
    diagnostics.Report(DiagnosticSeverity::Error, "after reset");
    REQUIRE(diagnostics.RecentMessages() == std::vector<std::string>{"after reset"});
}

/**
 * The driver calls the debug callback on whichever thread raised the message, so
 * concurrent Report() is the normal case rather than an edge one.
 */
TEST_CASE("Concurrent Report counts every message exactly once", "[rhi][diagnostics]")
{
    constexpr size_t kThreads = 8;
    constexpr size_t kPerThread = 500;

    std::atomic<uint64_t> sinkCalls{0};
    Diagnostics diagnostics(Diagnostics::Desc{
        .OnMessage = [&sinkCalls](DiagnosticSeverity, std::string_view)
        { sinkCalls.fetch_add(1, std::memory_order_relaxed); }});

    std::vector<std::thread> threads;
    threads.reserve(kThreads);
    for (size_t t = 0; t < kThreads; ++t)
    {
        threads.emplace_back(
            [&diagnostics, t]
            {
                for (size_t i = 0; i < kPerThread; ++i)
                    diagnostics.Report(DiagnosticSeverity::Error, std::format("t{} #{}", t, i));
            });
    }

    for (std::thread& thread : threads)
        thread.join();

    constexpr uint64_t kTotal = kThreads * kPerThread;
    REQUIRE(diagnostics.ErrorCount() == kTotal);
    REQUIRE(sinkCalls.load(std::memory_order_relaxed) == kTotal);
    REQUIRE(diagnostics.RecentMessages().size() == Diagnostics::kCaptureCapacity);
    REQUIRE(diagnostics.DroppedMessageCount() == kTotal - Diagnostics::kCaptureCapacity);
}
