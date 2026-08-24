#pragma once

#include <cstdint>
#include <optional>
#include <stdexcept>
#include <string>
#include <vector>

#include <platform/Extent2D.h>

// Thrown when an option's value is missing or malformed. The message names the
// flag, so a caller can log it verbatim.
class CommandLineError : public std::runtime_error
{
public:
    explicit CommandLineError(const std::string& message) : std::runtime_error(message) {}
};

// One token from the command line: the flag as written, plus its value if one
// was supplied. A token that is not a flag at all (a stray positional) is
// reported with itself as the Flag and no Value, so the caller can reject it.
struct CommandLineOption
{
    std::string Flag{};
    std::optional<std::string> Value{};

    // All of these throw CommandLineError, naming this flag, when the value is
    // absent or cannot be converted.
    std::string RequireValue() const;
    int RequireInt() const;
    uint64_t RequireUint64() const;

    // A `<width>x<height>` pair, as a resolution is conventionally written.
    // Zero is rejected in either half: it is the "choose one for me" sentinel
    // elsewhere, so accepting it here would mean a command line that asks for
    // an explicit size and silently gets a chosen one.
    Extent2D RequireExtent2D() const;

    // For flags that take no value. A value is attached to whatever flag
    // precedes it, so without this a boolean flag would silently swallow a
    // stray token instead of rejecting it.
    void RequireNoValue() const;
};

// Splits argv into options, handling both `--flag value` and `--flag=value`.
//
// This knows nothing about which flags an application accepts — it owns the
// tokenising and conversion only. Which flags exist, what they mean, and what
// to do about an unrecognised one stay with the caller.
class CommandLine
{
public:
    CommandLine(int argc, const char* const* argv);

    const std::vector<CommandLineOption>& Options() const { return m_Options; }

private:
    std::vector<CommandLineOption> m_Options;
};
