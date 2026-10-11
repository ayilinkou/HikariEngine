#pragma once

#include <filesystem>
#include <memory>
#include <string>

#include <asset/TextureCookPaths.h>

#include "CookRecord.h"

namespace Hikari::Asset
{

// All operations require the output's CookLock. The immutable backups and
// journal remain until both public files match the proposed record.
class CookPublication
{
public:
    static bool Recover(const TextureCookPaths& paths, std::string& error);
    static std::unique_ptr<CookPublication> Begin(const TextureCookPaths& paths,
                                                  std::string& error);
    ~CookPublication();
    const std::filesystem::path& OutputPath() const { return m_Output; }
    bool Stage(const CookRecord& record, std::string& error);
    bool PublishOutput(std::string& error);
    bool PublishRecord(std::string& error);
    CookPublication(const CookPublication&) = delete;
    CookPublication& operator=(const CookPublication&) = delete;

private:
    explicit CookPublication(const TextureCookPaths& paths);
    TextureCookPaths m_Paths;
    std::filesystem::path m_Directory;
    std::filesystem::path m_Output;
};

} // namespace Hikari::Asset
