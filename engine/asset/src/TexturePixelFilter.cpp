#include "TexturePixelFilter.h"

#include <algorithm>
#include <array>
#include <cmath>

namespace Hikari::Asset
{

namespace
{
bool HasCompletePixels(const Image& source)
{
    const auto width = source.Extent.Width, height = source.Extent.Height;
    return width && height && uint64_t(width) * height <= source.Pixels.size() / 4 &&
           uint64_t(width) * height * 4 == source.Pixels.size();
}

std::array<double, 3> NormalizeNormal(std::array<double, 3> vector)
{
    const double lengthSquared =
        vector[0] * vector[0] + vector[1] * vector[1] + vector[2] * vector[2];
    // Zero is between bytes 127 and 128. Treat that quantization cell
    // as degenerate rather than amplifying its rounding noise.
    constexpr double kDegenerateLengthSquared = 3.0 / (255.0 * 255.0);
    if (!std::isfinite(lengthSquared) || lengthSquared <= kDegenerateLengthSquared + 1e-12)
        return {0, 0, 1};
    const double inverseLength = 1 / std::sqrt(lengthSquared);
    for (auto& value : vector)
        value *= inverseLength;
    return vector;
}
} // namespace

std::optional<Image> FilterTextureMip(const Image& source, TextureColorSpace color,
                                      std::string& error, TextureUsage usage)
{
    error.clear();
    const auto width = source.Extent.Width, height = source.Extent.Height;
    if (!HasCompletePixels(source) || color == TextureColorSpace::Auto)
    {
        error = "Mip filtering requires a complete RGBA image and resolved colour space";
        return std::nullopt;
    }
    const bool bNormal = usage == TextureUsage::Normal;
    if (bNormal && (color != TextureColorSpace::Linear || source.SourceChannels < 3))
    {
        error = "Normal mip filtering requires linear RGB/XYZ source channels";
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
                        if (bNormal && channel < 3)
                            value = value * 2 - 1;
                        else if (color == TextureColorSpace::Srgb && channel < 3)
                            value = decode(value);
                        sum[channel] += value * weight;
                    }
                }
            for (auto& value : sum)
                value /= weightSum;
            if (bNormal)
            {
                const auto normal = NormalizeNormal({sum[0], sum[1], sum[2]});
                std::copy(normal.begin(), normal.end(), sum.begin());
            }
            for (size_t channel = 0; channel < 4; ++channel)
            {
                double value = sum[channel];
                if (bNormal && channel < 3)
                    value = value * 0.5 + 0.5;
                else if (color == TextureColorSpace::Srgb && channel < 3)
                    value = encode(value);
                result.Pixels[(size_t(y) * result.Extent.Width + x) * 4 + channel] =
                    static_cast<uint8_t>(std::lround(std::clamp(value, 0.0, 1.0) * 255));
            }
        }
    return result;
}

std::optional<Image> PrepareNormalTextureXY(const Image& source, std::string& error)
{
    error.clear();
    if (!HasCompletePixels(source) || source.SourceChannels < 3)
    {
        error = "Normal XY preparation requires a complete RGB/XYZ source image";
        return std::nullopt;
    }
    Image result = source;
    for (size_t pixel = 0; pixel < result.Pixels.size(); pixel += 4)
    {
        // Bytes 127 and 128 straddle zero; retain that quantization tolerance.
        if (source.Pixels[pixel + 2] < 127)
        {
            error = "Normal XY preparation requires positive-Z tangent-space XYZ";
            return std::nullopt;
        }
        const auto normal =
            NormalizeNormal({source.Pixels[pixel] / 127.5 - 1, source.Pixels[pixel + 1] / 127.5 - 1,
                             source.Pixels[pixel + 2] / 127.5 - 1});
        for (size_t channel = 0; channel < 3; ++channel)
            result.Pixels[pixel + channel] = static_cast<uint8_t>(
                std::lround(std::clamp(normal[channel] * 0.5 + 0.5, 0.0, 1.0) * 255));
    }
    return result;
}

} // namespace Hikari::Asset
