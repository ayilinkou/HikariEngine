#pragma once

#include <filesystem>

#include <nlohmann/json.hpp>

#include <asset/TextureData.h>
#include <rhi/IDevice.h>

// Manual review images, rendered through the production sampler and normal decoder.
nlohmann::json RenderTextureReview(Hikari::Rhi::IDevice& device,
                                   const Hikari::Asset::TextureData& data,
                                   Hikari::Rhi::TextureViewHandle source,
                                   const std::filesystem::path& directory);
