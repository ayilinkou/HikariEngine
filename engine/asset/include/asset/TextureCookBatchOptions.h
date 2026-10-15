#pragma once

#include <cstdint>

namespace Hikari::Asset
{

/** Batch options are operational; texture overrides belong in authored settings. */
struct TextureCookBatchOptions
{
    bool bForce = false;
    uint32_t EncoderThreads = 0;
    double EncodingQuality = 1.0;
};

} // namespace Hikari::Asset
