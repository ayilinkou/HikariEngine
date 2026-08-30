#include <core/SerialJobSystem.h>

#include <future>

namespace Hikari::Core
{

std::shared_future<void> SerialJobSystem::Submit(std::function<void()> job)
{
    std::promise<void> promise;
    std::shared_future<void> future = promise.get_future().share();

    try
    {
        job();
        promise.set_value();
    }
    catch (...)
    {
        promise.set_exception(std::current_exception());
    }

    m_Pending.push_back(future);
    return future;
}

void SerialJobSystem::Wait()
{
    JobSystemDetail::WaitAndRethrow(m_Pending);
}

void SerialJobSystem::ParallelFor(uint32_t count, uint32_t grain,
                                  const std::function<void(uint32_t, uint32_t)>& fn)
{
    JobSystemDetail::ForEachParallelForChunk(count, grain,
                                             [&](uint32_t begin, uint32_t end) { fn(begin, end); });
}
} // namespace Hikari::Core
