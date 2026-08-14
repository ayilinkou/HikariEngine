#pragma once

#include <mutex>

template <typename T>
class ResourceCache
{
public:
    template <typename LoadFn>
    std::shared_ptr<T> Get(const std::string& key, LoadFn&& load)
    {
        std::lock_guard lock(m_Mutex);

        if (auto it = m_Cache.find(key); it != m_Cache.end())
        {
            if (std::shared_ptr<T> sp = it->second.lock())
                return sp;
            m_Cache.erase(it);
        }

        std::shared_ptr<T> sp = load();
        if (sp)
            m_Cache.emplace(key, std::weak_ptr(sp));
        return sp;
    }

    // Remove entries to resources which are expired
    void Purge()
    {
        std::lock_guard lock(m_Mutex);
        std::erase_if(m_Cache, [](const auto& kv) { return kv.second.expired(); });
    }

    size_t LiveCount() const
    {
        std::lock_guard lock(m_Mutex);
        return std::count_if(m_Cache.begin(), m_Cache.end(),
                             [](const auto& kv) { return !kv.second.expired(); });
    }

private:
    std::unordered_map<std::string, std::weak_ptr<T>> m_Cache;
    mutable std::mutex m_Mutex;
};
