#pragma once

#include <cstdint>
#include <functional>
#include <future>
#include <vector>

#include <core/IJobSystem.h>

namespace Hikari::Core
{

/**
 * Runs every job immediately, synchronously, on the calling thread — no
 * worker threads, no locking, no concurrency at all. Constructed by main()
 * when passed --jobs 0. Useful as a deterministic baseline: if a scene
 * renders differently under SerialJobSystem than under the default
 * SharedQueueJobSystem, that's a real race in whatever submitted the jobs,
 * not a bug in this class.
 */
class SerialJobSystem : public IJobSystem
{
public:
    std::shared_future<void> Submit(std::function<void()> job) override;
    void Wait() override;
    void ParallelFor(uint32_t count, uint32_t grain,
                     const std::function<void(uint32_t begin, uint32_t end)>& fn) override;
    constexpr uint32_t WorkerCount() const override { return 1u; }

private:
    std::vector<std::shared_future<void>> m_Pending;
};
} // namespace Hikari::Core
