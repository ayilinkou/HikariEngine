#pragma once

#include <future>
#include <queue>
#include <vector>

// A plain shared-queue thread pool: N worker threads all pulling from one
// mutex-protected job queue. Owned directly by SharedQueueJobSystem.
class ThreadPool
{
public:
    explicit ThreadPool(uint32_t threadCount);
    ~ThreadPool();

    ThreadPool(const ThreadPool&) = delete;
    ThreadPool& operator=(const ThreadPool&) = delete;

    uint32_t GetThreadCount() const { return static_cast<uint32_t>(m_Workers.size()); }

    // F&& is a forward reference. This allows the template to take both lvalues
    // and rvalues. std::forward then casts it back into the value category the
    // argument had at the call site. This allows it to avoid copies if the
    // argument is an rvalue but still works with lvalues (with copies).
    template <typename F>
    std::future<void> Submit(F&& f)
    {
        auto task = std::make_shared<std::packaged_task<void()>>(std::forward<F>(f));
        std::future<void> future = task->get_future();
        {
            std::unique_lock lock(m_Mutex);
            m_Jobs.emplace([task] { (*task)(); });
        }
        m_CV.notify_one();
        return future;
    }

private:
    std::vector<std::thread> m_Workers;
    std::queue<std::function<void()>> m_Jobs;
    std::mutex m_Mutex;
    std::condition_variable m_CV;
    bool m_Stopping = false;
};
