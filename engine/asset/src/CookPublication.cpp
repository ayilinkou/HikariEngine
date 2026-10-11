#include "CookPublication.h"

#include <format>
#include <system_error>

#include <nlohmann/json.hpp>

#include "CookFileUtil.h"
#include "SettingsJson.h"

namespace Hikari::Asset
{

namespace
{
std::filesystem::path TransactionDirectory(const TextureCookPaths& paths)
{
    auto directory = paths.CookedPath;
    directory += ".transaction";
    return directory;
}

bool CopyFile(const std::filesystem::path& source, const std::filesystem::path& destination,
              std::string& error)
{
    std::error_code ec;
    std::filesystem::copy_file(source, destination,
                               std::filesystem::copy_options::overwrite_existing, ec);
    if (ec)
    {
        error = std::format("{}: Cannot preserve/restore cooked file: {}", source.string(),
                            ec.message());
        return false;
    }
    return FlushCookFile(destination, error);
}

bool RestoreFile(const std::filesystem::path& directory, const std::filesystem::path& destination,
                 std::string_view backupName, bool existed, std::string& error)
{
    if (existed)
    {
        // Keep the backup immutable so interruption during recovery is itself recoverable.
        const auto temporary = directory / (std::string(backupName) + ".restore");
        return CopyFile(directory / backupName, temporary, error) &&
               ReplaceCookFile(temporary, destination, error);
    }
    std::error_code ec;
    std::filesystem::remove(destination, ec);
    if (ec)
        error = std::format("{}: Cannot remove interrupted publication: {}", destination.string(),
                            ec.message());
    return !ec;
}

bool RemoveTransaction(const std::filesystem::path& directory, std::string& error)
{
    std::error_code ec;
    std::filesystem::remove_all(directory, ec);
    if (ec)
        error = std::format("{}: Cannot clean cooking transaction: {}", directory.string(),
                            ec.message());
    return !ec;
}
} // namespace

CookPublication::CookPublication(const TextureCookPaths& paths)
    : m_Paths(paths), m_Directory(TransactionDirectory(paths)),
      m_Output(m_Directory / "output.ktx2")
{
}

std::unique_ptr<CookPublication> CookPublication::Begin(const TextureCookPaths& paths,
                                                        std::string& error)
{
    std::unique_ptr<CookPublication> publication(new CookPublication(paths));
    std::error_code ec;
    if (!std::filesystem::create_directory(publication->m_Directory, ec))
    {
        error =
            std::format("{}: Cannot reserve cooking transaction: {}",
                        publication->m_Directory.string(), ec ? ec.message() : "already exists");
        // An existing transaction belongs to recovery; do not remove it.
        publication->m_Directory.clear();
        return nullptr;
    }
    return publication;
}

CookPublication::~CookPublication()
{
    if (!m_Directory.empty())
    {
        std::string ignored;
        Recover(m_Paths, ignored);
    }
}

bool CookPublication::Stage(const CookRecord& record, std::string& error)
{
    namespace fs = std::filesystem;
    std::error_code ec;
    const bool hadOutput = fs::exists(m_Paths.CookedPath, ec);
    if (ec)
    {
        error = std::format("{}: Cannot inspect previous output: {}", m_Paths.CookedPath.string(),
                            ec.message());
        return false;
    }
    const bool hadRecord = fs::exists(m_Paths.RecordPath, ec);
    if (ec)
    {
        error = std::format("{}: Cannot inspect previous record: {}", m_Paths.RecordPath.string(),
                            ec.message());
        return false;
    }
    if ((hadOutput && !CopyFile(m_Paths.CookedPath, m_Directory / "previous.ktx2", error)) ||
        (hadRecord && !CopyFile(m_Paths.RecordPath, m_Directory / "previous.json", error)) ||
        !FlushCookFile(m_Output, error) ||
        !WriteCookText(m_Directory / "record.json", SerializeCookRecord(record), error))
        return false;
    // Publish the journal last. Until it exists, no public file may be replaced.
    return WriteCookText(m_Directory / "journal.json",
                         nlohmann::json{{"version", 1},
                                        {"hadOutput", hadOutput},
                                        {"hadRecord", hadRecord},
                                        {"fingerprint", record.Fingerprint},
                                        {"outputHash", record.OutputHash}}
                             .dump(),
                         error);
}

bool CookPublication::PublishOutput(std::string& error)
{
    return ReplaceCookFile(m_Output, m_Paths.CookedPath, error);
}

bool CookPublication::PublishRecord(std::string& error)
{
    return ReplaceCookFile(m_Directory / "record.json", m_Paths.RecordPath, error);
}

bool CookPublication::Recover(const TextureCookPaths& paths, std::string& error)
{
    namespace fs = std::filesystem;
    const auto directory = TransactionDirectory(paths);
    std::error_code ec;
    const bool exists = fs::exists(directory, ec);
    if (ec)
    {
        error = std::format("{}: Cannot inspect cooking transaction: {}", directory.string(),
                            ec.message());
        return false;
    }
    if (!exists)
        return true;
    const auto journalPath = directory / "journal.json";
    const bool hadJournal = fs::exists(journalPath, ec);
    if (ec)
    {
        error = std::format("{}: Cannot inspect cooking journal: {}", journalPath.string(),
                            ec.message());
        return false;
    }
    if (!hadJournal)
        return RemoveTransaction(directory, error);
    const auto text = ReadCookText(journalPath, error);
    if (!text)
        return false;
    const auto journal = ParseSettingsJson(*text, journalPath.string(), error);
    if (!journal)
        return false;
    try
    {
        if (journal->size() != 5 || journal->at("version") != 1 ||
            !journal->at("hadOutput").is_boolean() || !journal->at("hadRecord").is_boolean())
        {
            error = std::format("{}: Invalid cooking journal; transaction retained for recovery",
                                journalPath.string());
            return false;
        }
        const auto fingerprint = journal->at("fingerprint").get<std::string>();
        const auto hash = journal->at("outputHash").get<std::string>();
        const auto record = ReadCookRecord(paths.RecordPath);
        const bool committed =
            record && record->Fingerprint == fingerprint && record->OutputHash == hash &&
            IsCookCurrent(paths.CookedPath, paths.RecordPath, fingerprint, record->Versions);
        if (!committed && (!RestoreFile(directory, paths.CookedPath, "previous.ktx2",
                                        journal->at("hadOutput").get<bool>(), error) ||
                           !RestoreFile(directory, paths.RecordPath, "previous.json",
                                        journal->at("hadRecord").get<bool>(), error)))
            return false;
        // Recovery is complete before cleanup begins. Removing this marker
        // first makes interruption partway through cleanup harmless.
        fs::remove(journalPath, ec);
        if (ec)
        {
            error = std::format("{}: Cannot retire cooking journal: {}", journalPath.string(),
                                ec.message());
            return false;
        }
        return RemoveTransaction(directory, error);
    }
    catch (const nlohmann::json::exception& exception)
    {
        error = std::format("{}: Cannot recover cooking transaction: {}", journalPath.string(),
                            exception.what());
        return false;
    }
}

} // namespace Hikari::Asset
