#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <span>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

#include <nlohmann/json.hpp>

#include <asset/ImageReader.h>
#include <asset/ImageWriter.h>
#include <asset/KtxReader.h>
#include <rhi/Backend.h>
#include <rhi/DeviceDesc.h>
#include <rhi/IDevice.h>
#include <rhi/UniqueHandle.h>

#include "TextureBlockCodec.h"
#include "TextureReview.h"

using namespace Hikari;
using Json = nlohmann::json;
using Clock = std::chrono::steady_clock;

namespace
{
double Elapsed(Clock::time_point start)
{
    return std::chrono::duration<double, std::milli>(Clock::now() - start).count();
}

std::pair<Asset::TextureCompression, const char*> Compression(Rhi::Format format)
{
    switch (format)
    {
        case Rhi::Format::BC1Unorm:
        case Rhi::Format::BC1Srgb:
            return {Asset::TextureCompression::BC1, "BC1"};
        case Rhi::Format::BC5Unorm:
            return {Asset::TextureCompression::BC5, "BC5"};
        case Rhi::Format::BC7Unorm:
        case Rhi::Format::BC7Srgb:
            return {Asset::TextureCompression::BC7, "BC7"};
        default:
            throw std::runtime_error("This review probe supports BC1, BC5 UNORM and BC7");
    }
}

std::array<double, 3> Normal(const uint8_t* pixel)
{
    std::array<double, 3> result{pixel[0] / 127.5 - 1, pixel[1] / 127.5 - 1, pixel[2] / 127.5 - 1};
    const double length =
        std::sqrt(result[0] * result[0] + result[1] * result[1] + result[2] * result[2]);
    if (length <= 1e-12)
        return {0, 0, 1};
    for (auto& value : result)
        value /= length;
    return result;
}

Json Previews(const Asset::TextureData& data, const std::filesystem::path& source,
              const std::filesystem::path& directory)
{
    std::filesystem::create_directories(directory);
    std::vector<std::filesystem::path> references;
    if (data.Faces == 6)
    {
        std::ifstream file(source);
        const auto descriptor = Json::parse(file);
        for (const char* face :
             {"positiveX", "negativeX", "positiveY", "negativeY", "positiveZ", "negativeZ"})
            references.push_back(source.parent_path() /
                                 descriptor.at("faces").at(face).get<std::string>());
    }
    else
        references.push_back(source);
    Json previews = Json::array();
    double squaredError = 0, angularError = 0, maximumAngle = 0;
    uint64_t basePixels = 0;
    const bool normal = data.Metadata.Usage == Asset::TextureUsage::Normal;
    for (const auto& sub : data.Subresources)
    {
        if (sub.MipLevel != 0 && sub.MipLevel != 2 && sub.MipLevel != 4)
            continue;
        const auto bytes = std::span(data.Data).subspan(sub.Offset, sub.Size);
        std::string error;
        auto decoded = Asset::DecodeTextureBlocks(
            {reinterpret_cast<const uint8_t*>(bytes.data()), bytes.size()},
            {sub.Extent.Width, sub.Extent.Height}, Compression(data.Format).first,
            directory / "decoder", HIKARI_PROBE_ENCODER, error);
        if (!decoded)
            throw std::runtime_error(error);
        if (normal &&
            data.Metadata.NormalEncoding == Asset::TextureNormalEncoding::XyUnormPositiveZ)
            for (size_t offset = 0; offset < decoded->Pixels.size(); offset += 4)
            {
                const double x = decoded->Pixels[offset] / 127.5 - 1;
                const double y = decoded->Pixels[offset + 1] / 127.5 - 1;
                const double z = std::sqrt(std::max(0.0, 1 - x * x - y * y));
                const double length = std::sqrt(x * x + y * y + z * z);
                const std::array<double, 3> xyz{x / length, y / length, z / length};
                for (size_t channel = 0; channel < 3; ++channel)
                    decoded->Pixels[offset + channel] = static_cast<uint8_t>(
                        std::clamp(std::lround((xyz[channel] * 0.5 + 0.5) * 255), 0l, 255l));
            }
        const auto filename =
            "face-" + std::to_string(sub.Face) + "-mip-" + std::to_string(sub.MipLevel) + ".png";
        if (!Asset::WritePng(decoded->Pixels, decoded->Extent, (directory / filename).string()))
            throw std::runtime_error("Could not write preview " + filename);
        previews.push_back({{"face", sub.Face},
                            {"mip", sub.MipLevel},
                            {"file", filename},
                            {"width", sub.Extent.Width},
                            {"height", sub.Extent.Height}});
        if (sub.MipLevel == 0)
        {
            const auto original = Asset::ReadTextureImage(references.at(sub.Face).string(), error);
            if (!original || original->Extent != decoded->Extent)
                throw std::runtime_error(error.empty() ? "Reference dimensions disagree" : error);
            for (size_t offset = 0; offset < decoded->Pixels.size(); offset += 4)
            {
                for (size_t channel = 0; channel < 3; ++channel)
                {
                    const double delta = double(decoded->Pixels[offset + channel]) -
                                         original->Pixels[offset + channel];
                    squaredError += delta * delta;
                }
                if (normal)
                {
                    const auto a = Normal(decoded->Pixels.data() + offset);
                    const auto b = Normal(original->Pixels.data() + offset);
                    const double angle =
                        std::acos(std::clamp(a[0] * b[0] + a[1] * b[1] + a[2] * b[2], -1.0, 1.0)) *
                        180.0 / std::acos(-1.0);
                    angularError += angle;
                    maximumAngle = std::max(maximumAngle, angle);
                }
                ++basePixels;
            }
        }
    }
    const double mse = squaredError / (double(basePixels) * 3);
    Json result{
        {"images", previews},
        {"baseRgbRmse", std::sqrt(mse)},
        {"baseRgbPsnrDb", mse > 0 ? Json(10 * std::log10(255 * 255 / mse)) : Json(nullptr)}};
    if (normal)
    {
        result["baseNormalMeanAngleDegrees"] = angularError / double(basePixels);
        result["baseNormalMaxAngleDegrees"] = maximumAngle;
    }
    return result;
}
} // namespace

int main(int argc, char** argv)
{
    try
    {
        const bool renderOnly = argc == 7 && std::string(argv[6]) == "--render-only";
        if (argc != 5 && argc != 6 && !renderOnly)
            throw std::runtime_error(
                "Usage: HikariTextureLoadProbe <ktx2> <report.json> <source-image-or-cubemap.json> "
                "<preview-directory> [Vulkan|D3D12] [--render-only]");
        const auto start = Clock::now();
        std::string error;
        const auto data = Asset::ReadKtxTexture(argv[1], error);
        if (!data)
            throw std::runtime_error(error);
        const double cpuLoad = Elapsed(start);
        Json report{{"cpuLoadMs", cpuLoad},
                    {"width", data->Extent.Width},
                    {"height", data->Extent.Height},
                    {"mipLevels", data->MipLevels},
                    {"faces", data->Faces},
                    {"payloadBytes", data->Data.size()},
                    {"fileBytes", std::filesystem::file_size(argv[1])},
                    {"format", Compression(data->Format).second}};
        Rhi::Diagnostics diagnostics;
        {
            Rhi::DeviceDesc desc;
            desc.ApplicationName = "Hikari texture quality review";
            desc.Requirements.bPresent = false;
            desc.bEnableValidation = true;
            desc.GpuBasedValidation = Rhi::GpuBasedValidation::Full;
            desc.pDiagnostics = &diagnostics;
            if (argc >= 6)
            {
                const auto backend = Rhi::BackendFromString(argv[5]);
                if (!backend)
                    throw std::runtime_error("Unknown backend");
                desc.Backend = *backend;
            }
            const auto deviceStart = Clock::now();
            const auto device = Rhi::CreateDevice(desc);
            report["deviceCreationMs"] = Elapsed(deviceStart);
            report["backend"] = Rhi::ToString(device->GetInfo().Backend);
            report["gpu"] = device->GetInfo().Gpu;
            report["driver"] = device->GetInfo().Driver;
            const auto uploadStart = Clock::now();
            const Rhi::UniqueHandle<Rhi::TextureHandle> texture(
                *device, device->CreateTexture(
                             {.Format = data->Format,
                              .Extent = {data->Extent.Width, data->Extent.Height, 1u},
                              .MipLevels = data->MipLevels,
                              .ArrayLayers = data->Faces,
                              .Usage = Rhi::TextureUsage::Sampled | Rhi::TextureUsage::CopyDst,
                              .bCubeCompatible = data->Faces == 6,
                              .DebugName = "Quality review texture"}));
            const Rhi::UniqueHandle<Rhi::TextureViewHandle> view(
                *device, device->CreateTextureView(
                             {.Texture = texture.Get(),
                              .Dimension = data->Faces == 6 ? Rhi::TextureViewDimension::TextureCube
                                                            : Rhi::TextureViewDimension::Texture2D,
                              .MipCount = data->MipLevels,
                              .LayerCount = data->Faces,
                              .DebugName = "Quality review view"}));
            const auto context = device->CreateUploadContext({});
            std::vector<Rhi::TextureUpload> uploads;
            for (const auto& sub : data->Subresources)
                uploads.push_back({.Data = std::span(data->Data).subspan(sub.Offset, sub.Size),
                                   .MipLevel = sub.MipLevel,
                                   .BaseLayer = sub.Face,
                                   .Extent = sub.Extent});
            context->UploadTexture(texture.Get(), uploads);
            context->Flush();
            report["gpuCreateUploadFlushMs"] = Elapsed(uploadStart);
            report["uploadSubmissions"] = context->GetStats().Submits;
            // Rendering and decoding stay outside the load timing.
            report["renderedImages"] = RenderTextureReview(
                *device, *data, view.Get(), std::filesystem::path(argv[4]) / "rendered");
        }
        report["validationErrors"] = diagnostics.ErrorCount();
        report["validationWarnings"] = diagnostics.WarningCount();
        if (diagnostics.ErrorCount())
            throw std::runtime_error("GPU load produced validation errors");
        if (!renderOnly)
            report["previews"] = Previews(*data, argv[3], argv[4]);
        std::ofstream output(argv[2]);
        output << report.dump(2) << '\n';
        if (!output)
            throw std::runtime_error("Could not write load report");
        return 0;
    }
    catch (const std::exception& exception)
    {
        std::cerr << exception.what() << '\n';
        return 1;
    }
}
