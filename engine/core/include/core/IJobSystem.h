#pragma once

#include <algorithm>
#include <cstdint>
#include <functional>
#include <future>
#include <vector>

namespace JobSystemDetail
{
// Waits on every future in `futures`, surfacing the first exception thrown by
// any job (if any) only after every future has been waited on — so one
// throwing job never prevents the others from being drained, and callers
// never block forever on a job that already finished (successfully or not).
// Clears `futures` before returning (or throwing), so a second Wait() call
// with nothing new submitted is a clean no-op.
inline void WaitAndRethrow(std::vector<std::shared_future<void>>& futures)
{
    std::exception_ptr firstException = nullptr;
    for (std::shared_future<void>& future : futures)
    {
        try
        {
            future.get();
        }
        catch (...)
        {
            if (!firstException)
                firstException = std::current_exception();
        }
    }
    futures.clear();

    if (firstException)
        std::rethrow_exception(firstException);
}

// Shared chunking rules for ParallelFor, used by every IJobSystem
// implementation so the boundary conditions live in exactly one place:
//   count == 0     -> onChunk is never invoked (no-op).
//   grain > count  -> onChunk is never invoked (no-op); a grain larger than
//                     the whole range is treated as caller error rather than
//                     silently clamped.
//   grain == 0     -> treated as grain == count (one whole-range chunk);
//                     avoids an infinite loop from stepping by 0.
//   otherwise      -> ceil(count / grain) chunks of at most grain elements
//                     each, in order, covering [0, count) with no gaps or
//                     overlap.
template <typename ChunkFn>
void ForEachParallelForChunk(uint32_t count, uint32_t grain, ChunkFn&& onChunk)
{
    if (count == 0u || grain > count)
        return;

    const uint32_t effectiveGrain = (grain == 0u) ? count : grain;

    for (uint32_t begin = 0u; begin < count; begin += effectiveGrain)
    {
        const uint32_t end = std::min(begin + effectiveGrain, count);
        onChunk(begin, end);
    }
}
} // namespace JobSystemDetail

// Common interface for scheduling work either onto worker threads
// (SharedQueueJobSystem) or inline on the calling thread (SerialJobSystem).
class IJobSystem
{
public:
    virtual ~IJobSystem() = default;

    // Schedules `job` to run — immediately and inline for SerialJobSystem, on
    // a worker thread for SharedQueueJobSystem. Never throws synchronously
    // itself — if `job` throws, the exception is captured into the returned
    // future's shared state and only surfaces when that future (or a
    // subsequent Wait()) is consumed.
    virtual std::shared_future<void> Submit(std::function<void()> job) = 0;

    // Blocks until every job submitted via Submit()/ParallelFor() since the
    // last Wait() has completed. If any of them threw, Wait() rethrows the
    // first such exception only after all of them have been waited on.
    virtual void Wait() = 0;

    // Splits [0, count) into chunks and invokes fn(begin, end) once per
    // chunk — see JobSystemDetail::ForEachParallelForChunk for the exact
    // chunking/no-op rules. Blocks until every invoked chunk has completed
    // before returning.
    virtual void ParallelFor(uint32_t count, uint32_t grain,
                             const std::function<void(uint32_t begin, uint32_t end)>& fn) = 0;

    // Number of worker threads this job system executes on, excluding the
    // calling thread. SerialJobSystem always reports 1 (everything runs on
    // the caller).
    virtual uint32_t WorkerCount() const = 0;
};
