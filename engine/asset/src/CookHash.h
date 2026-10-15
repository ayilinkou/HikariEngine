#pragma once

#include <array>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <span>
#include <string>
#include <string_view>

namespace Hikari::Asset
{

// Streaming SHA-256: fingerprints and output integrity must be deterministic
// across hosts and must not depend on std::hash or filesystem timestamps.
class CookHash
{
public:
    void Update(std::span<const uint8_t> bytes);
    std::string Finish();

private:
    void Transform();
    std::array<uint32_t, 8> m_State{0x6a09e667, 0xbb67ae85, 0x3c6ef372, 0xa54ff53a,
                                    0x510e527f, 0x9b05688c, 0x1f83d9ab, 0x5be0cd19};
    std::array<uint8_t, 64> m_Block{};
    uint64_t m_Size = 0;
    size_t m_Used = 0;
};

std::string HashCookText(std::string_view text);
std::optional<std::string> HashCookFile(const std::filesystem::path& path, std::string& error);

} // namespace Hikari::Asset
