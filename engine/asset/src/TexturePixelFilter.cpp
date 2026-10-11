#include "TexturePixelFilter.h"

#include <algorithm>
#include <array>
#include <cmath>

namespace Hikari::Asset
{

std::optional<Image> FilterTextureMip(const Image& source, TextureColorSpace color,
                                      std::string& error)
{
    error.clear();
    const auto width = source.Extent.Width, height = source.Extent.Height;
    if (!width || !height || uint64_t(width) * height > source.Pixels.size() / 4 ||
        uint64_t(width) * height * 4 != source.Pixels.size() || color == TextureColorSpace::Auto)
    {
        error = "Mip filtering requires a complete RGBA image and resolved colour space";
        return std::nullopt;
    }
    Image result;
    result.Extent = {std::max(width / 2, 1u), std::max(height / 2, 1u)};
    result.SourceChannels = source.SourceChannels;
    result.Pixels.resize(size_t(result.Extent.Width) * result.Extent.Height * 4);
    const auto decode = [](double value)
    { return value <= 0.04045 ? value / 12.92 : std::pow((value + 0.055) / 1.055, 2.4); };
    const auto encode = [](double value)
    { return value <= 0.0031308 ? value * 12.92 : 1.055 * std::pow(value, 1.0 / 2.4) - 0.055; };
    for (uint32_t y = 0; y < result.Extent.Height; ++y)
        for (uint32_t x = 0; x < result.Extent.Width; ++x)
        {
            const double x0 = double(x) * width / result.Extent.Width;
            const double x1 = double(x + 1) * width / result.Extent.Width;
            const double y0 = double(y) * height / result.Extent.Height;
            const double y1 = double(y + 1) * height / result.Extent.Height;
            std::array<double, 4> sum{};
            double weightSum = 0;
            for (uint32_t sy = static_cast<uint32_t>(y0);
                 sy < std::min(uint32_t(std::ceil(y1)), height); ++sy)
                for (uint32_t sx = static_cast<uint32_t>(x0);
                     sx < std::min(uint32_t(std::ceil(x1)), width); ++sx)
                {
                    const double weight =
                        (std::min(x1, double(sx + 1)) - std::max(x0, double(sx))) *
                        (std::min(y1, double(sy + 1)) - std::max(y0, double(sy)));
                    weightSum += weight;
                    for (size_t channel = 0; channel < 4; ++channel)
                    {
                        double value =
                            source.Pixels[(size_t(sy) * width + sx) * 4 + channel] / 255.0;
                        if (color == TextureColorSpace::Srgb && channel < 3)
                            value = decode(value);
                        sum[channel] += value * weight;
                    }
                }
            for (size_t channel = 0; channel < 4; ++channel)
            {
                double value = sum[channel] / weightSum;
                if (color == TextureColorSpace::Srgb && channel < 3)
                    value = encode(value);
                result.Pixels[(size_t(y) * result.Extent.Width + x) * 4 + channel] =
                    static_cast<uint8_t>(std::lround(std::clamp(value, 0.0, 1.0) * 255));
            }
        }
    return result;
}

} // namespace Hikari::Asset
