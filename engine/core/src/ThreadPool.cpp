#include <core/ThreadPool.h>

#if defined(__linux__)
#include <pthread.h>
#elif defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#endif
#include <core/Log.h>

namespace Hikari::Core
{

constexpr LogCategory LogThreadPool("Thread Pool");

/** Self-naming, call from inside the thread's own lambda */
inline void SetCurrentThreadName(const std::string& name)
{
#if defined(__linux__)
    pthread_setname_np(pthread_self(), name.c_str());
#elif defined(_WIN32)
    std::wstring wname(name.begin(), name.end());
    SetThreadDescription(GetCurrentThread(), wname.c_str());
#endif
}

ThreadPool::ThreadPool(uint32_t threadCount)
{
    LogMsg(LogSeverity::Info, LogThreadPool, "Initialising ThreadPool with {} threads...",
           threadCount);

    for (uint32_t i = 0u; i < threadCount; i++)
    {
        m_Workers.emplace_back(
            [this, i]
            {
                SetCurrentThreadName("WorkerThread-" + std::to_string(i));
                while (true)
                {
                    std::function<void()> job;
                    {
                        std::unique_lock lock(m_Mutex);
                        m_CV.wait(lock, [this] { return m_Stopping || !m_Jobs.empty(); });

                        if (m_Stopping && m_Jobs.empty())
                            return;

                        job = std::move(m_Jobs.front());
                        m_Jobs.pop();
                    }
                    job();
                }
            });
    }
}

ThreadPool::~ThreadPool()
{
    {
        std::unique_lock lock(m_Mutex);
        m_Stopping = true;
    }

    m_CV.notify_all();

    for (auto& thread : m_Workers)
    {
        thread.join();
    }
}
} // namespace Hikari::Core
