#pragma once

#include <array>
#include <functional>

#include <asset/DdsTextureInfo.h>

#include "DdsFixture.h"

inline DdsFixture MakePixelDdsFixture(
    Hikari::Asset::DdsPixelLayout layout, uint32_t width, uint32_t height, uint32_t levels,
    uint32_t faces, bool aligned, uint32_t basePadding,
    const std::function<std::array<uint8_t, 4>(uint32_t, uint32_t, uint32_t, uint32_t)>& pixel,
    uint32_t dxgi = 0)
{
    using Hikari::Asset::DdsPixelLayout;
    const bool rgb24 = layout == DdsPixelLayout::RGB8 || layout == DdsPixelLayout::BGR8;
    const bool bgr = layout == DdsPixelLayout::BGR8 || layout == DdsPixelLayout::BGRA8 ||
                     layout == DdsPixelLayout::BGRX8;
    const bool alpha = layout == DdsPixelLayout::RGBA8 || layout == DdsPixelLayout::BGRA8;
    const uint32_t bytes = rgb24 ? 3 : 4;
    auto fixture = MakeDdsFixture(dxgi ? "DX10" : "DXT1", 8, width, height, levels, faces, dxgi);
    fixture.Bytes.resize(dxgi ? 148 : 128);
    fixture.Payloads.clear();
    fixture.SetWord(8, 0x100f | (levels > 1 ? 0x20000 : 0));
    if (!dxgi)
    {
        fixture.SetWord(80, 0x40 | (alpha ? 1 : 0));
        fixture.SetWord(84, 0);
        fixture.SetWord(88, bytes * 8);
        fixture.SetWord(92, bgr ? 0xff0000 : 0xff);
        fixture.SetWord(96, 0xff00);
        fixture.SetWord(100, bgr ? 0xff : 0xff0000);
        fixture.SetWord(104, alpha ? 0xff000000 : 0);
    }
    for (uint32_t face = 0; face < faces; ++face)
        for (uint32_t level = 0; level < levels; ++level)
        {
            const uint32_t w = std::max(width >> level, 1u), h = std::max(height >> level, 1u);
            const uint32_t row = w * bytes;
            const uint32_t pitch =
                (aligned ? ((row + 3) / 4) * 4 : row) + (!level ? basePadding : 0);
            if (!level)
                fixture.SetWord(20, pitch);
            const size_t start = fixture.Bytes.size();
            for (uint32_t y = 0; y < h; ++y)
            {
                for (uint32_t x = 0; x < w; ++x)
                {
                    const auto value = pixel(face, level, x, y);
                    fixture.Bytes.push_back(static_cast<char>(value[bgr ? 2 : 0]));
                    fixture.Bytes.push_back(static_cast<char>(value[1]));
                    fixture.Bytes.push_back(static_cast<char>(value[bgr ? 0 : 2]));
                    if (bytes == 4)
                        fixture.Bytes.push_back(static_cast<char>(alpha ? value[3] : 13));
                }
                fixture.Bytes.append(pitch - row, static_cast<char>(0xcd));
            }
            fixture.Payloads.push_back({level, face, start, fixture.Bytes.size() - start});
        }
    return fixture;
}
