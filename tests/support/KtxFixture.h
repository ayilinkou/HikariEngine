#pragma once

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <functional>
#include <memory>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <ktx.h>

struct KtxFixture
{
    std::string Bytes;
    std::vector<std::vector<std::byte>> Payloads; // Mip-major, then faces.
};

/** Native blocks with a distinct pattern for each face, mip and byte. */
inline KtxFixture
MakeKtxFixture(uint32_t format = 133, uint32_t width = 12, uint32_t height = 12,
               uint32_t levels = 4, uint32_t faces = 1,
               const std::vector<std::pair<std::string, std::string>>& metadata = {},
               const std::function<void(uint32_t, uint32_t, std::vector<std::byte>&)>& fill = {})
{
    ktxTextureCreateInfo info{};
    info.vkFormat = format;
    info.baseWidth = width;
    info.baseHeight = height;
    info.baseDepth = 1;
    info.numDimensions = 2;
    info.numLevels = levels;
    info.numLayers = 1;
    info.numFaces = faces;
    const auto check = [](KTX_error_code status)
    {
        if (status != KTX_SUCCESS)
            throw std::runtime_error(ktxErrorString(status));
    };
    ktxTexture2* texture = nullptr;
    check(ktxTexture2_Create(&info, KTX_TEXTURE_CREATE_ALLOC_STORAGE, &texture));
    const auto destroy = [](ktxTexture2* value) { ktxTexture_Destroy(ktxTexture(value)); };
    const std::unique_ptr<ktxTexture2, decltype(destroy)> owner(texture, destroy);
    for (const auto& [key, value] : metadata)
        check(ktxHashList_AddKVPair(&texture->kvDataHead, key.c_str(),
                                    static_cast<unsigned int>(value.size() + 1), value.c_str()));
    KtxFixture fixture;
    for (uint32_t level = 0; level < levels; ++level)
        for (uint32_t face = 0; face < faces; ++face)
        {
            std::vector<std::byte> payload(ktxTexture_GetImageSize(ktxTexture(texture), level));
            for (size_t i = 0; i < payload.size(); ++i)
                payload[i] = static_cast<std::byte>((i * 37u + level * 41u + face * 53u) & 255u);
            if (fill)
                fill(level, face, payload);
            check(ktxTexture_SetImageFromMemory(ktxTexture(texture), level, 0, face,
                                                reinterpret_cast<const uint8_t*>(payload.data()),
                                                payload.size()));
            fixture.Payloads.push_back(std::move(payload));
        }
    uint8_t* output = nullptr;
    ktx_size_t size = 0;
    check(ktxTexture2_WriteToMemory(texture, &output, &size));
    const std::unique_ptr<uint8_t, decltype(&std::free)> outputOwner(output, std::free);
    fixture.Bytes.assign(reinterpret_cast<const char*>(output), size);
    return fixture;
}

inline uint64_t KtxFixtureUnsigned(std::string_view bytes, size_t offset, size_t width = 4)
{
    uint64_t value = 0;
    for (size_t i = 0; i < width; ++i)
        value |= static_cast<uint64_t>(static_cast<uint8_t>(bytes.at(offset + i))) << (i * 8u);
    return value;
}

inline void SetKtxFixtureUnsigned(std::string& bytes, size_t offset, uint64_t value,
                                  size_t width = 4)
{
    for (size_t i = 0; i < width; ++i)
        bytes.at(offset + i) = static_cast<char>((value >> (i * 8u)) & 255u);
}
