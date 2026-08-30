#pragma once

#include <filesystem>
#include <string>

namespace Hikari::Rhi
{
struct PipelineCacheDesc
{
    /**
     * The file the cache is seeded from at creation and written back to by
     * Save(). Empty means memory-only: pipelines created in one run still
     * benefit from whatever the driver learned building the ones before them,
     * it just does not survive the process.
     */
    std::filesystem::path Path;

    std::string DebugName;
};

/**
 * The compiled-pipeline cache: an opaque blob the backend adds to as pipelines
 * are created, and that a later run can be seeded with so a pipeline the driver
 * has already compiled comes back without being compiled again.
 *
 * Neutral even though pipeline creation itself is not (plan D8), because the
 * whole of what a caller does with one is create it, hand it to pipeline
 * creation and save it. D3D12's equivalent — a cached PSO blob, or
 * ID3D12PipelineLibrary — answers to the same three verbs.
 *
 * What goes in the blob is entirely the backend's business, including deciding
 * that a file written by another GPU or an updated driver is unusable.
 * Rejecting one is normal rather than an error: the run is correct either way,
 * and only slower.
 *
 * Safe to hand to pipeline creation from several threads at once. Vulkan
 * specifies pipeline creation's use of the cache as internally synchronised,
 * and the backend does not opt out of that.
 */
class IPipelineCache
{
public:
    virtual ~IPipelineCache() = default;

    IPipelineCache(const IPipelineCache&) = delete;
    IPipelineCache& operator=(const IPipelineCache&) = delete;
    IPipelineCache(IPipelineCache&&) = delete;
    IPipelineCache& operator=(IPipelineCache&&) = delete;

    /**
     * Writes the cache's current contents over its file, replacing it in one
     * step so that an interrupted write cannot leave a half-file behind for the
     * next run to read. Returns whether anything was written; a failure is
     * logged and otherwise ignored, because losing a cache costs compile time
     * and nothing else.
     *
     * Deliberately not done by the destructor. This touches the disk and can
     * fail, and a caller that wants to know needs it somewhere it can be told.
     */
    virtual bool Save() = 0;

protected:
    IPipelineCache() = default;
};
} // namespace Hikari::Rhi
