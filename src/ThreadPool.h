#pragma once

class ThreadPool
{
public:
    static ThreadPool* Get() { return s_Instance; }

    static void Init();
    static void Shutdown();

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
    ThreadPool(uint32_t threadCount);
    ~ThreadPool();

private:
    std::vector<std::thread> m_Workers;
    std::queue<std::function<void()>> m_Jobs;
    std::mutex m_Mutex;
    std::condition_variable m_CV;
    bool m_Stopping = false;

    inline static ThreadPool* s_Instance = nullptr;
};
