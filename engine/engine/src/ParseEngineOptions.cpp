#include <engine/ParseEngineOptions.h>

#include <algorithm>
#include <iostream>
#include <string>

#include <rhi/Backend.h>

#include <engine/CameraPresets.h>

namespace Hikari::Engine
{

namespace
{

/** What --scene loads when the flag is given with no value. */
constexpr const char* kDefaultScene = "scenes/test_scene.map";

/** What --frames means when the flag is given with no value. */
constexpr uint64_t kDefaultFrames = 1000u;

/** The backends this build has, spelled the way --backend accepts them. */
std::string AvailableBackendNames()
{
    std::string names;
    for (const Rhi::Backend backend : Rhi::AvailableBackends())
    {
        if (!names.empty())
            names += ", ";

        names += Rhi::ToString(backend);
    }

    return names;
}

} // namespace

bool ParseEngineOption(const Platform::CommandLineOption& option, RunSpec& spec,
                       EngineConfig& config)
{
    const std::string& flag = option.Flag;

    if (flag == "--scene")
        spec.ScenePath = option.Value.value_or(kDefaultScene);
    else if (flag == "--content")
        spec.ContentRoot = option.RequireValue();
    else if (flag == "--frames")
        spec.Frames = option.Value ? option.RequireUint64() : kDefaultFrames;
    else if (flag == "--fixed-dt")
    {
        option.RequireNoValue();
        spec.bFixedDt = true;
    }
    else if (flag == "--camera-preset")
        spec.CameraPreset = option.RequireInt();
    else if (flag == "--no-ui")
    {
        option.RequireNoValue();
        spec.bNoUi = true;
    }
    else if (flag == "--jobs")
        spec.JobCount = option.RequireInt();
    else if (flag == "--strict-validation")
    {
        option.RequireNoValue();
        spec.bStrictValidation = true;
    }
    else if (flag == "--backend")
    {
        const std::string value = option.RequireValue();
        const std::optional<Rhi::Backend> backend = Rhi::BackendFromString(value);

        // Refused here rather than at device creation, which also checks: this
        // runs before the window, the job system and the content root exist, it
        // comes out as the same kind of error as every other bad flag, and it is
        // the only one of the two that can list what this build does have. A run
        // that quietly measured another backend is worse than one that refused
        // (plan D25).
        const std::span<const Rhi::Backend> available = Rhi::AvailableBackends();
        if (!backend || std::ranges::find(available, *backend) == available.end())
        {
            throw Platform::CommandLineError("--backend: this build does not contain '" + value +
                                             "'. Available: " + AvailableBackendNames());
        }

        spec.Backend = *backend;
    }
    else if (flag == "--validation-policy")
    {
        const std::string value = option.RequireValue();
        const std::optional<Rhi::ValidationPolicy> policy = Rhi::ValidationPolicyFromString(value);
        if (!policy)
        {
            throw Platform::CommandLineError(
                "--validation-policy expects ignore, count or failfast, got: " + value);
        }

        spec.ValidationPolicy = *policy;
    }
    else if (flag == "--frames-in-flight")
    {
        const int value = option.RequireInt();
        if (value < 1)
            throw Platform::CommandLineError("--frames-in-flight expects a count of 1 or more, "
                                             "got: " +
                                             std::to_string(value));

        config.FramesInFlight = static_cast<uint32_t>(value);
    }
    else if (flag == "--vk-disable-extension")
        spec.DisabledVulkanExtensions.push_back(option.RequireValue());
    else if (flag == "--vk-force-single-queue")
    {
        option.RequireNoValue();
        spec.bForceSingleQueue = true;
    }
    else
        return false;

    return true;
}

void PrintEngineUsage()
{
    std::cout << "  --scene <path>          Load a scene (.map) on startup\n"
                 "  --content <dir>         Use <dir> as the content root\n"
                 "  --frames <N>            Exit automatically after N frames "
                 "(0 = run until closed)\n"
                 "  --fixed-dt              Use a fixed 1/60s timestep instead "
                 "of wall-clock time\n"
                 "  --camera-preset <N>     Use a hardcoded camera preset (0-" +
                     std::to_string(kNumCameraPresets - 1) +
                     ") instead of free camera\n"
                     "  --no-ui                 Suppress the editor panel. ImGui still "
                     "initialises and its\n"
                     "                          pass still runs, so only what is drawn "
                     "changes\n"
                     "  --jobs <N>              Worker thread count (0 = SerialJobSystem, "
                     "no threads; default = hardware_concurrency() - 1)\n"
                     "  --frames-in-flight <N>  Frames the CPU may work on at once "
                     "(default: 2)\n"
                     "  --strict-validation     Exit non-zero if any Vulkan "
                     "validation error occurred\n"
                     "  --backend <name>        Which backend to run on. Available in this "
                     "build: " +
                     AvailableBackendNames() +
                     "\n"
                     "  --validation-policy <p> ignore | count | failfast "
                     "(default: count; failfast aborts on the first error)\n"
                     "  --vk-disable-extension <name>\n"
                     "                          Vulkan only. Behave as though the device did not "
                     "support this\n"
                     "                          optional extension, to exercise the fallback path. "
                     "Repeatable.\n"
                     "  --vk-force-single-queue Vulkan only. Behave as though the device exposed "
                     "one queue\n"
                     "                          family, to exercise the path an integrated GPU "
                     "takes\n";
}

} // namespace Hikari::Engine
