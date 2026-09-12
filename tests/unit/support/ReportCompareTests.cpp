#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <string>
#include <string_view>
#include <vector>

#include <engine/RunReport.h>
#include <engine/RunReportJson.h>
#include <platform/IPlatform.h>

#include <rhi/Backend.h>
#include <rhi/RhiTypes.h>

#include "ReportCompare.h"

using namespace Hikari;
using TestSupport::ReportOutcome;
namespace Platform = Hikari::Platform;

namespace
{
/** A report whose every field carries a value nothing else would produce. */
Engine::RunReport MakeReport()
{
    Engine::RunReport report;
    report.StartedAt = "2026-09-12T17:51:29Z";
    report.Frames = 1000u;

    report.Counters.Frame.DrawCalls = 22u;
    report.Counters.Frame.Batches = 21u;
    report.Counters.Frame.Instances = 23u;
    report.Counters.Frame.Barriers = 14u;
    report.Counters.Frame.BarrierCalls = 9u;
    report.Counters.Run.ValidationErrors = 0u;
    report.Counters.Run.ValidationWarnings = 0u;
    report.Counters.Run.UploadSubmissions = 4u;

    report.Timings.StartupMs = 512.25f;
    report.Timings.FirstFrame.FrameMs = 33.5f;
    report.Timings.FirstFrame.CpuMs = 12.5f;
    report.Timings.FrameMs = Engine::TimingStats{.Mean = 8.f, .P99 = 9.f, .Min = 7.f, .Max = 11.f};
    report.Timings.CpuMs = Engine::TimingStats{.Mean = 4.f, .P99 = 5.f, .Min = 3.f, .Max = 6.f};

    report.Run.bFixedDt = true;
    report.Run.bHeadless = false;
    report.Run.bNoUi = true;
    report.Run.Width = 1920u;
    report.Run.Height = 1080u;
    report.Run.JobCount = 15u;
    report.Run.PresentMode = Rhi::PresentMode::Mailbox;
    report.Run.BuildConfig = "debug";
    report.Run.ScenePath = "scenes/test_scene.map";
    report.Run.CameraPreset = 1;
    report.Run.InputScriptPath = "";
    report.Run.CaptureFrame = 999u;
    report.Run.bValidationEnabled = true;
    report.Run.ValidationPolicy = Rhi::ValidationPolicy::Count;
    report.Run.bSyncValidation = true;
    report.Run.DisabledVulkanExtensions = {};
    report.Run.bForceSingleQueue = false;
    report.Run.FramesInFlight = 2u;
    report.Run.WindowMode = Platform::WindowMode::BorderlessFullscreen;

    report.System.Backend = Rhi::Backend::Vulkan;
    report.System.Gpu = "Test GPU 9000";
    report.System.Driver = "testdrv 1.2.3";
    report.System.ApiVersion = "1.4.354";
    report.System.Os = "Linux";
    report.System.Arch = "x86_64";

    return report;
}

std::string Json(const Engine::RunReport& report)
{
    return Engine::ToJson(report);
}

/** Drops the line carrying `needle`, which must not be the last of its object. */
std::string WithoutLine(std::string text, std::string_view needle)
{
    const size_t at = text.find(needle);
    REQUIRE(at != std::string::npos);

    const size_t begin = text.rfind('\n', at) + 1u;
    const size_t end = text.find('\n', at) + 1u;
    text.erase(begin, end - begin);
    return text;
}

/** Adds a field the table has never heard of, keeping the JSON valid. */
std::string WithExtraField(std::string text)
{
    const size_t at = text.find("  \"frames\"");
    REQUIRE(at != std::string::npos);
    text.insert(at, "  \"somethingNew\": 7,\n");
    return text;
}

bool Mentions(const std::vector<std::string>& lines, std::string_view needle)
{
    return std::ranges::any_of(lines, [needle](const std::string& line)
                               { return line.find(needle) != std::string::npos; });
}
} // namespace

TEST_CASE("Every field the report emits is classified", "[support][report]")
{
    const std::vector<std::string> paths = TestSupport::FieldPaths(Json(MakeReport()));
    REQUIRE_FALSE(paths.empty());

    const std::span<const TestSupport::FieldClassification> table = TestSupport::ClassifiedFields();

    for (const std::string& path : paths)
    {
        INFO("emitted field: " << path);
        CHECK(std::ranges::any_of(table, [&path](const TestSupport::FieldClassification& field)
                                  { return field.Path == path; }));
    }
}

TEST_CASE("Every classified field is one the report emits", "[support][report]")
{
    // The other direction, which catches an entry left behind when a field is
    // renamed or removed: without it the table would keep a row that can never
    // match, and every comparison would be provisional forever.
    const std::vector<std::string> paths = TestSupport::FieldPaths(Json(MakeReport()));

    for (const TestSupport::FieldClassification& field : TestSupport::ClassifiedFields())
    {
        INFO("classified field: " << field.Path);
        CHECK(std::ranges::find(paths, field.Path) != paths.end());
    }
}

TEST_CASE("A report compared against itself matches", "[support][report]")
{
    const std::string json = Json(MakeReport());
    const TestSupport::ReportComparison result = TestSupport::CompareReports(json, json);

    CHECK(result.Outcome == ReportOutcome::Matched);
    CHECK(result.Problems.empty());
    CHECK(result.Differences.empty());
    CHECK(result.Skips.empty());
    CHECK_FALSE(result.bProvisional);
    CHECK(result.bComparePixels);
    CHECK(result.PixelTolerance.MaxChannelDelta == 0u);
    CHECK(result.PixelTolerance.MaxDifferingFraction == 0.0);
}

TEST_CASE("A counter that moved is reported with both values", "[support][report]")
{
    Engine::RunReport moved = MakeReport();
    moved.Counters.Frame.DrawCalls = 23u;

    const TestSupport::ReportComparison result =
        TestSupport::CompareReports(Json(moved), Json(MakeReport()));

    CHECK(result.Outcome == ReportOutcome::Moved);
    REQUIRE(result.Differences.size() == 1u);
    CHECK(result.Differences[0] == "counters.frame.drawCalls: 23 vs 22");
    CHECK(result.bComparePixels);
}

TEST_CASE("Timings never move a comparison", "[support][report]")
{
    Engine::RunReport slower = MakeReport();
    slower.Timings.StartupMs = 2048.f;
    slower.Timings.FrameMs.Mean = 99.f;
    slower.Timings.CpuMs.Max = 77.f;

    const TestSupport::ReportComparison result =
        TestSupport::CompareReports(Json(slower), Json(MakeReport()));

    CHECK(result.Outcome == ReportOutcome::Matched);
    CHECK(result.Differences.empty());
}

TEST_CASE("A condition that gates a signal skips it and names itself", "[support][report]")
{
    Engine::RunReport other = MakeReport();
    other.Run.BuildConfig = "release";

    const TestSupport::ReportComparison result =
        TestSupport::CompareReports(Json(other), Json(MakeReport()));

    CHECK(result.Outcome == ReportOutcome::Skipped);
    CHECK(Mentions(result.Skips, "counters: run.buildConfig differs"));
    CHECK(Mentions(result.Skips, "pixels: run.buildConfig differs"));
    CHECK_FALSE(result.bComparePixels);
}

TEST_CASE("A skipped counter comparison withdraws what it found", "[support][report]")
{
    // The point of gating: a release build reports zero validation errors
    // trivially, so a difference in the counters between two build
    // configurations says nothing about the renderer.
    Engine::RunReport other = MakeReport();
    other.Run.BuildConfig = "release";
    other.Counters.Frame.DrawCalls = 99u;

    const TestSupport::ReportComparison result =
        TestSupport::CompareReports(Json(other), Json(MakeReport()));

    CHECK(result.Outcome == ReportOutcome::Skipped);
    CHECK(result.Differences.empty());
}

TEST_CASE("A field that gates only pixels leaves the counters compared", "[support][report]")
{
    Engine::RunReport other = MakeReport();
    other.Run.bNoUi = false;
    other.Counters.Frame.Batches = 44u;

    const TestSupport::ReportComparison result =
        TestSupport::CompareReports(Json(other), Json(MakeReport()));

    // Moved outranks skipped: the counters were genuinely compared, and they
    // differ, which is true whatever happened to the pixels.
    CHECK(result.Outcome == ReportOutcome::Moved);
    CHECK(Mentions(result.Differences, "counters.frame.batches"));
    CHECK(Mentions(result.Skips, "pixels: run.noUi differs"));
    CHECK_FALSE(result.bComparePixels);
}

TEST_CASE("Fields that must never gate do not", "[support][report]")
{
    SECTION("the job count gates nothing, because a difference there is a race")
    {
        Engine::RunReport other = MakeReport();
        other.Run.JobCount = 1u;

        const TestSupport::ReportComparison result =
            TestSupport::CompareReports(Json(other), Json(MakeReport()));

        CHECK(result.Outcome == ReportOutcome::Matched);
        CHECK(result.Skips.empty());
        CHECK(result.bComparePixels);
    }

    SECTION("headless never gates pixels, which step 46 verified")
    {
        Engine::RunReport other = MakeReport();
        other.Run.bHeadless = true;

        const TestSupport::ReportComparison result =
            TestSupport::CompareReports(Json(other), Json(MakeReport()));

        CHECK(result.bComparePixels);
        CHECK(Mentions(result.Skips, "counters: run.headless differs"));
        CHECK_FALSE(Mentions(result.Skips, "pixels:"));
    }
}

TEST_CASE("A missing field is provisional and still compares the rest", "[support][report]")
{
    Engine::RunReport moved = MakeReport();
    moved.Counters.Frame.Instances = 42u;

    const std::string withoutJobCount = WithoutLine(Json(MakeReport()), "\"jobCount\"");
    const TestSupport::ReportComparison result =
        TestSupport::CompareReports(Json(moved), withoutJobCount);

    CHECK(result.Outcome == ReportOutcome::NoVerdict);
    CHECK(result.bProvisional);
    CHECK(result.Problems.empty());
    CHECK(Mentions(result.MissingFields, "run.jobCount"));

    // The evidence a field-adding step needs: everything else was still looked
    // at, so "nothing moved" is a claim about the rest rather than a shrug.
    CHECK(Mentions(result.Differences, "counters.frame.instances: 42 vs 23"));
}

TEST_CASE("An unclassified field is a hard failure", "[support][report]")
{
    const std::string json = Json(MakeReport());
    const TestSupport::ReportComparison result =
        TestSupport::CompareReports(WithExtraField(json), json);

    CHECK(result.Outcome == ReportOutcome::NoVerdict);
    CHECK_FALSE(result.bProvisional);
    CHECK(Mentions(result.Problems, "unclassified field: somethingNew"));
}

TEST_CASE("An unclassified field is named once, not once per report", "[support][report]")
{
    const std::string json = WithExtraField(Json(MakeReport()));
    const TestSupport::ReportComparison result = TestSupport::CompareReports(json, json);

    CHECK(result.Problems.size() == 1u);
}

TEST_CASE("A report that will not parse gives no verdict", "[support][report]")
{
    const std::string json = Json(MakeReport());

    SECTION("malformed text")
    {
        const TestSupport::ReportComparison result =
            TestSupport::CompareReports("{ not json", json);

        CHECK(result.Outcome == ReportOutcome::NoVerdict);
        CHECK(Mentions(result.Problems, "the actual report could not be read"));
    }

    SECTION("valid JSON that is not an object")
    {
        const TestSupport::ReportComparison result = TestSupport::CompareReports(json, "[1, 2, 3]");

        CHECK(result.Outcome == ReportOutcome::NoVerdict);
        CHECK(Mentions(result.Problems, "the expected report is not a JSON object"));
    }
}

TEST_CASE("Describe names what the comparison established", "[support][report]")
{
    Engine::RunReport moved = MakeReport();
    moved.Counters.Run.UploadSubmissions = 40u;

    const std::string text =
        TestSupport::Describe(TestSupport::CompareReports(Json(moved), Json(MakeReport())));

    CHECK(text.find("a compared signal moved") != std::string::npos);
    CHECK(text.find("counters.run.uploadSubmissions: 40 vs 4") != std::string::npos);
}

TEST_CASE("What answered never gates the counters", "[support][report]")
{
    // The independence D26 requires: gating here would excuse the defect a
    // cross-machine or cross-backend comparison exists to find, since a draw
    // call count is a statement about what the renderer decided.
    Engine::RunReport other = MakeReport();
    other.System.Gpu = "Some Other GPU";
    other.Counters.Frame.DrawCalls = 99u;

    const TestSupport::ReportComparison result =
        TestSupport::CompareReports(Json(other), Json(MakeReport()));

    CHECK(result.Outcome == ReportOutcome::Moved);
    CHECK(Mentions(result.Differences, "counters.frame.drawCalls: 99 vs 22"));

    // Pixels are a different matter: two GPUs differ in the low bits by design.
    CHECK(Mentions(result.Skips, "pixels: system.gpu differs"));
    CHECK_FALSE(result.bComparePixels);
}

TEST_CASE("A differing backend skips pixels and not counters", "[support][report]")
{
    Engine::RunReport other = MakeReport();
    other.System.Backend = Rhi::Backend::D3D12;

    const TestSupport::ReportComparison result =
        TestSupport::CompareReports(Json(other), Json(MakeReport()));

    CHECK(result.Outcome == ReportOutcome::Skipped);
    CHECK(Mentions(result.Skips, "pixels: system.backend differs"));
    CHECK_FALSE(Mentions(result.Skips, "counters:"));
}

TEST_CASE("A baseline without the system block is provisional, not a failure", "[support][report]")
{
    // What step 6 itself produces: the committed baseline predates the block, so
    // every field of it is missing. The comparison still looks at everything
    // else, which is the evidence that promoting the baseline is safe.
    std::string older = Json(MakeReport());
    for (const char* field :
         {"\"backend\"", "\"gpu\"", "\"driver\"", "\"apiVersion\"", "\"os\"", "\"arch\""})
    {
        older = WithoutLine(std::move(older), field);
    }

    // Removing every member leaves "system": {}, which is still valid JSON.
    const TestSupport::ReportComparison result =
        TestSupport::CompareReports(Json(MakeReport()), older);

    CHECK(result.Outcome == ReportOutcome::NoVerdict);
    CHECK(result.bProvisional);
    CHECK(result.Problems.empty());
    CHECK(result.MissingFields.size() == 6u);
    CHECK(result.Differences.empty());
}

TEST_CASE("Two runs at different times still match", "[support][report]")
{
    // startedAt is a measurement of the clock, not a condition: every run
    // differs in it, so gating on it would skip every signal of every
    // comparison and comparing it would fail every one.
    Engine::RunReport later = MakeReport();
    later.StartedAt = "2027-01-01T00:00:00Z";

    const TestSupport::ReportComparison result =
        TestSupport::CompareReports(Json(later), Json(MakeReport()));

    CHECK(result.Outcome == ReportOutcome::Matched);
    CHECK(result.Differences.empty());
    CHECK(result.Skips.empty());
    CHECK(result.bComparePixels);
}

TEST_CASE("A different scene skips everything and says which field did it", "[support][report]")
{
    Engine::RunReport other = MakeReport();
    other.Run.ScenePath = "scenes/other.map";

    const TestSupport::ReportComparison result =
        TestSupport::CompareReports(Json(other), Json(MakeReport()));

    CHECK(result.Outcome == ReportOutcome::Skipped);
    CHECK(Mentions(result.Skips, "counters: run.scene differs"));
    CHECK(Mentions(result.Skips, "pixels: run.scene differs"));
}

TEST_CASE("A run with no scene and no script records null rather than empty", "[support][report]")
{
    // Absent has to be distinguishable from a scene literally named "": a report
    // that says "" would compare equal to one that ran nothing.
    Engine::RunReport bare = MakeReport();
    bare.Run.ScenePath.clear();
    bare.Run.CaptureFrame.reset();
    bare.Run.WindowMode.reset();

    const std::string json = Json(bare);
    CHECK(json.find("\"scene\": null") != std::string::npos);
    CHECK(json.find("\"inputScript\": null") != std::string::npos);
    CHECK(json.find("\"captureFrame\": null") != std::string::npos);
    CHECK(json.find("\"windowMode\": null") != std::string::npos);

    // And it still parses and classifies: null is a value, not a missing field.
    const TestSupport::ReportComparison result = TestSupport::CompareReports(json, json);
    CHECK(result.Outcome == ReportOutcome::Matched);
}

TEST_CASE("A disabled extension list survives the round trip", "[support][report]")
{
    Engine::RunReport other = MakeReport();
    other.Run.DisabledVulkanExtensions = {"VK_KHR_maintenance9", "VK_EXT_descriptor_indexing"};

    const TestSupport::ReportComparison result =
        TestSupport::CompareReports(Json(other), Json(MakeReport()));

    CHECK(result.Outcome == ReportOutcome::Skipped);
    CHECK(Mentions(result.Skips, "run.vkDisabledExtensions differs"));
}
