#include <filesystem>
#include <fstream>
#include <iostream>
#include <optional>
#include <sstream>
#include <string>

#include <asset/ImageReader.h>
#include <core/Extent2D.h>
#include <platform/CommandLine.h>

#include "ImageCompare.h"
#include "ReportCompare.h"

using namespace Hikari;

namespace
{
/**
 * Strongest first: no verdict, then moved, then skipped, then matched. A
 * difference in a signal that *was* compared is real whatever happened to the
 * others, which is why moved outranks skipped; and a pair that cannot be
 * compared at all outranks both, because nothing has been established.
 */
constexpr int kMatched = 0;
constexpr int kMoved = 1;
constexpr int kSkipped = 2;
constexpr int kNoVerdict = 3;

/** How the exit codes rank, which is not their numeric order. */
int Severity(int code)
{
    switch (code)
    {
        case kNoVerdict:
            return 3;
        case kMoved:
            return 2;
        case kSkipped:
            return 1;
        default:
            return 0;
    }
}

int Stronger(int a, int b)
{
    return Severity(a) >= Severity(b) ? a : b;
}

int ToExitCode(TestSupport::ReportOutcome outcome)
{
    switch (outcome)
    {
        case TestSupport::ReportOutcome::Matched:
            return kMatched;
        case TestSupport::ReportOutcome::Skipped:
            return kSkipped;
        case TestSupport::ReportOutcome::Moved:
            return kMoved;
        case TestSupport::ReportOutcome::NoVerdict:
            break;
    }

    return kNoVerdict;
}

struct Options
{
    std::string ActualReport;
    std::string ExpectedReport;
    std::string ActualImage;
    std::string ExpectedImage;
    std::string DiffPrefix;
    bool bUpdate = false;
    bool bHelp = false;
};

void PrintUsage()
{
    std::cout << R"(HikariCompare — compares two runs of the engine.

  --actual-report <path>     the run being judged
  --expected-report <path>   what it is judged against, usually tests/baseline/
  --actual-image <path>      the capture from the same run, optional
  --expected-image <path>    the capture it is judged against
  --diff-prefix <prefix>     where failure images go; defaults to beside the
                             actual image, named comparison_actual.png and so on
  --update                   promote the actual run into the expected files,
                             refusing when the two runs' conditions differ

Exit codes, strongest first:
  3  no verdict — a report would not read, a field is missing or unclassified
  1  a compared signal moved
  2  nothing moved, but a signal could not be compared
  0  everything compared, and it matched
)";
}

Options ParseArgs(int argc, char** argv)
{
    Options options;

    // Named rather than a temporary: Options() hands back a reference into the
    // CommandLine, which a temporary would destroy before the loop ran.
    const Platform::CommandLine commandLine(argc, argv);
    for (const Platform::CommandLineOption& option : commandLine.Options())
    {
        const std::string& flag = option.Flag;
        if (flag == "--help" || flag == "-h")
        {
            option.RequireNoValue();
            options.bHelp = true;
        }
        else if (flag == "--actual-report")
            options.ActualReport = option.RequireValue();
        else if (flag == "--expected-report")
            options.ExpectedReport = option.RequireValue();
        else if (flag == "--actual-image")
            options.ActualImage = option.RequireValue();
        else if (flag == "--expected-image")
            options.ExpectedImage = option.RequireValue();
        else if (flag == "--diff-prefix")
            options.DiffPrefix = option.RequireValue();
        else if (flag == "--update")
        {
            option.RequireNoValue();
            options.bUpdate = true;
        }
        else
            throw Platform::CommandLineError("Unknown option: " + flag);
    }

    return options;
}

std::optional<std::string> ReadFile(const std::string& path)
{
    std::ifstream file(path);
    if (!file.is_open())
        return std::nullopt;

    std::ostringstream contents;
    contents << file.rdbuf();
    return contents.str();
}

/** Copies over the expected file, reporting what happened rather than throwing. */
bool Promote(const std::string& from, const std::string& to)
{
    std::error_code ec;
    std::filesystem::copy_file(from, to, std::filesystem::copy_options::overwrite_existing, ec);
    if (ec)
    {
        std::cerr << "could not replace " << to << ": " << ec.message() << "\n";
        return false;
    }

    std::cout << "promoted " << from << " -> " << to << "\n";
    return true;
}
} // namespace

int main(int argc, char** argv)
{
    Options options;
    try
    {
        options = ParseArgs(argc, argv);
    }
    catch (const Platform::CommandLineError& e)
    {
        std::cerr << e.what() << "\n\n";
        PrintUsage();
        return kNoVerdict;
    }

    if (options.bHelp)
    {
        PrintUsage();
        return kMatched;
    }

    if (options.ActualReport.empty() || options.ExpectedReport.empty())
    {
        std::cerr << "--actual-report and --expected-report are both required\n\n";
        PrintUsage();
        return kNoVerdict;
    }

    if (options.ActualImage.empty() != options.ExpectedImage.empty())
    {
        std::cerr << "--actual-image and --expected-image go together\n";
        return kNoVerdict;
    }

    const std::optional<std::string> actualJson = ReadFile(options.ActualReport);
    const std::optional<std::string> expectedJson = ReadFile(options.ExpectedReport);
    if (!actualJson || !expectedJson)
    {
        std::cerr << "could not read "
                  << (actualJson ? options.ExpectedReport : options.ActualReport) << "\n";
        return kNoVerdict;
    }

    const TestSupport::ReportComparison report =
        TestSupport::CompareReports(*actualJson, *expectedJson);
    std::cout << TestSupport::Describe(report) << "\n";

    int code = ToExitCode(report.Outcome);

    std::optional<TestSupport::ImageComparison> pixels;
    if (!options.ActualImage.empty())
    {
        const std::optional<Asset::Image> actual = Asset::ReadPng(options.ActualImage);
        const std::optional<Asset::Image> expected = Asset::ReadPng(options.ExpectedImage);
        if (!actual || !expected)
            return kNoVerdict;

        if (!report.bComparePixels)
        {
            // The skip is already named above, with the field that caused it.
            std::cout << "pixels: not compared\n";
        }
        else
        {
            pixels = TestSupport::CompareImages(actual->Pixels, actual->Extent, expected->Pixels,
                                                expected->Extent, report.PixelTolerance);
            std::cout << "pixels: " << TestSupport::Describe(*pixels) << "\n";

            if (!pixels->bComparable)
                code = Stronger(code, kNoVerdict);
            else if (!pixels->bWithinTolerance)
                code = Stronger(code, kMoved);

            if (!pixels->bWithinTolerance)
            {
                const std::string prefix =
                    options.DiffPrefix.empty()
                        ? (std::filesystem::path(options.ActualImage).parent_path() / "comparison_")
                              .string()
                        : options.DiffPrefix;

                if (TestSupport::WriteComparisonImages(actual->Pixels, actual->Extent,
                                                       expected->Pixels, expected->Extent,
                                                       report.PixelTolerance, prefix))
                {
                    std::cout << "comparison images written with prefix " << prefix << "\n";
                }
            }
        }
    }

    if (!options.bUpdate)
    {
        // Nothing here can know whether a difference was intended, so it says
        // what moved and asks rather than suggesting the baseline is stale.
        if (code == kMoved)
            std::cout << "\nIf that was expected, re-run with --update to promote the baseline.\n";

        return code;
    }

    // The guard that stops a baseline quietly moving to another machine or
    // another configuration. A missing field is the one difference --update
    // exists to absorb, so it is not a refusal; everything else is.
    if (!report.Skips.empty())
    {
        std::cerr << "\nrefusing to update: the run's conditions differ from the baseline's\n";
        return kNoVerdict;
    }

    if (!report.Problems.empty() || (pixels && !pixels->bComparable))
    {
        std::cerr << "\nrefusing to update: the comparison did not establish anything\n";
        return kNoVerdict;
    }

    const bool bNothingMoved = report.Differences.empty() && report.MissingFields.empty() &&
                               (!pixels || pixels->bWithinTolerance);
    if (bNothingMoved)
    {
        // PNG encoding is not reproducible, so rewriting an identical capture
        // still puts a binary diff into git.
        std::cout << "\nnothing moved; the baseline is left alone\n";
        return code;
    }

    std::cout << "\n";
    bool bPromoted = Promote(options.ActualReport, options.ExpectedReport);
    if (!options.ActualImage.empty())
        bPromoted = Promote(options.ActualImage, options.ExpectedImage) && bPromoted;

    return bPromoted ? kMatched : kNoVerdict;
}
