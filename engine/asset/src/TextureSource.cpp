#include <asset/TextureSource.h>

#include <algorithm>
#include <cctype>
#include <cmath>
#include <format>
#include <random>
#include <stdexcept>
#include <system_error>

#include <asset/KtxReader.h>
#include <asset/TextureCooker.h>

#include "CookFileUtil.h"
#include "CookProcess.h"

namespace Hikari::Asset
{
namespace
{
bool IsCooked(const std::filesystem::path& path)
{
    std::string extension = path.extension().string();
    std::transform(extension.begin(), extension.end(), extension.begin(),
                   [](unsigned char ch) { return static_cast<char>(std::tolower(ch)); });
    return extension == ".ktx2";
}

class AcquisitionLog
{
public:
    AcquisitionLog()
    {
        std::random_device random;
        for (int attempt = 0; attempt < 16; ++attempt)
        {
            const auto directory = std::filesystem::temp_directory_path() /
                                   ("hikari texture acquisition " + std::to_string(random()));
            if (std::filesystem::create_directory(directory))
            {
                m_Directory = directory;
                return;
            }
        }
        throw std::runtime_error("Cannot reserve texture acquisition diagnostic directory");
    }
    ~AcquisitionLog()
    {
        std::error_code ignored;
        std::filesystem::remove_all(m_Directory, ignored);
    }
    std::filesystem::path Path() const { return m_Directory / "cooker.log"; }
    AcquisitionLog(const AcquisitionLog&) = delete;
    AcquisitionLog& operator=(const AcquisitionLog&) = delete;

private:
    std::filesystem::path m_Directory;
};
} // namespace

TextureSource::TextureSource(std::filesystem::path sourceRoot, std::filesystem::path cookedRoot,
                             std::filesystem::path cookerExecutable, double encodingQuality)
    : m_SourceRoot(std::filesystem::weakly_canonical(std::filesystem::absolute(sourceRoot))),
      m_CookedRoot(std::filesystem::weakly_canonical(cookedRoot.empty()
                                                         ? m_SourceRoot.parent_path() / "cooked"
                                                         : std::filesystem::absolute(cookedRoot))),
      m_CookerExecutable(std::move(cookerExecutable)), m_EncodingQuality(encodingQuality)
{
    if (!std::isfinite(encodingQuality) || encodingQuality < 0 || encodingQuality > 1)
        throw std::invalid_argument("Texture encoding quality must be a finite number in [0, 1]");
}

std::filesystem::path TextureSource::ResolvePath(const std::filesystem::path& requested) const
{
    if (requested.empty() || (requested.has_root_path() && !requested.is_absolute()))
        throw std::runtime_error("A source-relative or cooked-relative texture path is required");
    const auto& root = IsCooked(requested) ? m_CookedRoot : m_SourceRoot;
    const auto path =
        std::filesystem::weakly_canonical(requested.is_absolute() ? requested : root / requested);
    const auto relative = RelativeCookPath(path, root);
    if (!relative || relative->empty())
        throw std::runtime_error(
            std::format("{}: Texture path escapes its {} root {}", requested.string(),
                        IsCooked(requested) ? "cooked" : "source", root.string()));
    return path;
}

std::filesystem::path TextureSource::SettingsPath(const std::filesystem::path& resolved) const
{
    if (IsCooked(resolved))
        return {};
    std::string name = resolved.filename().string();
    std::transform(name.begin(), name.end(), name.begin(),
                   [](unsigned char ch) { return static_cast<char>(std::tolower(ch)); });
    if (name.ends_with(".cubemap.json"))
        return resolved;
    auto path = resolved;
    path += ".texture.json";
    return path;
}

std::optional<TextureData> TextureSource::Read(const std::filesystem::path& requested,
                                               std::string& error,
                                               std::vector<std::string>& warnings) const
{
    error.clear();
    warnings.clear();
    try
    {
        const auto resolved = ResolvePath(requested);
        if (IsCooked(resolved))
            return ReadKtxTexture(resolved, error);
        const TextureCooker cooker(m_SourceRoot, m_CookedRoot);
        const auto prepared = cooker.Prepare(
            {.Source = resolved, .EncodingQuality = m_EncodingQuality, .bRuntime = true}, error,
            warnings);
        if (!prepared)
            return std::nullopt;
        std::error_code ec;
        if (!std::filesystem::is_regular_file(m_CookerExecutable, ec))
        {
            error = std::format("{}: Shared Release texture cooker was not found at {}. "
                                "Reconfigure/rebuild to provision HikariTextureCooker.",
                                resolved.string(), m_CookerExecutable.string());
            return std::nullopt;
        }
        const AcquisitionLog log;
        // The child owns freshness checks, output hashes, locking and safe
        // publication. No outer cook lock is held while waiting for it.
        if (!RunCookProcess(m_CookerExecutable,
                            {"cook", CookPathUtf8(resolved), "--source-root",
                             CookPathUtf8(m_SourceRoot), "--cooked-root",
                             CookPathUtf8(m_CookedRoot), "--runtime", "--quality",
                             std::format("{}", m_EncodingQuality)},
                            log.Path(), error, false))
        {
            error = std::format("{}: Texture cooking failed: {}", resolved.string(), error);
            return std::nullopt;
        }
        return ReadKtxTexture(prepared->Paths.CookedPath, error);
    }
    catch (const std::exception& exception)
    {
        error =
            std::format("{}: Texture acquisition failed: {}", requested.string(), exception.what());
        return std::nullopt;
    }
}
} // namespace Hikari::Asset
