#include "ThreadPool.h"

#if defined(__linux__)
#include <pthread.h>
#elif defined(_WIN32)
#include <processthreadsapi.h>
#include <windows.h>
#endif

// Self-naming, call from inside the thread's own lambda
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

                        // stop waiting if the instance is shutting down or if
                        // there are jobs to be completed
                        m_CV.wait(lock, [this]
                                  { return m_Stopping || !m_Jobs.empty(); });

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

    // wakes all threads
    m_CV.notify_all();

    for (auto& thread : m_Workers)
    {
        thread.join();
    }
}

void ThreadPool::Init()
{
    if (s_Instance)
        throw std::runtime_error("Attempting to initialise ThreadPool instance "
                                 "when instance is already initialised!");

    // number of physical cores minus one (main thread)
    s_Instance = new ThreadPool(std::thread::hardware_concurrency() - 1);
}

void ThreadPool::Shutdown()
{
    if (!s_Instance)
        throw std::runtime_error("Attempting to shutdown ThreadPool instance "
                                 "which is already null!");

    delete s_Instance;
    s_Instance = nullptr;
}
