#pragma once

#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "ImageCompare.h"

namespace TestSupport
{

/**
 * What a comparison of two run reports is allowed to say.
 *
 * Ordered by nothing in particular; the tool's exit codes rank them, strongest
 * first — no verdict, then moved, then skipped, then matched. A difference in a
 * signal that *was* compared is real whatever happened to the others, which is
 * why moved outranks skipped.
 */
enum class ReportOutcome : uint8_t
{
    /** Everything the table knows was compared, and all of it matched. */
    Matched,

    /** Nothing moved, but a signal could not be compared at all. */
    Skipped,

    /** A compared signal differs. */
    Moved,

    /**
     * No comparison could be made: a report would not parse, a field is absent
     * from one of them, or a field is present that the table does not classify.
     */
    NoVerdict,
};

/** What a field means to a comparison. */
enum class FieldRole : uint8_t
{
    /** An expectation. The two reports must agree on it exactly. */
    Compared,

    /**
     * A measurement that varies with the machine, so comparing it would only
     * ever produce noise. Classified rather than ignored, because "the table
     * does not mention it" has to stay a failure.
     */
    Measured,

    /**
     * A condition the numbers were measured under. A difference in one does not
     * fail the comparison; it invalidates whichever signals it gates, which are
     * then skipped and named.
     */
    Condition,
};

/**
 * One field of the run report, and what a difference in it means.
 *
 * A field gates a signal when a difference in it would change that signal for a
 * legitimate reason — comparing anyway would report a flag change as a code
 * regression. A field must *not* gate a signal when gating would excuse the
 * very defect the comparison exists to find. Anything not shown to be one or
 * the other gates, because caution costs a skip and the alternative costs a
 * missed regression.
 */
struct FieldClassification
{
    std::string_view Path;
    FieldRole Role = FieldRole::Condition;
    bool bGatesCounters = false;
    bool bGatesPixels = false;
};

/** Every field the run report emits, for the comparison and for the test that pins it. */
[[nodiscard]] std::span<const FieldClassification> ClassifiedFields();

/**
 * Every leaf path in a report, "counters.frame.drawCalls" style. Returns nothing
 * if the text will not parse.
 *
 * Exposed so that the test pinning the table can ask a real report what it
 * emits, instead of repeating the field names and agreeing with itself.
 */
[[nodiscard]] std::vector<std::string> FieldPaths(std::string_view json);

/** What comparing two reports established, and what it could not. */
struct ReportComparison
{
    ReportOutcome Outcome = ReportOutcome::NoVerdict;

    /**
     * True when a field the table knows is absent from a report. The absent
     * fields are treated as matching and the rest is compared anyway, so a step
     * that adds a field still gets evidence that nothing else moved — but the
     * result is labelled and stays a no-verdict, because a field nobody looked
     * at is not a field that matched.
     */
    bool bProvisional = false;

    /**
     * Whether the captures are worth comparing, and how strictly. False when a
     * field gating pixels differs. The tolerance is exact today and will stay
     * exact within one backend; it is chosen from what the reports say rather
     * than from a flag, so there is nothing to nudge when a comparison goes red.
     */
    bool bComparePixels = false;
    ImageTolerance PixelTolerance;

    /** Why there is no verdict: a parse failure, a missing or unclassified field. */
    std::vector<std::string> Problems;

    /** Each compared field that differs, with both values. */
    std::vector<std::string> Differences;

    /** Each skipped signal, naming the field that caused it and both its values. */
    std::vector<std::string> Skips;
};

/**
 * Compares two run reports given as JSON text.
 *
 * Takes text rather than paths so that the rules are testable without a
 * filesystem; the caller reads the files.
 */
[[nodiscard]] ReportComparison CompareReports(std::string_view actualJson,
                                              std::string_view expectedJson);

/** Several lines describing what the comparison established, for a tool or a test. */
[[nodiscard]] std::string Describe(const ReportComparison& comparison);

} // namespace TestSupport
