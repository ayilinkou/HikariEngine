#pragma once

#include <filesystem>
#include <memory>
#include <string>

#include <asset/TextureCookPaths.h>

namespace Hikari::Asset
{

// An OS lock survives no process: abrupt termination releases it automatically.
// Keep the lock file on disk so waiters always synchronize on the same inode.
class CookLock
{
public:
    static std::unique_ptr<CookLock> Acquire(const TextureCookPaths& paths, std::string& error);
    ~CookLock();
    CookLock(const CookLock&) = delete;
    CookLock& operator=(const CookLock&) = delete;

private:
    CookLock() = default;
#ifdef _WIN32
    void* m_Handle = nullptr;
#else
    int m_Handle = -1;
#endif
};

} // namespace Hikari::Asset
