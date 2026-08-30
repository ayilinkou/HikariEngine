#include <catch2/catch_test_macros.hpp>

#include <core/Log.h>

#include "CaptureStream.h"

using namespace Hikari::Core;

namespace
{
// Restores g_MinSeverity after each test, so filtering tests don't leak state
// into whatever test case runs next.
struct ScopedMinSeverity
{
    LogSeverity Previous;
    explicit ScopedMinSeverity(LogSeverity newMin) : Previous(Log::g_MinSeverity)
    {
        Log::g_MinSeverity = newMin;
    }
    ~ScopedMinSeverity() { Log::g_MinSeverity = Previous; }
};

constexpr LogCategory TestCategory("Test");
} // namespace

TEST_CASE("SeverityColor maps each severity to its ANSI code", "[Log]")
{
    REQUIRE(Log::SeverityColor(LogSeverity::Info) == Log::Reset);
    REQUIRE(Log::SeverityColor(LogSeverity::Warning) == Log::Yellow);
    REQUIRE(Log::SeverityColor(LogSeverity::Error) == Log::Red);
}

TEST_CASE("SeverityTag maps each severity to its printed tag", "[Log]")
{
    REQUIRE(Log::SeverityTag(LogSeverity::Info).empty());
    REQUIRE(Log::SeverityTag(LogSeverity::Warning) == "[WARNING]");
    REQUIRE(Log::SeverityTag(LogSeverity::Error) == "[ERROR]");
}

TEST_CASE("LogMsg below g_MinSeverity produces no output", "[Log]")
{
    ScopedMinSeverity guard(LogSeverity::Warning);

    std::string output = CaptureStream(
        stdout, [&] { LogMsg(LogSeverity::Info, TestCategory, "should not appear"); });

    REQUIRE(output.empty());
}

TEST_CASE("LogMsg at or above g_MinSeverity produces output", "[Log]")
{
    ScopedMinSeverity guard(LogSeverity::Warning);

    std::string output = CaptureStream(
        stderr, [&] { LogMsg(LogSeverity::Warning, TestCategory, "should appear"); });

    REQUIRE_FALSE(output.empty());
    REQUIRE(output.find("should appear") != std::string::npos);
    REQUIRE(output.find("[WARNING]") != std::string::npos);
    REQUIRE(output.find("[Test]") != std::string::npos);
}

TEST_CASE("LogMsg forwards format arguments into the formatted message", "[Log]")
{
    ScopedMinSeverity guard(LogSeverity::Info);

    std::string output = CaptureStream(
        stdout, [&] { LogMsg(LogSeverity::Info, TestCategory, "value is {} and {}", 42, "abc"); });

    REQUIRE(output.find("value is 42 and abc") != std::string::npos);
}

TEST_CASE("Info severity logs go to stdout, not stderr", "[Log]")
{
    ScopedMinSeverity guard(LogSeverity::Info);

    std::string stderrOutput =
        CaptureStream(stderr, [&] { LogMsg(LogSeverity::Info, TestCategory, "info message"); });

    REQUIRE(stderrOutput.empty());
}
