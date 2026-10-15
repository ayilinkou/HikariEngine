#pragma once

#include <filesystem>
#include <functional>
#include <string>

#include <asset/PreparedTextureCook.h>

namespace Hikari::Asset
{

// Bump Pipeline when filtering, encoding or container semantics change.
// Dependency identities come from the packages used by the configured build.
struct TextureCookVersions
{
    std::string Pipeline;
    std::string Compressonator;
    std::string Ktx;
    std::string ImageDecoder;

    bool operator==(const TextureCookVersions&) const = default;
};

TextureCookVersions DefaultTextureCookVersions();

struct TextureCookEncoder
{
    TextureCookVersions Versions = DefaultTextureCookVersions();
    // Write only to temporaryOutput. The service validates and publishes it.
    // Called synchronously under the per-output lock; must not reenter Cook
    // for the same output. Encoding implementations arrive in TC5–TC9.
    std::function<bool(const PreparedTextureCook& prepared,
                       const std::filesystem::path& temporaryOutput, std::string& error)>
        Encode;
};

} // namespace Hikari::Asset
