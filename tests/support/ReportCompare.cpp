#include "ReportCompare.h"

#include <array>
#include <format>
#include <map>

#include <nlohmann/json.hpp>

namespace TestSupport
{

namespace
{
using Json = nlohmann::json;

/**
 * Every field the run report emits, and what a difference in each one means.
 *
 * The three kinds of entry are argued rather than assumed. A gate is claimed
 * only where something in the code shows the field changes the signal; a
 * never-gate only where gating would excuse the defect the comparison exists to
 * find; and everything else gates, which is the safe direction — the planned
 * comparisons differ only in fields that are classified either way, so caution
 * costs nothing where it is actually used.
 */
constexpr std::array kFields = {
    // When the run happened. A measurement of the clock rather than a condition:
    // every run differs in it, so gating on it would skip every signal of every
    // comparison, and comparing it would fail every one.
    FieldClassification{"startedAt", FieldRole::Measured},

    // The frame count is both a condition and a result: counters.frame
    // describes the last frame drawn and the capture shows it, so two runs of
    // different lengths describe different frames.
    FieldClassification{"frames", FieldRole::Condition, true, true},

    // The expectations. Statements about what the renderer decided, not about
    // what the rasterizer produced, so they must match exactly — across
    // backends too, where a disagreement is a bug in one of them (plan D26).
    FieldClassification{"counters.frame.drawCalls", FieldRole::Compared},
    FieldClassification{"counters.frame.batches", FieldRole::Compared},
    FieldClassification{"counters.frame.instances", FieldRole::Compared},
    FieldClassification{"counters.frame.barriers", FieldRole::Compared},
    FieldClassification{"counters.frame.barrierCalls", FieldRole::Compared},
    FieldClassification{"counters.run.validationErrors", FieldRole::Compared},
    FieldClassification{"counters.run.validationWarnings", FieldRole::Compared},
    FieldClassification{"counters.run.uploadSubmissions", FieldRole::Compared},

    // Measurements. They vary with the machine by design, so they are read for
    // drift rather than diffed.
    FieldClassification{"timings.startupMs", FieldRole::Measured},
    FieldClassification{"timings.firstFrame.frameMs", FieldRole::Measured},
    FieldClassification{"timings.firstFrame.cpuMs", FieldRole::Measured},
    FieldClassification{"timings.frameMs.mean", FieldRole::Measured},
    FieldClassification{"timings.frameMs.p99", FieldRole::Measured},
    FieldClassification{"timings.frameMs.min", FieldRole::Measured},
    FieldClassification{"timings.frameMs.max", FieldRole::Measured},
    FieldClassification{"timings.cpuMs.mean", FieldRole::Measured},
    FieldClassification{"timings.cpuMs.p99", FieldRole::Measured},
    FieldClassification{"timings.cpuMs.min", FieldRole::Measured},
    FieldClassification{"timings.cpuMs.max", FieldRole::Measured},

    // A variable timestep advances animation by whatever the frame took, so the
    // frame a capture shows is a different moment. Unknown for counters, and
    // therefore gating.
    FieldClassification{"run.fixedDt", FieldRole::Condition, true, true},

    // Never gates pixels: a headless capture and an editor one of the same
    // frame were verified pixel-identical at step 46, which is what the headless
    // scene tests rest on. It does gate counters, because an offscreen target
    // and a swapchain reach the capture through different layout transitions,
    // and barriers are counted.
    FieldClassification{"run.headless", FieldRole::Condition, true, false},

    // Never gates counters: the UI pass records its barrier and its rendering
    // scope whether or not the panel draws, so the counters describe the frame
    // that ships either way. It plainly gates pixels — the panel is a fifth of
    // the frame.
    FieldClassification{"run.noUi", FieldRole::Condition, false, true},

    // The extent. Different pixels, obviously; unknown for counters.
    FieldClassification{"run.width", FieldRole::Condition, true, true},
    FieldClassification{"run.height", FieldRole::Condition, true, true},

    // Never gates anything. The job count changes who does the work, not what
    // the work is, so a difference in a signal across two job counts is a race
    // in whatever submitted the jobs — exactly what a comparison should report
    // rather than excuse.
    FieldClassification{"run.jobCount", FieldRole::Condition, false, false},

    // Unknown, and therefore gating: the present mode decides how the frame loop
    // is throttled, and nothing here has established that it leaves the recorded
    // barriers alone.
    FieldClassification{"run.presentMode", FieldRole::Condition, true, true},

    // Gates counters: validation is compiled in on NDEBUG, and a build that
    // never ran it reports zero errors trivially. Unknown for pixels, so it
    // gates those too.
    FieldClassification{"run.buildConfig", FieldRole::Condition, true, true},

    // What answered, rather than what was asked. **None of these may ever gate
    // the counters**, and that is a requirement rather than an observation:
    // counters are statements about what the renderer decided, so two backends
    // or two machines disagreeing about a draw call count is a bug in one of
    // them, always (plan D26). Gating them here would excuse exactly the defect
    // a cross-backend comparison exists to find.
    //
    // They do gate pixels, and for the opposite reason: two rasterizers differ
    // in the low bits by design, and so do two GPUs on one API. Within Stage 7.6
    // that makes a cross-machine or cross-backend pair simply not comparable on
    // pixels; Stage 7.7 is where a differing backend selects D26's tolerance
    // instead of skipping, once there are two backends to measure between.
    FieldClassification{"system.backend", FieldRole::Condition, false, true},
    FieldClassification{"system.gpu", FieldRole::Condition, false, true},
    FieldClassification{"system.driver", FieldRole::Condition, false, true},
    FieldClassification{"system.apiVersion", FieldRole::Condition, false, true},
    FieldClassification{"system.os", FieldRole::Condition, false, true},
    FieldClassification{"system.arch", FieldRole::Condition, false, true},
};

/** Reduces a JSON document to leaf paths, "counters.frame.drawCalls" style. */
void Flatten(const Json& node, const std::string& prefix, std::map<std::string, Json>& out)
{
    if (node.is_object())
    {
        for (const auto& [key, value] : node.items())
            Flatten(value, prefix.empty() ? key : prefix + "." + key, out);
    }
    else if (node.is_array())
    {
        for (size_t i = 0u; i < node.size(); ++i)
            Flatten(node[i], std::format("{}[{}]", prefix, i), out);
    }
    else
    {
        out.emplace(prefix, node);
    }
}

const FieldClassification* Classify(const std::string& path)
{
    for (const FieldClassification& field : kFields)
    {
        if (field.Path == path)
            return &field;
    }

    return nullptr;
}

/** Parses, or records why it could not. */
bool Parse(std::string_view text, std::string_view which, Json& out,
           std::vector<std::string>& problems)
{
    try
    {
        out = Json::parse(text.begin(), text.end());
    }
    catch (const Json::exception& e)
    {
        problems.push_back(std::format("the {} report could not be read: {}", which, e.what()));
        return false;
    }

    if (!out.is_object())
    {
        problems.push_back(std::format("the {} report is not a JSON object", which));
        return false;
    }

    return true;
}
} // namespace

std::span<const FieldClassification> ClassifiedFields()
{
    return kFields;
}

std::vector<std::string> FieldPaths(std::string_view json)
{
    std::vector<std::string> problems;
    Json document;
    if (!Parse(json, "given", document, problems))
        return {};

    std::map<std::string, Json> leaves;
    Flatten(document, "", leaves);

    std::vector<std::string> paths;
    paths.reserve(leaves.size());
    for (const auto& [path, value] : leaves)
        paths.push_back(path);

    return paths;
}

ReportComparison CompareReports(std::string_view actualJson, std::string_view expectedJson)
{
    ReportComparison result;

    Json actualDoc;
    Json expectedDoc;
    if (!Parse(actualJson, "actual", actualDoc, result.Problems) ||
        !Parse(expectedJson, "expected", expectedDoc, result.Problems))
    {
        return result;
    }

    std::map<std::string, Json> actual;
    std::map<std::string, Json> expected;
    Flatten(actualDoc, "", actual);
    Flatten(expectedDoc, "", expected);

    // Every field either report carries has to be classified, or the table is
    // out of date and the comparison is quietly narrower than it looks. The
    // other direction — a table entry neither report carries — falls out of the
    // loop below as a missing field.
    for (const auto& [path, value] : actual)
    {
        if (Classify(path) == nullptr)
            result.Problems.push_back("unclassified field: " + path);
    }

    for (const auto& [path, value] : expected)
    {
        if (Classify(path) == nullptr && !actual.contains(path))
            result.Problems.push_back("unclassified field: " + path);
    }

    bool bCountersSkipped = false;
    bool bPixelsSkipped = false;

    for (const FieldClassification& field : kFields)
    {
        const std::string path(field.Path);
        const auto inActual = actual.find(path);
        const auto inExpected = expected.find(path);

        if (inActual == actual.end() || inExpected == expected.end())
        {
            // Treated as matching so that the rest of the comparison still
            // happens: a step that adds a field needs evidence that nothing
            // else moved, at the exact moment it is touching the code the
            // baseline protects.
            result.bProvisional = true;
            result.MissingFields.push_back(
                std::format("{} (absent from the {} report)", path,
                            inActual == actual.end() ? "actual" : "expected"));
            continue;
        }

        if (inActual->second == inExpected->second)
            continue;

        const std::string values =
            std::format("{} vs {}", inActual->second.dump(), inExpected->second.dump());

        switch (field.Role)
        {
            case FieldRole::Compared:
                result.Differences.push_back(std::format("{}: {}", path, values));
                break;

            case FieldRole::Measured:
                break;

            case FieldRole::Condition:
                if (field.bGatesCounters && !bCountersSkipped)
                {
                    bCountersSkipped = true;
                    result.Skips.push_back(std::format("counters: {} differs ({})", path, values));
                }

                if (field.bGatesPixels && !bPixelsSkipped)
                {
                    bPixelsSkipped = true;
                    result.Skips.push_back(std::format("pixels: {} differs ({})", path, values));
                }
                break;
        }
    }

    // A gated counter comparison is not a comparison, so anything it found is
    // withdrawn rather than reported: the reason those counters differ is the
    // condition that differed. Every Compared field lives under "counters", so
    // this withdraws exactly the signal that was skipped.
    if (bCountersSkipped)
        result.Differences.clear();

    result.bComparePixels = !bPixelsSkipped;

    if (!result.Problems.empty() || !result.MissingFields.empty())
        result.Outcome = ReportOutcome::NoVerdict;
    else if (!result.Differences.empty())
        result.Outcome = ReportOutcome::Moved;
    else if (!result.Skips.empty())
        result.Outcome = ReportOutcome::Skipped;
    else
        result.Outcome = ReportOutcome::Matched;

    return result;
}

std::string Describe(const ReportComparison& comparison)
{
    std::string text;
    switch (comparison.Outcome)
    {
        case ReportOutcome::Matched:
            text = "reports match";
            break;
        case ReportOutcome::Skipped:
            text = "nothing moved, but a signal was skipped";
            break;
        case ReportOutcome::Moved:
            text = "a compared signal moved";
            break;
        case ReportOutcome::NoVerdict:
            text = comparison.bProvisional ? "no verdict (provisional: a field is missing)"
                                           : "no verdict";
            break;
    }

    const auto append = [&text](std::string_view label, const std::vector<std::string>& lines)
    {
        for (const std::string& line : lines)
            text += std::format("\n  {} {}", label, line);
    };

    append("!", comparison.Problems);
    append("missing field:", comparison.MissingFields);
    append("moved:", comparison.Differences);
    append("skipped:", comparison.Skips);

    return text;
}

} // namespace TestSupport
