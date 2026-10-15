#include "CookProcess.h"

#include <array>
#include <cstdio>
#include <format>
#include <fstream>
#include <system_error>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <io.h>
#include <windows.h>
#else
#include <cerrno>
#include <fcntl.h>
#include <spawn.h>
#include <sys/wait.h>
#include <unistd.h>
extern char** environ;
#endif

namespace Hikari::Asset
{

std::string CookPathUtf8(const std::filesystem::path& path)
{
    const auto text = path.generic_u8string();
    return {reinterpret_cast<const char*>(text.data()), text.size()};
}

namespace
{
#ifdef _WIN32
std::wstring QuoteArgument(const std::wstring& value)
{
    std::wstring result = L"\"";
    size_t slashes = 0;
    for (const auto ch : value)
    {
        if (ch == L'\\')
            ++slashes;
        else
        {
            result.append(ch == L'"' ? slashes * 2 + 1 : slashes, L'\\');
            result += ch;
            slashes = 0;
        }
    }
    result.append(slashes * 2, L'\\');
    return result + L'"';
}
#endif
} // namespace

bool RunCookProcess(const std::filesystem::path& executable,
                    const std::vector<std::string>& arguments, const std::filesystem::path& log,
                    std::string& error, bool bCheckHpcFallback, bool bInheritStdout)
{
    error.clear();
    std::error_code ec;
    if (!std::filesystem::is_regular_file(executable, ec))
    {
        error = std::format("Pinned CPU encoder was not found at {}. Reconfigure/rebuild to "
                            "provision Compressonator.",
                            CookPathUtf8(executable));
        return false;
    }
    int exitCode = -1;
    if (bInheritStdout)
        std::fflush(stdout);
#ifdef _WIN32
    SECURITY_ATTRIBUTES security{sizeof(SECURITY_ATTRIBUTES), nullptr, TRUE};
    const auto output = CreateFileW(log.c_str(), GENERIC_WRITE, FILE_SHARE_READ, &security,
                                    CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    const auto input = CreateFileW(L"NUL", GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE,
                                   &security, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (output == INVALID_HANDLE_VALUE || input == INVALID_HANDLE_VALUE)
    {
        const auto code = GetLastError();
        if (output != INVALID_HANDLE_VALUE)
            CloseHandle(output);
        if (input != INVALID_HANDLE_VALUE)
            CloseHandle(input);
        error = std::format("Cannot open encoder log/input (Windows error {})", code);
        return false;
    }
    std::wstring command = QuoteArgument(executable.native());
    for (const auto& argument : arguments)
        command +=
            L" " +
            QuoteArgument(
                std::filesystem::path(std::u8string(argument.begin(), argument.end())).native());
    STARTUPINFOW startup{};
    startup.cb = sizeof(startup);
    startup.dwFlags = STARTF_USESTDHANDLES;
    startup.hStdInput = input;
    startup.hStdOutput = output;
    startup.hStdError = output;
    HANDLE inheritedOutput = nullptr;
    if (bInheritStdout)
    {
        const auto parentOutput = reinterpret_cast<HANDLE>(_get_osfhandle(_fileno(stdout)));
        if (!DuplicateHandle(GetCurrentProcess(), parentOutput, GetCurrentProcess(),
                             &inheritedOutput, 0, TRUE, DUPLICATE_SAME_ACCESS))
        {
            const auto code = GetLastError();
            CloseHandle(input);
            CloseHandle(output);
            error = std::format("Cannot inherit runtime cooker output (Windows error {})", code);
            return false;
        }
        startup.hStdOutput = inheritedOutput;
    }
    PROCESS_INFORMATION process{};
    const bool started =
        CreateProcessW(executable.c_str(), command.data(), nullptr, nullptr, TRUE, CREATE_NO_WINDOW,
                       nullptr, nullptr, &startup, &process) != 0;
    const auto startError = GetLastError();
    CloseHandle(input);
    CloseHandle(output);
    if (inheritedOutput)
        CloseHandle(inheritedOutput);
    if (!started)
    {
        error = std::format("Cannot launch CPU encoder (Windows error {})", startError);
        return false;
    }
    DWORD status = 0;
    const bool waited = WaitForSingleObject(process.hProcess, INFINITE) == WAIT_OBJECT_0 &&
                        GetExitCodeProcess(process.hProcess, &status);
    CloseHandle(process.hThread);
    CloseHandle(process.hProcess);
    if (!waited)
    {
        error = "Cannot collect CPU encoder exit status";
        return false;
    }
    exitCode = static_cast<int>(status);
#else
    int output = open(log.c_str(), O_WRONLY | O_CREAT | O_TRUNC | O_CLOEXEC, 0600);
    if (output >= 0 && output < 3)
    {
        const int promoted = fcntl(output, F_DUPFD_CLOEXEC, 3);
        close(output);
        output = promoted;
    }
    if (output < 0)
    {
        error = std::format("Cannot open encoder log: {}",
                            std::error_code(errno, std::generic_category()).message());
        return false;
    }
    std::string application = executable.native();
    std::vector<char*> argv{application.data()};
    for (const auto& argument : arguments)
        argv.push_back(const_cast<char*>(argument.c_str()));
    argv.push_back(nullptr);
    posix_spawn_file_actions_t actions;
    int status = posix_spawn_file_actions_init(&actions);
    if (status)
    {
        close(output);
        error = "Cannot initialize CPU encoder process actions";
        return false;
    }
    status = posix_spawn_file_actions_addopen(&actions, STDIN_FILENO, "/dev/null", O_RDONLY, 0);
    if (!status && !bInheritStdout)
        status = posix_spawn_file_actions_adddup2(&actions, output, STDOUT_FILENO);
    if (!status)
        status = posix_spawn_file_actions_adddup2(&actions, output, STDERR_FILENO);
    if (!status && output != STDOUT_FILENO && output != STDERR_FILENO)
        status = posix_spawn_file_actions_addclose(&actions, output);
    pid_t child = 0;
    if (!status)
        status = posix_spawn(&child, application.c_str(), &actions, nullptr, argv.data(), environ);
    posix_spawn_file_actions_destroy(&actions);
    close(output);
    if (status)
    {
        error = std::format("Cannot launch CPU encoder: {}",
                            std::error_code(status, std::generic_category()).message());
        return false;
    }
    pid_t waited;
    do
        waited = waitpid(child, &status, 0);
    while (waited < 0 && errno == EINTR);
    if (waited < 0 || !WIFEXITED(status))
    {
        error = "CPU encoder did not exit normally";
        return false;
    }
    exitCode = WEXITSTATUS(status);
#endif
    // Keep diagnostics bounded even if a tool produces extensive progress output.
    std::ifstream file(log, std::ios::binary);
    std::string diagnostic;
    bool fallback = false;
    std::array<char, 8192> buffer{};
    while (file)
    {
        file.read(buffer.data(), buffer.size());
        diagnostic.append(buffer.data(), static_cast<size_t>(file.gcount()));
        fallback |= bCheckHpcFallback && diagnostic.find("CPU will be used") != std::string::npos;
        if (diagnostic.size() > 32768)
            diagnostic.erase(0, diagnostic.size() - 32768);
    }
    if (!file.eof() || file.bad())
    {
        error = "Cannot read CPU encoder diagnostic log";
        return false;
    }
    if (fallback)
    {
        error = "CPU encoder fell back to the unverified legacy path: " + diagnostic;
        return false;
    }
    if (exitCode)
    {
        error = std::format("CPU encoder failed (exit {}): {}", exitCode, diagnostic);
        return false;
    }
    return true;
}

} // namespace Hikari::Asset
