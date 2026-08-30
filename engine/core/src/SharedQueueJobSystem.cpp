#include <core/SharedQueueJobSystem.h>

#include <future>
#include <thread>

namespace Hikari::Core
{

SharedQueueJobSystem::SharedQueueJobSystem() : m_Pool(DefaultThreadCount()) {}

SharedQueueJobSystem::SharedQueueJobSystem(uint32_t threadCount)
    : m_Pool(threadCount > 0u ? threadCount : 1u)
{
}

uint32_t SharedQueueJobSystem::DefaultThreadCount()
{
    const uint32_t hc = std::thread::hardware_concurrency();
    return hc > 1u ? hc - 1u : 1u;
}

std::shared_future<void> SharedQueueJobSystem::Submit(std::function<void()> job)
{
    std::shared_future<void> future = m_Pool.Submit(std::move(job)).share();
    m_Pending.push_back(future);
    return future;
}

void SharedQueueJobSystem::Wait()
{
    JobSystemDetail::WaitAndRethrow(m_Pending);
}

void SharedQueueJobSystem::ParallelFor(uint32_t count, uint32_t grain,
                                       const std::function<void(uint32_t, uint32_t)>& fn)
{
    const size_t pendingCountBefore = m_Pending.size();

    JobSystemDetail::ForEachParallelForChunk(count, grain, [&](uint32_t begin, uint32_t end)
                                             { Submit([&fn, begin, end] { fn(begin, end); }); });

    if (m_Pending.size() > pendingCountBefore)
        Wait();
}

uint32_t SharedQueueJobSystem::WorkerCount() const
{
    return m_Pool.GetThreadCount();
}
} // namespace Hikari::Core
