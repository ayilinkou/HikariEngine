#include <engine/RunReportJson.h>

#include <format>
#include <optional>
#include <sstream>

#include <rhi/Backend.h>
#include <rhi/RhiTypes.h>

namespace Hikari::Engine
{

namespace
{
/**
 * The present mode as a JSON value: a quoted name, or null where the target
 * does not present at all, which is what an offscreen run reports.
 */
std::string PresentModeJson(std::optional<Rhi::PresentMode> mode)
{
    if (!mode)
        return "null";

    switch (*mode)
    {
        case Rhi::PresentMode::Immediate:
            return "\"immediate\"";
        case Rhi::PresentMode::Mailbox:
            return "\"mailbox\"";
        case Rhi::PresentMode::Fifo:
            return "\"fifo\"";
        case Rhi::PresentMode::FifoRelaxed:
            return "\"fifo-relaxed\"";
    }

    return "null";
}
} // namespace

std::string ToJson(const RunReport& report)
{
    const auto stats = [](const TimingStats& s)
    {
        return std::format("{{ \"mean\": {:.4f}, \"p99\": {:.4f}, \"min\": {:.4f}, "
                           "\"max\": {:.4f} }}",
                           s.Mean, s.P99, s.Min, s.Max);
    };

    std::ostringstream out;
    out << "{\n"
        << "  \"startedAt\": \"" << report.StartedAt << "\",\n"
        << "  \"frames\": " << report.Frames << ",\n"
        << "  \"counters\": {\n"
        << "    \"frame\": {\n"
        << "      \"drawCalls\": " << report.Counters.Frame.DrawCalls << ",\n"
        << "      \"batches\": " << report.Counters.Frame.Batches << ",\n"
        << "      \"instances\": " << report.Counters.Frame.Instances << ",\n"
        << "      \"barriers\": " << report.Counters.Frame.Barriers << ",\n"
        << "      \"barrierCalls\": " << report.Counters.Frame.BarrierCalls << "\n"
        << "    },\n"
        << "    \"run\": {\n"
        << "      \"validationErrors\": " << report.Counters.Run.ValidationErrors << ",\n"
        << "      \"validationWarnings\": " << report.Counters.Run.ValidationWarnings << ",\n"
        << "      \"uploadSubmissions\": " << report.Counters.Run.UploadSubmissions << "\n"
        << "    }\n"
        << "  },\n"
        << "  \"timings\": {\n"
        << std::format("    \"startupMs\": {:.4f},\n", report.Timings.StartupMs)
        << std::format("    \"firstFrame\": {{ \"frameMs\": {:.4f}, \"cpuMs\": {:.4f} }},\n",
                       report.Timings.FirstFrame.FrameMs, report.Timings.FirstFrame.CpuMs)
        << "    \"frameMs\": " << stats(report.Timings.FrameMs) << ",\n"
        << "    \"cpuMs\": " << stats(report.Timings.CpuMs) << "\n"
        << "  },\n"
        << "  \"run\": {\n"
        << "    \"fixedDt\": " << (report.Run.bFixedDt ? "true" : "false") << ",\n"
        << "    \"headless\": " << (report.Run.bHeadless ? "true" : "false") << ",\n"
        << "    \"noUi\": " << (report.Run.bNoUi ? "true" : "false") << ",\n"
        << "    \"width\": " << report.Run.Width << ",\n"
        << "    \"height\": " << report.Run.Height << ",\n"
        << "    \"jobCount\": " << report.Run.JobCount << ",\n"
        << "    \"presentMode\": " << PresentModeJson(report.Run.PresentMode) << ",\n"
        << "    \"buildConfig\": \"" << report.Run.BuildConfig << "\"\n"
        << "  },\n"
        << "  \"system\": {\n"
        << "    \"backend\": \"" << Rhi::ToString(report.System.Backend) << "\",\n"
        << "    \"gpu\": \"" << report.System.Gpu << "\",\n"
        << "    \"driver\": \"" << report.System.Driver << "\",\n"
        << "    \"apiVersion\": \"" << report.System.ApiVersion << "\",\n"
        << "    \"os\": \"" << report.System.Os << "\",\n"
        << "    \"arch\": \"" << report.System.Arch << "\"\n"
        << "  }\n"
        << "}\n";

    return out.str();
}

} // namespace Hikari::Engine
