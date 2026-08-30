#pragma once

#include <cstdint>
#include <functional>
#include <future>
#include <vector>

#include <core/IJobSystem.h>
#include <core/ThreadPool.h>

namespace Hikari::Core
{

/**
 * Wraps a plain shared-queue thread pool (see ThreadPool): one job queue,
 * one mutex, N worker threads pulling from it. Owns its ThreadPool directly
 * — no global/singleton state.
 */
class SharedQueueJobSystem : public IJobSystem
{
public:
    SharedQueueJobSystem();

    explicit SharedQueueJobSystem(uint32_t threadCount);

    std::shared_future<void> Submit(std::function<void()> job) override;
    void Wait() override;
    void ParallelFor(uint32_t count, uint32_t grain,
                     const std::function<void(uint32_t begin, uint32_t end)>& fn) override;
    uint32_t WorkerCount() const override;

private:
    static uint32_t DefaultThreadCount();

    ThreadPool m_Pool;
    std::vector<std::shared_future<void>> m_Pending;
};
} // namespace Hikari::Core
