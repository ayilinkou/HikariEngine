#include <asset/TextureCooker.h>

#include <algorithm>
#include <cctype>
#include <cmath>
#include <exception>
#include <format>
#include <map>
#include <set>
#include <string_view>
#include <system_error>
#include <utility>

#include <nlohmann/json.hpp>

#include "CookFileUtil.h"

namespace Hikari::Asset
{
namespace
{
namespace fs = std::filesystem;

std::string LowerName(const fs::path& path)
{
    auto name = path.filename().string();
    std::transform(name.begin(), name.end(), name.begin(),
                   [](unsigned char ch) { return static_cast<char>(std::tolower(ch)); });
    return name;
}

bool IsDescriptor(const fs::path& path)
{
    return std::string_view(LowerName(path)).ends_with(".cubemap.json");
}

bool IsSource(const fs::path& path)
{
    if (IsDescriptor(path))
        return true;
    const auto extension = fs::path(LowerName(path)).extension().string();
    return extension == ".png" || extension == ".jpg" || extension == ".jpeg" ||
           extension == ".dds";
}

std::vector<fs::path> Discover(const fs::path& sourceRoot, const fs::path& cookedRoot,
                               std::vector<std::string>& errors)
{
    std::vector<fs::path> directories{sourceRoot};
    std::set<fs::path> sources;
    while (!directories.empty())
    {
        const auto directory = std::move(directories.back());
        directories.pop_back();
        std::error_code ec;
        fs::directory_iterator iterator(directory, ec);
        if (ec)
        {
            errors.push_back(
                std::format("{}: Cannot discover textures: {}", directory.string(), ec.message()));
            continue;
        }
        for (; iterator != fs::directory_iterator{}; iterator.increment(ec))
        {
            if (ec)
                break;
            const auto path = iterator->path();
            const auto status = iterator->symlink_status(ec);
            if (ec)
            {
                errors.push_back(std::format("{}: Cannot inspect source entry: {}", path.string(),
                                             ec.message()));
                ec.clear();
                continue;
            }
            if (fs::is_directory(status))
            {
                // No directory symlinks are followed: discovery cannot escape
                // the source tree, cycle, or re-enter the generated tree.
                const auto resolved = fs::weakly_canonical(path, ec);
                if (ec)
                {
                    errors.push_back(std::format("{}: Cannot resolve source directory: {}",
                                                 path.string(), ec.message()));
                    ec.clear();
                }
                else if (!RelativeCookPath(resolved, cookedRoot))
                    directories.push_back(path);
                continue;
            }
            if (!IsSource(path))
                continue;
            if (fs::is_symlink(status) && iterator->is_directory(ec))
                continue;
            if (ec)
                ec.clear();
            // Keep unresolved/outside aliases as requests so the existing
            // per-file validation reports them without reading foreign assets.
            const auto resolved = fs::canonical(path, ec);
            if (ec)
            {
                sources.insert(path);
                ec.clear();
            }
            else if (!RelativeCookPath(resolved, cookedRoot))
                sources.insert(RelativeCookPath(resolved, sourceRoot) ? resolved : path);
        }
        if (ec)
            errors.push_back(std::format("{}: Cannot finish texture discovery: {}",
                                         directory.string(), ec.message()));
    }
    return {sources.begin(), sources.end()};
}

void ReserveFaces(const fs::path& descriptor, const fs::path& sourceRoot,
                  const fs::path& cookedRoot, std::set<fs::path>& reserved)
{
    std::error_code ec;
    const auto resolved = fs::canonical(descriptor, ec);
    if (ec || !fs::is_regular_file(resolved, ec) || !RelativeCookPath(resolved, sourceRoot) ||
        RelativeCookPath(resolved, cookedRoot))
        return;
    std::string ignored;
    const auto text = ReadCookText(resolved, ignored);
    if (!text)
        return;
    using Json = nlohmann::json;
    bool bRootFaces = false;
    std::vector<bool> facesContext;
    const auto callback = [&](int depth, Json::parse_event_t event, Json& value)
    {
        if (event == Json::parse_event_t::key && depth == 1)
            bRootFaces = value == "faces";
        if (event == Json::parse_event_t::object_start || event == Json::parse_event_t::array_start)
            facesContext.push_back((depth == 1 && bRootFaces) ||
                                   (!facesContext.empty() && facesContext.back()));
        else if (event == Json::parse_event_t::object_end ||
                 event == Json::parse_event_t::array_end)
            facesContext.pop_back();
        else if (event == Json::parse_event_t::value && value.is_string() &&
                 ((depth == 1 && bRootFaces) || (!facesContext.empty() && facesContext.back())))
        {
            const auto name = value.get<std::string>();
            if (!name.empty() && name.find('\0') == std::string::npos)
            {
                const auto reference = resolved.parent_path() / fs::path(name);
                reserved.insert(reference.lexically_normal());
                std::error_code referenceError;
                const auto canonical = fs::weakly_canonical(reference, referenceError);
                if (!referenceError)
                    reserved.insert(canonical);
            }
        }
        return true;
    };
    // Collect explicit paths before strict validation, including duplicate
    // fields and strings seen before a syntax error. A failed descriptor must
    // not silently turn its recoverable face references into standalone assets.
    const auto document = Json::parse(*text, callback, false);
    (void)document;
}

bool IsReserved(const fs::path& path, const std::set<fs::path>& reserved)
{
    if (reserved.contains(path))
        return true;
    for (const auto& reference : reserved)
    {
        std::error_code ec;
        if (fs::equivalent(path, reference, ec))
            return true;
    }
    return false;
}
} // namespace

TextureCookBatchResult TextureCooker::CookAll(const TextureCookBatchOptions& options) const
{
    return CookBatch(options, nullptr);
}

TextureCookBatchResult TextureCooker::CookAll(const TextureCookBatchOptions& options,
                                              const TextureCookEncoder& encoder) const
{
    return CookBatch(options, &encoder);
}

TextureCookBatchResult TextureCooker::CookBatch(const TextureCookBatchOptions& options,
                                                const TextureCookEncoder* encoder) const
{
    TextureCookBatchResult batch;
    if (options.EncoderThreads > 128)
    {
        batch.Errors.push_back("Encoder threads must be in [0, 128] (0 selects automatic)");
        return batch;
    }
    if (!std::isfinite(options.EncodingQuality) || options.EncodingQuality < 0 ||
        options.EncodingQuality > 1)
    {
        batch.Errors.push_back("Encoding quality must be a finite number in [0, 1]");
        return batch;
    }
    fs::path sourceRoot, cookedRoot;
    std::string error;
    if (!ResolveCookRoots(m_SourceRoot, m_CookedRoot, sourceRoot, cookedRoot, error))
    {
        batch.Errors.push_back(error);
        return batch;
    }
    if (RelativeCookPath(sourceRoot, cookedRoot))
    {
        batch.Errors.push_back(
            "Cooked root must not contain the source root; choose a separate --cooked-root");
        return batch;
    }
    const auto sources = Discover(sourceRoot, cookedRoot, batch.Errors);
    std::set<fs::path> reserved;
    std::vector<fs::path> descriptors;
    for (const auto& source : sources)
        if (IsDescriptor(source))
        {
            descriptors.push_back(source);
            ReserveFaces(source, sourceRoot, cookedRoot, reserved);
        }
    const auto requestFor = [&](const fs::path& source)
    {
        TextureCookRequest request{.Source = source};
        request.bForce = options.bForce;
        request.EncoderThreads = options.EncoderThreads;
        request.EncodingQuality = options.EncodingQuality;
        return request;
    };
    const auto cook = [&](const fs::path& source)
    {
        const auto request = requestFor(source);
        TextureCookResult result;
        try
        {
            result = encoder ? Cook(request, *encoder) : Cook(request);
        }
        catch (const std::exception& exception)
        {
            result.Error = std::format("{}: Cooking failed: {}", source.string(), exception.what());
        }
        batch.Entries.push_back(
            {RelativeCookPath(source, sourceRoot).value_or(source), std::move(result)});
    };
    std::map<fs::path, TextureCookResult> invalidDescriptors;
    for (const auto& descriptor : descriptors)
    {
        // Validate without writes before any descriptor is cooked. Its face
        // reservations survive both validation and later encoder failures.
        TextureCookResult failure;
        if (!Prepare(requestFor(descriptor), failure.Error, failure.Warnings))
            invalidDescriptors.emplace(descriptor, std::move(failure));
    }
    for (const auto& descriptor : descriptors)
    {
        const auto invalid = invalidDescriptors.find(descriptor);
        if (invalid != invalidDescriptors.end())
            batch.Entries.push_back({RelativeCookPath(descriptor, sourceRoot).value_or(descriptor),
                                     std::move(invalid->second)});
        else
            cook(descriptor);
        ReserveFaces(descriptor, sourceRoot, cookedRoot, reserved);
    }
    for (const auto& source : sources)
        if (!IsDescriptor(source) && !IsReserved(source, reserved))
            cook(source);
    return batch;
}
} // namespace Hikari::Asset
