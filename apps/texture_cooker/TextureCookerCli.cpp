#include "TextureCookerCli.h"

#include <cmath>
#include <cstdlib>
#include <exception>
#include <format>
#include <optional>
#include <ostream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

#include <asset/TextureCooker.h>
#include <platform/CommandLine.h>
#include <platform/Paths.h>

namespace Hikari::Tools
{

namespace
{
void PrintUsage(std::ostream& output)
{
    const auto option = [&](std::string_view flag, std::string_view description)
    { output << std::format("  {:<38}{}\n", flag, description); };
    output << "HikariTextureCooker\n\n"
              "Usage: HikariTextureCooker cook <source-relative or absolute file> [options]\n"
              "       HikariTextureCooker cook-all [operational options]\n\n"
              "Operational options:\n";
    option("--source-root <dir>", "Source root (default: HIKARI_CONTENT or content/)");
    option("--cooked-root <dir>", "Cooked root (default: source root's sibling cooked/)");
    option("--force", "Bypass incremental checks without resetting settings");
    option("--threads <1..128>", "CPU encoder thread limit (default: 8)");
    option("--dry-run", "Validate a single request and print paths without writes");
    option("--help", "Show help");
    output << "\nSingle-file options (saved settings otherwise stay in effect):\n";
    option("--usage <color|normal|scalar|packed>", "Texture usage (default: color)");
    option("--format <auto|BC1|BC4|BC5|BC7>", "Compression format (default: auto)");
    option("", "DDS also supports BC2, BC3, BC4_S and BC5_S");
    option("--color-space <auto|linear|srgb>", "Color space (default: auto)");
    option("--flip-normal-y", "Enable flipping; edit JSON to false to disable it");
    option("--alpha-mode <opaque|mask>", "Alpha interpretation");
    option("--alpha-cutoff <0..1>", "Mask cutoff (default: 0.5)");
    option("--no-mips", "Save mipmaps: false");
}

template <class T>
T RequireToken(const Platform::CommandLineOption& option,
               std::optional<T> (*parse)(std::string_view))
{
    const auto token = option.RequireValue();
    const auto value = parse(token);
    if (!value)
        throw Platform::CommandLineError(
            std::format("{}: Unsupported value '{}'", option.Flag, token));
    return *value;
}

std::filesystem::path RequirePath(const Platform::CommandLineOption& option)
{
    const auto value = option.RequireValue();
    if (value.empty())
        throw Platform::CommandLineError(std::format("{}: Required nonempty path", option.Flag));
    return value;
}

double RequireCutoff(const Platform::CommandLineOption& option)
{
    const auto value = option.RequireValue();
    try
    {
        size_t consumed = 0;
        const double cutoff = std::stod(value, &consumed);
        if (consumed != value.size() || !std::isfinite(cutoff) || cutoff < 0 || cutoff > 1)
            throw std::invalid_argument("Invalid cutoff");
        return cutoff;
    }
    catch (const std::exception&)
    {
        throw Platform::CommandLineError(
            std::format("{}: Expected a finite number in [0, 1]", option.Flag));
    }
}

void PrintPrepared(const Asset::PreparedTextureCook& prepared, std::ostream& output)
{
    output << "Source: " << prepared.Paths.SourcePath.string() << '\n'
           << "Settings: " << prepared.Paths.SettingsPath.string() << '\n'
           << "Cooked: " << prepared.Paths.CookedPath.string() << '\n'
           << "Record: " << prepared.Paths.RecordPath.string() << '\n'
           << "Usage: " << Asset::ToString(prepared.Settings.Authored.Usage) << '\n'
           << "Format: " << Asset::ToDisplayString(prepared.Settings.Format) << '\n'
           << "Color space: " << Asset::ToString(prepared.Settings.ColorSpace) << '\n'
           << "Encoder threads: " << prepared.Request.EncoderThreads << '\n'
           << "Force: " << (prepared.Request.bForce ? "true" : "false") << '\n';
    for (size_t i = 0; i < prepared.Inputs.size(); ++i)
        output << (prepared.Descriptor ? std::string(Asset::kCubemapFaceNames[i]) : "Image") << ": "
               << prepared.Inputs[i].string() << '\n';
}
} // namespace

int RunTextureCooker(int argc, const char* const* argv,
                     const std::filesystem::path& fallbackSourceRoot, std::ostream& output,
                     std::ostream& errors)
{
    namespace fs = std::filesystem;
    if (argc == 2 && (std::string_view(argv[1]) == "--help" || std::string_view(argv[1]) == "-h"))
    {
        PrintUsage(output);
        return 0;
    }
    try
    {
        if (argc < 2)
            throw Platform::CommandLineError("Expected cook or cook-all; use --help for usage");
        const std::string_view command = argv[1];
        const bool bBatch = command == "cook-all";
        if (command != "cook" && !bBatch)
            throw Platform::CommandLineError(
                std::format("Unknown command '{}'; expected cook or cook-all", command));

        Asset::TextureCookRequest request;
        std::vector<const char*> optionArgs{argv[0]};
        int optionsStart = 2;
        if (!bBatch && argc > 2 && !std::string_view(argv[2]).starts_with("--") &&
            std::string_view(argv[2]) != "-h")
        {
            request.Source = argv[2];
            optionsStart = 3;
        }
        for (int i = optionsStart; i < argc; ++i)
            optionArgs.push_back(argv[i]);
        const Platform::CommandLine commandLine(static_cast<int>(optionArgs.size()),
                                                optionArgs.data());
        fs::path sourceRoot, cookedRoot;
        bool bHelp = false, bDryRun = false;
        for (const auto& option : commandLine.Options())
        {
            const auto& flag = option.Flag;
            if (flag == "--help" || flag == "-h")
            {
                option.RequireNoValue();
                bHelp = true;
            }
            else if (flag == "--source-root")
                sourceRoot = RequirePath(option);
            else if (flag == "--cooked-root")
                cookedRoot = RequirePath(option);
            else if (flag == "--threads")
            {
                const auto count = option.RequireUint64();
                if (count < 1 || count > 128)
                    throw Platform::CommandLineError("--threads: Expected an integer in [1, 128]");
                request.EncoderThreads = static_cast<uint32_t>(count);
            }
            else if (flag == "--force")
            {
                option.RequireNoValue();
                request.bForce = true;
            }
            else if (flag == "--dry-run")
            {
                option.RequireNoValue();
                bDryRun = true;
            }
            else if (flag == "--usage")
                request.Overrides.Usage = RequireToken(option, Asset::ParseTextureUsage);
            else if (flag == "--format")
                request.Overrides.Format = RequireToken(option, Asset::ParseTextureCompression);
            else if (flag == "--color-space")
                request.Overrides.ColorSpace = RequireToken(option, Asset::ParseTextureColorSpace);
            else if (flag == "--alpha-mode")
                request.Overrides.AlphaMode = RequireToken(option, Asset::ParseTextureAlphaMode);
            else if (flag == "--alpha-cutoff")
                request.Overrides.AlphaCutoff = RequireCutoff(option);
            else if (flag == "--flip-normal-y")
            {
                option.RequireNoValue();
                request.Overrides.bFlipNormalY = true;
            }
            else if (flag == "--no-mips")
            {
                option.RequireNoValue();
                request.Overrides.bNoMips = true;
            }
            else
                throw Platform::CommandLineError(std::format("Unexpected argument '{}'", flag));
        }
        if (bHelp)
        {
            PrintUsage(output);
            return 0;
        }
        if (bBatch && !request.Overrides.IsEmpty())
            throw Platform::CommandLineError(
                "Batch cooking (cook-all) rejects per-texture overrides; use individual settings "
                "or single-file cook");
        if (bBatch && bDryRun)
            throw Platform::CommandLineError(
                "--dry-run currently requires a single-file cook request");
        if (bBatch)
        {
            errors << "Recursive batch cooking is not available yet\n";
            return 1;
        }
        if (request.Source.empty())
            throw Platform::CommandLineError(
                "cook requires exactly one source path before its options");

        Platform::ContentRootCandidates candidates;
        candidates.CommandLineOverride = sourceRoot;
        if (const char* environmentRoot = std::getenv("HIKARI_CONTENT"))
            candidates.EnvironmentOverride = environmentRoot;
        candidates.ExecutableRelative = fs::absolute(argv[0]).parent_path() / "content";
        candidates.SourceRelative = fallbackSourceRoot;
        sourceRoot = Platform::ResolveContentRoot(candidates);
        const Asset::TextureCooker cooker(sourceRoot, cookedRoot);
        if (bDryRun)
        {
            std::string error;
            std::vector<std::string> warnings;
            const auto prepared = cooker.Prepare(request, error, warnings);
            for (const auto& warning : warnings)
                errors << "Warning: " << warning << '\n';
            if (!prepared)
            {
                errors << error << '\n';
                return 1;
            }
            PrintPrepared(*prepared, output);
            return 0;
        }
        const auto result = cooker.Cook(request);
        for (const auto& warning : result.Warnings)
            errors << "Warning: " << warning << '\n';
        if (result.Status == Asset::TextureCookStatus::Failed)
        {
            errors << result.Error << '\n';
            return 1;
        }
        output << (result.Status == Asset::TextureCookStatus::Skipped ? "Unchanged: " : "Cooked: ")
               << result.Output.string() << '\n';
        return 0;
    }
    catch (const Platform::CommandLineError& exception)
    {
        errors << exception.what() << '\n';
        return 2;
    }
    catch (const std::exception& exception)
    {
        errors << exception.what() << '\n';
        return 1;
    }
}

} // namespace Hikari::Tools
