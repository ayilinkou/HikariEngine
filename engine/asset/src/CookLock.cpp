#include "CookLock.h"

#include <format>
#include <system_error>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#else
#include <cerrno>
#include <fcntl.h>
#include <sys/file.h>
#include <unistd.h>
#endif

namespace Hikari::Asset
{

std::unique_ptr<CookLock> CookLock::Acquire(const TextureCookPaths& paths, std::string& error)
{
    const auto& path = paths.LockPath;
    std::error_code ec;
    std::filesystem::create_directories(path.parent_path(), ec);
    if (ec)
    {
        error = std::format(
            "{}: Cannot create cooking lock directory: {}. Choose a writable --cooked-root.",
            path.parent_path().string(), ec.message());
        return nullptr;
    }
    std::unique_ptr<CookLock> lock(new CookLock);
#ifdef _WIN32
    lock->m_Handle =
        CreateFileW(path.c_str(), GENERIC_READ | GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE,
                    nullptr, OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (lock->m_Handle == INVALID_HANDLE_VALUE)
    {
        ec = std::error_code(static_cast<int>(GetLastError()), std::system_category());
        lock->m_Handle = nullptr;
    }
    else
    {
        OVERLAPPED offset{};
        if (!LockFileEx(lock->m_Handle, LOCKFILE_EXCLUSIVE_LOCK, 0, 1, 0, &offset))
            ec = std::error_code(static_cast<int>(GetLastError()), std::system_category());
    }
#else
    lock->m_Handle = open(path.c_str(), O_CREAT | O_RDWR | O_CLOEXEC, 0666);
    if (lock->m_Handle < 0)
        ec = std::error_code(errno, std::generic_category());
    else
    {
        int status;
        do
            status = flock(lock->m_Handle, LOCK_EX);
        while (status < 0 && errno == EINTR);
        if (status < 0)
            ec = std::error_code(errno, std::generic_category());
    }
#endif
    if (ec)
    {
        error = std::format("{}: Cannot acquire cooking lock: {}", path.string(), ec.message());
        return nullptr;
    }
    return lock;
}

CookLock::~CookLock()
{
#ifdef _WIN32
    if (m_Handle)
        CloseHandle(m_Handle);
#else
    if (m_Handle >= 0)
        close(m_Handle);
#endif
}

} // namespace Hikari::Asset
