#pragma once

#include <algorithm>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

struct DdsFixturePayload
{
    uint32_t Level;
    uint32_t Face;
    size_t Offset;
    size_t Size;
};

struct DdsFixture
{
    std::string Bytes;
    std::vector<DdsFixturePayload> Payloads;

    void SetWord(size_t offset, uint32_t value)
    {
        for (size_t byte = 0; byte < 4; ++byte)
            Bytes.at(offset + byte) = static_cast<char>(value >> (byte * 8));
    }
};

// Explicit fixture layout, independent of the production reader/format mapping.
inline DdsFixture MakeDdsFixture(std::string_view fourCC, uint32_t blockBytes, uint32_t width = 7,
                                 uint32_t height = 5, uint32_t levels = 3, uint32_t faces = 1,
                                 uint32_t dxgi = 98, uint32_t alpha = 0)
{
    const bool dx10 = fourCC == "DX10";
    DdsFixture fixture{std::string(dx10 ? 148 : 128, '\0'), {}};
    fixture.Bytes.replace(0, 4, "DDS ");
    fixture.SetWord(4, 124);
    fixture.SetWord(8, 0x1007 | 0x80000 | (levels > 1 ? 0x20000 : 0));
    fixture.SetWord(12, height);
    fixture.SetWord(16, width);
    fixture.SetWord(20, ((width + 3) / 4) * ((height + 3) / 4) * blockBytes);
    fixture.SetWord(28, levels);
    fixture.SetWord(76, 32);
    fixture.SetWord(80, 4);
    fixture.Bytes.replace(84, 4, fourCC);
    fixture.SetWord(108, 0x1000 | (levels > 1 ? 0x400008 : 0) | (faces == 6 ? 8 : 0));
    fixture.SetWord(112, faces == 6 ? 0xfe00 : 0);
    if (dx10)
    {
        fixture.SetWord(128, dxgi);
        fixture.SetWord(132, 3);
        fixture.SetWord(136, faces == 6 ? 4 : 0);
        fixture.SetWord(140, 1);
        fixture.SetWord(144, alpha);
    }
    for (uint32_t face = 0; face < faces; ++face)
        for (uint32_t level = 0; level < levels; ++level)
        {
            const size_t size = size_t((std::max(width >> level, 1u) + 3) / 4) *
                                ((std::max(height >> level, 1u) + 3) / 4) * blockBytes;
            fixture.Payloads.push_back({level, face, fixture.Bytes.size(), size});
            for (size_t byte = 0; byte < size; ++byte)
                fixture.Bytes.push_back(static_cast<char>((face + 1) * 29 + level * 7 + byte));
        }
    return fixture;
}
