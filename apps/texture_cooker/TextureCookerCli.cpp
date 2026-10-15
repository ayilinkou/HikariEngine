#include "TextureCookerCli.h"

#include <cmath>
#include <exception>
#include <format>
#include <optional>
#include <ostream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

#include <SDL3/SDL_stdinc.h>

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
              "       HikariTextureCooker cook-all [operational options]\n"
              "       HikariTextureCooker init-texture <path/name> [settings options]\n"
              "       HikariTextureCooker init-cubemap <path/name> [settings options]\n\n"
              "Cooking options:\n";
    option("--source-root <dir>", "Source root (default: HIKARI_CONTENT or content/)");
    option("--cooked-root <dir>", "Cooked root (default: source root's sibling cooked/)");
    option("--force", "Bypass incremental checks without resetting settings");
    option("--threads <0..128>", "CPU encoder thread limit (default: 0 = automatic)");
    option("--quality <0..1>", "Minimum encoding quality (default: 1.0; exact with --force)");
    option("--runtime", "Single-file runtime policy: auto opaque colour pixels prefer BC1");
    option("--dry-run", "Validate a single request and print paths without writes");
    option("--help", "Show help");
    output << "\nCreation options (no source images required):\n";
    option("--overwrite, --force", "Replace existing settings (otherwise fail)");
    option("", "Names get .texture.json or .cubemap.json appended");
    option("", "Paths use the current working directory; parent must exist");
    option("", "Cooked 2D settings use <image>.texture.json beside the image");
    output << "\nSettings options (cook and init commands; cook preserves omitted settings):\n";
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

double RequireUnitInterval(const Platform::CommandLineOption& option)
{
    const auto value = option.RequireValue();
    try
    {
        size_t consumed = 0;
        const double number = std::stod(value, &consumed);
        if (consumed != value.size() || !std::isfinite(number) || number < 0 || number > 1)
            throw std::invalid_argument("Invalid number");
        return number;
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
           << "Encoding quality: " << prepared.Request.EncodingQuality << '\n'
           << "Force: " << (prepared.Request.bForce ? "true" : "false") << '\n';
    if (prepared.Dds)
        output << "DDS mip levels: " << prepared.Dds->Levels << '\n'
               << "DDS faces: " << prepared.Dds->Faces << '\n';
    for (size_t i = 0; i < prepared.Inputs.size(); ++i)
        output << (prepared.Descriptor ? std::string(Asset::kCubemapFaceNames[i])
                   : prepared.Dds      ? "DDS"
                                       : "Image")
               << ": " << prepared.Inputs[i].string() << '\n';
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
            throw Platform::CommandLineError("Expected a cooker command; use --help for usage");
        const std::string_view command = argv[1];
        const bool bBatch = command == "cook-all";
        const bool bInitTexture = command == "init-texture";
        const bool bInitCubemap = command == "init-cubemap";
        const bool bInit = bInitTexture || bInitCubemap;
        if (command != "cook" && !bBatch && !bInit)
            throw Platform::CommandLineError(
                std::format("Unknown command '{}'; expected cook, cook-all, init-texture or "
                            "init-cubemap",
                            command));

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
            if (bInit &&
                (flag == "--source-root" || flag == "--cooked-root" || flag == "--threads" ||
                 flag == "--quality" || flag == "--runtime" || flag == "--dry-run"))
                throw Platform::CommandLineError(
                    std::format("{}: {} is a cooking-only option", command, flag));
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
                if (count > 128)
                    throw Platform::CommandLineError("--threads: Expected an integer in [0, 128]");
                request.EncoderThreads = static_cast<uint32_t>(count);
            }
            else if (flag == "--quality")
                request.EncodingQuality = RequireUnitInterval(option);
            else if (flag == "--runtime")
            {
                option.RequireNoValue();
                if (bBatch)
                    throw Platform::CommandLineError("--runtime: Requires single-file cook");
                request.bRuntime = true;
            }
            else if (flag == "--force" || (bInit && flag == "--overwrite"))
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
                request.Overrides.AlphaCutoff = RequireUnitInterval(option);
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
        if (bInit)
        {
            if (request.Source.empty())
                throw Platform::CommandLineError(std::format(
                    "{} requires exactly one template name before its options", command));
            std::string error;
            const auto path = bInitTexture
                                  ? Asset::TextureCooker::CreateTextureSettings(
                                        request.Source, request.Overrides, request.bForce, error)
                                  : Asset::TextureCooker::CreateCubemapSettings(
                                        request.Source, request.Overrides, request.bForce, error);
            if (!path)
            {
                errors << error << '\n';
                return 1;
            }
            output << "Created: " << path->string() << '\n';
            return 0;
        }
        if (bBatch && !request.Overrides.IsEmpty())
            throw Platform::CommandLineError(
                "Batch cooking (cook-all) rejects per-texture overrides; use individual settings "
                "or single-file cook");
        if (bBatch && bDryRun)
            throw Platform::CommandLineError(
                "--dry-run currently requires a single-file cook request");
        if (!bBatch && request.Source.empty())
            throw Platform::CommandLineError(
                "cook requires exactly one source path before its options");

        Platform::ContentRootCandidates candidates;
        candidates.CommandLineOverride = sourceRoot;
        if (const char* environmentRoot = SDL_getenv("HIKARI_CONTENT"))
            candidates.EnvironmentOverride = environmentRoot;
        candidates.ExecutableRelative = fs::absolute(argv[0]).parent_path() / "content";
        candidates.SourceRelative = fallbackSourceRoot;
        sourceRoot = Platform::ResolveContentRoot(candidates);
        const Asset::TextureCooker cooker(sourceRoot, cookedRoot);
        if (bBatch)
        {
            const auto batch = cooker.CookAll({.bForce = request.bForce,
                                               .EncoderThreads = request.EncoderThreads,
                                               .EncodingQuality = request.EncodingQuality});
            size_t cooked = 0, unchanged = 0, failed = batch.Errors.size();
            for (const auto& error : batch.Errors)
                errors << error << '\n';
            for (const auto& entry : batch.Entries)
            {
                for (const auto& warning : entry.Result.Warnings)
                    errors << "Warning: " << warning << '\n';
                switch (entry.Result.Status)
                {
                    case Asset::TextureCookStatus::Cooked:
                        ++cooked;
                        output << "Cooked: " << entry.Result.Output.string() << '\n';
                        break;
                    case Asset::TextureCookStatus::Skipped:
                        ++unchanged;
                        output << "Unchanged: " << entry.Result.Output.string() << '\n';
                        break;
                    case Asset::TextureCookStatus::Failed:
                        ++failed;
                        errors << "Failed: " << entry.Source.string() << ": " << entry.Result.Error
                               << '\n';
                        break;
                }
            }
            output << std::format("Batch: {} cooked, {} unchanged, {} failed\n", cooked, unchanged,
                                  failed);
            return batch.Succeeded() ? 0 : 1;
        }
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
