#pragma once

#include <cstddef>
#include <cstdint>
#include <span>
#include <string>

#include <rhi/Handles.h>
#include <rhi/RhiTypes.h>

namespace Rhi
{
// One subresource's worth of pixels, and which subresource they belong to.
//
// Deliberately not BufferTextureCopyRegion: that names an offset into a staging
// buffer, and the whole point of an upload context is that the caller does not
// have one. The context packs the data and works out the offsets.
struct TextureUpload
{
    std::span<const std::byte> Data;

    TextureAspect Aspect = TextureAspect::Color;
    uint32_t MipLevel = 0u;
    uint32_t BaseLayer = 0u;
    uint32_t LayerCount = 1u;

    // The size of this subresource, not of the texture: a mip is smaller than
    // the level above it, and Data must be exactly the packed size of Extent.
    Extent3D Extent{};
};

struct UploadContextDesc
{
    // How many bytes of staging may be pending before the context submits on
    // its own.
    //
    // A cap is not optional. Batching exists to turn many submissions into one,
    // and the cost of that is holding every staging buffer until the batch is
    // submitted — so without a limit, peak staging memory becomes the total size
    // of everything loaded, which for a large scene is most of a GPU. Flushing
    // on a budget keeps the win (many copies per submission) and bounds the
    // cost.
    //
    // A single upload larger than the budget still goes through; it just becomes
    // a batch of its own.
    uint64_t StagingBudget = 128ull * 1024ull * 1024ull;

    std::string DebugName;
};

// What the context has done, for the log and for anything that wants to assert
// on it. Cumulative over the context's lifetime.
struct UploadStats
{
    // Submissions, which is the number that R11 exists to reduce: it used to be
    // one per resource.
    uint64_t Submits = 0u;

    uint64_t Uploads = 0u;
    uint64_t Bytes = 0u;
};

// Batched staging uploads: many copies recorded into one command list, submitted
// once, waited on once.
//
// It replaces a pattern where every resource submitted its own command buffer
// and then blocked until the whole queue went idle. That is not merely slow —
// draining the queue is the bluntest synchronisation there is, and it only
// looks harmless because nothing else is running while a scene loads.
//
// **Not thread-safe.** One context serves one loading thread. Asset loading is
// single-threaded today; when it stops being, each thread takes its own context
// rather than this growing a lock.
class IUploadContext
{
public:
    virtual ~IUploadContext() = default;

    IUploadContext(const IUploadContext&) = delete;
    IUploadContext& operator=(const IUploadContext&) = delete;
    IUploadContext(IUploadContext&&) = delete;
    IUploadContext& operator=(IUploadContext&&) = delete;

    // Copies `data` into `destination` at `destinationOffset`.
    //
    // `data` is copied into staging before returning, so the caller's storage
    // can go away immediately. `destination` must carry BufferUsage::CopyDst.
    virtual void UploadBuffer(BufferHandle destination, uint64_t destinationOffset,
                              std::span<const std::byte> data) = 0;

    // Fills `destination` from `subresources` and leaves it readable by a
    // shader.
    //
    // Every subresource of one texture goes in a single call, and that is a
    // requirement rather than a convenience. The context transitions a texture
    // from Undefined, which lets the driver discard whatever was in it — so a
    // texture whose subresources were split across two batches would have the
    // first batch's pixels thrown away by the second. Taking them together is
    // what makes that impossible to express.
    //
    // For the same reason `destination` must be freshly created and not yet
    // used: Undefined is the only layout this can transition from.
    virtual void UploadTexture(TextureHandle destination,
                               std::span<const TextureUpload> subresources) = 0;

    // Single-subresource shorthand — the common case, since most textures are
    // one mip of one layer.
    void UploadTexture(TextureHandle destination, const TextureUpload& subresource)
    {
        UploadTexture(destination, std::span<const TextureUpload>(&subresource, 1));
    }

    // Submits everything pending, waits for the GPU to finish it, and frees the
    // staging buffers. Does nothing when nothing is pending.
    //
    // This is the point at which an uploaded resource actually holds its data.
    // Recording an upload does not put anything on the GPU; a caller that hands
    // out a resource before a Flush covering it has returned is handing out
    // uninitialised memory.
    virtual void Flush() = 0;

    virtual const UploadStats& GetStats() const = 0;

protected:
    IUploadContext() = default;
};
} // namespace Rhi
