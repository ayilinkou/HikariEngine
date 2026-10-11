#include "CookHash.h"

#include <algorithm>
#include <bit>
#include <format>
#include <fstream>

namespace Hikari::Asset
{

void CookHash::Transform()
{
    constexpr std::array<uint32_t, 64> constants{
        0x428a2f98, 0x71374491, 0xb5c0fbcf, 0xe9b5dba5, 0x3956c25b, 0x59f111f1, 0x923f82a4,
        0xab1c5ed5, 0xd807aa98, 0x12835b01, 0x243185be, 0x550c7dc3, 0x72be5d74, 0x80deb1fe,
        0x9bdc06a7, 0xc19bf174, 0xe49b69c1, 0xefbe4786, 0x0fc19dc6, 0x240ca1cc, 0x2de92c6f,
        0x4a7484aa, 0x5cb0a9dc, 0x76f988da, 0x983e5152, 0xa831c66d, 0xb00327c8, 0xbf597fc7,
        0xc6e00bf3, 0xd5a79147, 0x06ca6351, 0x14292967, 0x27b70a85, 0x2e1b2138, 0x4d2c6dfc,
        0x53380d13, 0x650a7354, 0x766a0abb, 0x81c2c92e, 0x92722c85, 0xa2bfe8a1, 0xa81a664b,
        0xc24b8b70, 0xc76c51a3, 0xd192e819, 0xd6990624, 0xf40e3585, 0x106aa070, 0x19a4c116,
        0x1e376c08, 0x2748774c, 0x34b0bcb5, 0x391c0cb3, 0x4ed8aa4a, 0x5b9cca4f, 0x682e6ff3,
        0x748f82ee, 0x78a5636f, 0x84c87814, 0x8cc70208, 0x90befffa, 0xa4506ceb, 0xbef9a3f7,
        0xc67178f2};
    std::array<uint32_t, 64> words{};
    for (size_t i = 0; i < 16; ++i)
        words[i] = (uint32_t(m_Block[i * 4]) << 24) | (uint32_t(m_Block[i * 4 + 1]) << 16) |
                   (uint32_t(m_Block[i * 4 + 2]) << 8) | uint32_t(m_Block[i * 4 + 3]);
    for (size_t i = 16; i < words.size(); ++i)
    {
        const auto x = words[i - 15], y = words[i - 2];
        const auto s0 = std::rotr(x, 7) ^ std::rotr(x, 18) ^ (x >> 3);
        const auto s1 = std::rotr(y, 17) ^ std::rotr(y, 19) ^ (y >> 10);
        words[i] = words[i - 16] + s0 + words[i - 7] + s1;
    }
    auto [a, b, c, d, e, f, g, h] = m_State;
    for (size_t i = 0; i < words.size(); ++i)
    {
        const auto s1 = std::rotr(e, 6) ^ std::rotr(e, 11) ^ std::rotr(e, 25);
        const auto choice = (e & f) ^ (~e & g);
        const auto t1 = h + s1 + choice + constants[i] + words[i];
        const auto s0 = std::rotr(a, 2) ^ std::rotr(a, 13) ^ std::rotr(a, 22);
        const auto majority = (a & b) ^ (a & c) ^ (b & c);
        const auto t2 = s0 + majority;
        h = g;
        g = f;
        f = e;
        e = d + t1;
        d = c;
        c = b;
        b = a;
        a = t1 + t2;
    }
    const std::array working{a, b, c, d, e, f, g, h};
    for (size_t i = 0; i < working.size(); ++i)
        m_State[i] += working[i];
}

void CookHash::Update(std::span<const uint8_t> bytes)
{
    m_Size += bytes.size();
    while (!bytes.empty())
    {
        const auto count = std::min(bytes.size(), m_Block.size() - m_Used);
        std::copy_n(bytes.begin(), count, m_Block.begin() + m_Used);
        bytes = bytes.subspan(count);
        m_Used += count;
        if (m_Used == m_Block.size())
        {
            Transform();
            m_Used = 0;
        }
    }
}

std::string CookHash::Finish()
{
    const uint64_t bits = m_Size * 8;
    m_Block[m_Used++] = 0x80;
    if (m_Used > 56)
    {
        std::fill(m_Block.begin() + m_Used, m_Block.end(), 0);
        Transform();
        m_Used = 0;
    }
    std::fill(m_Block.begin() + m_Used, m_Block.begin() + 56, 0);
    for (size_t i = 0; i < 8; ++i)
        m_Block[63 - i] = static_cast<uint8_t>(bits >> (i * 8));
    Transform();
    std::string digest;
    for (const auto word : m_State)
        digest += std::format("{:08x}", word);
    return digest;
}

std::string HashCookText(std::string_view text)
{
    CookHash hash;
    hash.Update({reinterpret_cast<const uint8_t*>(text.data()), text.size()});
    return hash.Finish();
}

std::optional<std::string> HashCookFile(const std::filesystem::path& path, std::string& error)
{
    std::ifstream file(path, std::ios::binary);
    if (!file)
    {
        error = std::format("{}: Cannot read file for content hashing", path.string());
        return std::nullopt;
    }
    CookHash hash;
    std::array<uint8_t, 64 * 1024> buffer{};
    while (file)
    {
        file.read(reinterpret_cast<char*>(buffer.data()), buffer.size());
        hash.Update(std::span(buffer).first(static_cast<size_t>(file.gcount())));
    }
    if (!file.eof() || file.bad())
    {
        error = std::format("{}: Failed while hashing file contents", path.string());
        return std::nullopt;
    }
    return hash.Finish();
}

} // namespace Hikari::Asset
