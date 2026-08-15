#pragma once

#include <cstdio>
#include <filesystem>
#include <fstream>
#include <functional>
#include <sstream>
#include <string>

#if defined(_WIN32)
#include <io.h>
#define ENGINE_TEST_DUP _dup
#define ENGINE_TEST_DUP2 _dup2
#define ENGINE_TEST_CLOSE _close
#define ENGINE_TEST_FILENO _fileno
#else
#include <unistd.h>
#define ENGINE_TEST_DUP dup
#define ENGINE_TEST_DUP2 dup2
#define ENGINE_TEST_CLOSE close
#define ENGINE_TEST_FILENO fileno
#endif

// Temporarily redirects `stream` (stdout/stderr) to a scratch file, runs `fn`,
// restores the stream, and returns everything that was written. Used to test
// LogMsg's output without depending on how the test runner itself captures
// console output.
inline std::string CaptureStream(FILE* stream, const std::function<void()>& fn)
{
    std::fflush(stream);
    int fd = ENGINE_TEST_FILENO(stream);
    int savedFd = ENGINE_TEST_DUP(fd);

    std::string tempPath =
        (std::filesystem::temp_directory_path() / "engine_log_capture.txt").string();

    FILE* redirected = std::freopen(tempPath.c_str(), "w", stream);
    if (!redirected)
        throw std::runtime_error("CaptureStream: freopen failed to redirect stream");

    fn();

    std::fflush(stream);
    ENGINE_TEST_DUP2(savedFd, fd);
    ENGINE_TEST_CLOSE(savedFd);

    std::ifstream in(tempPath);
    std::stringstream ss;
    ss << in.rdbuf();
    return ss.str();
}
