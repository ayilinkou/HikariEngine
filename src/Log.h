#pragma once

#include <SDL3/SDL_messagebox.h>
struct LogCategory
{
    std::string_view Name;
    constexpr explicit LogCategory(std::string_view name) : Name(name) {}
};

enum class LogSeverity : uint8_t
{
    Info,
    Warning,
    Error
};

constexpr bool operator<(LogSeverity a, LogSeverity b)
{
    return static_cast<uint8_t>(a) < static_cast<uint8_t>(b);
}

namespace Log
{
// ANSI escape codes. \033 is ESC.
inline constexpr std::string_view Reset = "\033[0m";
inline constexpr std::string_view White = "\033[37m";
inline constexpr std::string_view Yellow = "\033[33m";
inline constexpr std::string_view Red = "\033[31m";

constexpr std::string_view SeverityColor(LogSeverity severity)
{
    switch (severity)
    {
    case LogSeverity::Info:
        return White;
    case LogSeverity::Warning:
        return Yellow;
    case LogSeverity::Error:
        return Red;
    }
    return Reset;
}

constexpr std::string_view SeverityTag(LogSeverity severity)
{
    switch (severity)
    {
    case LogSeverity::Info:
        return "";
    case LogSeverity::Warning:
        return "[WARNING]";
    case LogSeverity::Error:
        return "[ERROR]";
    }
    return "[?]";
}

inline LogSeverity g_MinSeverity{LogSeverity::Info};
} // namespace Log

// If manually entering a log category, it must be constructed into a
// LogCategory object with a string literal.
template <typename... Args>
void LogMsg(LogSeverity severity, const LogCategory& cat,
            std::format_string<Args...> fmt, Args&&... args)
{
    if (severity < Log::g_MinSeverity)
        return;

    const auto color = Log::SeverityColor(severity);
    const auto tag = Log::SeverityTag(severity);

    FILE* stream =
        (severity == LogSeverity::Warning || severity == LogSeverity::Error)
            ? stderr
            : stdout;

    std::fprintf(stream, "%.*s%.*s%.*s [%.*s] ", static_cast<int>(color.size()),
                 color.data(), static_cast<int>(tag.size()), tag.data(),
                 static_cast<int>(Log::Reset.size()), Log::Reset.data(),
                 static_cast<int>(cat.Name.size()), cat.Name.data());

    std::fputs(std::vformat(fmt.get(), std::make_format_args(args...)).c_str(),
               stream);
    std::fputc('\n', stream);
}

// Also prints a log message if a LogCategory is given
inline void ShowMessageBox(const char* title, const char* message,
                           LogSeverity severity,
                           LogCategory category = LogCategory(""))
{
    SDL_MessageBoxFlags flags{0};
    switch (severity)
    {
    case LogSeverity::Info:
        flags |= SDL_MESSAGEBOX_INFORMATION;
        break;
    case LogSeverity::Warning:
        flags |= SDL_MESSAGEBOX_WARNING;
        break;
    case LogSeverity::Error:
        flags |= SDL_MESSAGEBOX_ERROR;
        break;
    }

    if (!category.Name.empty())
        LogMsg(severity, category, "{}", message);

    SDL_ShowSimpleMessageBox(flags, title, message, nullptr);
}
