#pragma once

#include <filesystem>
#include <optional>
#include <string>
#include <string_view>

#include <asset/TextureCookEncoder.h>

namespace Hikari::Asset
{

struct CookRecord
{
    std::string Fingerprint;
    std::string OutputHash;
    TextureCookVersions Versions;
    double EncodingQuality = 1.0;
};

std::optional<std::string> FingerprintTextureCook(const PreparedTextureCook& prepared,
                                                  const TextureCookVersions& versions,
                                                  std::string& error);
std::string SerializeCookRecord(const CookRecord& record);
// Legacy records are readable only for transaction recovery, never for cook reuse.
std::optional<CookRecord> ReadCookRecord(const std::filesystem::path& path,
                                         bool allowLegacy = false);
bool IsCookCurrent(const std::filesystem::path& output, const std::filesystem::path& record,
                   std::string_view fingerprint, const TextureCookVersions& versions,
                   double minimumQuality = 0.0);
bool ValidateCookOutput(const PreparedTextureCook& prepared, const std::filesystem::path& output,
                        std::string& error);

} // namespace Hikari::Asset
