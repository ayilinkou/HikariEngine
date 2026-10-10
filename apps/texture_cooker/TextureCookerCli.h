#pragma once

#include <filesystem>
#include <iosfwd>

namespace Hikari::Tools
{

// Streams and the development fallback root are injected for CPU-only CLI tests.
int RunTextureCooker(int argc, const char* const* argv,
                     const std::filesystem::path& fallbackSourceRoot, std::ostream& output,
                     std::ostream& errors);

} // namespace Hikari::Tools
