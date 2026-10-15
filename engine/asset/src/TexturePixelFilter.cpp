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

double DecodeFilterValue(uint8_t byte, size_t channel, TextureColorSpace color, TextureUsage usage)
{
    const double value = byte / 255.0;
    if (usage == TextureUsage::Normal && channel < 3)
        return value * 2 - 1;
    if (color == TextureColorSpace::Srgb && channel < 3)
        return value <= 0.04045 ? value / 12.92 : std::pow((value + 0.055) / 1.055, 2.4);
    return value;
}

void StoreFiltered(std::array<double, 4> sum, double weight, TextureColorSpace color,
                   TextureUsage usage, uint8_t* destination)
{
    for (auto& value : sum)
        value /= weight;
    if (usage == TextureUsage::Normal)
    {
        const auto normal = NormalizeNormal({sum[0], sum[1], sum[2]});
        std::copy(normal.begin(), normal.end(), sum.begin());
    }
    for (size_t channel = 0; channel < 4; ++channel)
    {
        double value = sum[channel];
        if (usage == TextureUsage::Normal && channel < 3)
            value = value * 0.5 + 0.5;
        else if (color == TextureColorSpace::Srgb && channel < 3)
            value = value <= 0.0031308 ? value * 12.92 : 1.055 * std::pow(value, 1.0 / 2.4) - 0.055;
        destination[channel] = static_cast<uint8_t>(std::lround(std::clamp(value, 0.0, 1.0) * 255));
    }
}

std::array<double, 3> CubeDirection(size_t face, double s, double t)
{
    // Vulkan "Cube Map Face and Coordinate Selection", inverted for rd rows.
    // https://github.com/KhronosGroup/Vulkan-Docs/blob/main/chapters/textures.adoc
    switch (face)
    {
        case 0:
            return {1, -t, -s};
        case 1:
            return {-1, -t, s};
        case 2:
            return {s, 1, t};
        case 3:
            return {s, -1, -t};
        case 4:
            return {s, -t, 1};
        default:
            return {-s, -t, -1};
    }
}

const uint8_t* CubeTexel(const std::array<Image, 6>& source, size_t face,
                         const std::array<double, 3>& direction)
{
    const auto [x, y, z] = direction;
    double s = 0, t = 0;
    const double major = std::abs(direction[face / 2]);
    switch (face)
    {
        case 0:
            s = -z;
            t = -y;
            break;
        case 1:
            s = z;
            t = -y;
            break;
        case 2:
            s = x;
            t = z;
            break;
        case 3:
            s = x;
            t = -z;
            break;
        case 4:
            s = x;
            t = -y;
            break;
        default:
            s = -x;
            t = -y;
            break;
    }
    const int size = static_cast<int>(source[face].Extent.Width);
    const auto column =
        std::clamp(static_cast<int>(std::floor((s / major * 0.5 + 0.5) * size)), 0, size - 1);
    const auto row =
        std::clamp(static_cast<int>(std::floor((t / major * 0.5 + 0.5) * size)), 0, size - 1);
    return source[face].Pixels.data() +
           (size_t(row) * static_cast<size_t>(size) + static_cast<size_t>(column)) * 4;
}

void AddCubeSample(const std::array<Image, 6>& source, size_t face, int x, int y, double weight,
                   TextureColorSpace color, TextureUsage usage, std::array<double, 4>& sum)
{
    const int size = static_cast<int>(source[face].Extent.Width);
    auto direction =
        CubeDirection(face, (double(x) + 0.5) * 2 / size - 1, (double(y) + 0.5) * 2 / size - 1);
    const auto add = [&](size_t selected, double sampleWeight)
    {
        const auto* pixel = CubeTexel(source, selected, direction);
        for (size_t channel = 0; channel < 4; ++channel)
            sum[channel] += DecodeFilterValue(pixel[channel], channel, color, usage) * sampleWeight;
    };
    if ((x < 0 || x >= size) && (y < 0 || y >= size))
    {
        // There is no unique corner neighbour. Average the three incident
        // face corners in working space, matching seamless sampler edge rules.
        for (auto& component : direction)
            component = component < 0 ? -1 : 1;
        for (size_t axis = 0; axis < 3; ++axis)
            add(axis * 2 + (direction[axis] < 0 ? 1u : 0u), weight / 3);
        return;
    }
    size_t axis = 2;
    if (std::abs(direction[1]) > std::abs(direction[axis]))
        axis = 1;
    if (std::abs(direction[0]) > std::abs(direction[axis]))
        axis = 0;
    add(axis * 2 + (direction[axis] < 0 ? 1u : 0u), weight);
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
                        sum[channel] += DecodeFilterValue(
                                            source.Pixels[(size_t(sy) * width + sx) * 4 + channel],
                                            channel, color, usage) *
                                        weight;
                    }
                }
            StoreFiltered(sum, weightSum, color, usage,
                          result.Pixels.data() + (size_t(y) * result.Extent.Width + x) * 4);
        }
    return result;
}

std::optional<std::array<Image, 6>> FilterCubemapMip(const std::array<Image, 6>& source,
                                                     TextureColorSpace color, TextureUsage usage,
                                                     std::string& error)
{
    error.clear();
    const auto extent = source[0].Extent;
    for (const auto& image : source)
        if (!HasCompletePixels(image) || image.Extent != extent || extent.Width != extent.Height ||
            extent.Width > 16384 || color == TextureColorSpace::Auto ||
            (usage == TextureUsage::Normal &&
             (color != TextureColorSpace::Linear || image.SourceChannels < 3)))
        {
            error = "Cubemap mip filtering requires six equal square RGBA faces and valid "
                    "colour/normal semantics";
            return std::nullopt;
        }
    std::array<Image, 6> result;
    const uint32_t size = std::max(extent.Width / 2, 1u);
    const double scale = double(extent.Width) / size;
    for (size_t face = 0; face < 6; ++face)
    {
        auto& destination = result[face];
        destination.Extent = {size, size};
        destination.SourceChannels = source[face].SourceChannels;
        destination.Pixels.resize(size_t(size) * size * 4);
        for (uint32_t y = 0; y < size; ++y)
            for (uint32_t x = 0; x < size; ++x)
            {
                const double cx = (double(x) + 0.5) * scale - 0.5;
                const double cy = (double(y) + 0.5) * scale - 0.5;
                std::array<double, 4> sum{};
                double weightSum = 0;
                for (int sy = static_cast<int>(std::ceil(cy - scale));
                     sy <= static_cast<int>(std::floor(cy + scale)); ++sy)
                    for (int sx = static_cast<int>(std::ceil(cx - scale));
                         sx <= static_cast<int>(std::floor(cx + scale)); ++sx)
                    {
                        const double weight = std::max(0.0, 1 - std::abs(sx - cx) / scale) *
                                              std::max(0.0, 1 - std::abs(sy - cy) / scale);
                        if (weight == 0)
                            continue;
                        AddCubeSample(source, face, sx, sy, weight, color, usage, sum);
                        weightSum += weight;
                    }
                StoreFiltered(sum, weightSum, color, usage,
                              destination.Pixels.data() + (size_t(y) * size + x) * 4);
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
