#include <platform/CommandLine.h>

#include <format>
#include <string_view>

namespace
{
// Whether `token` reads as the start of a new option rather than a value.
//
// Deliberately tests for a double dash: a single dash must still read as a
// value so that negative numbers (`--camera-preset -1`) keep working.
bool IsFlagStart(std::string_view token)
{
    return token.starts_with("--");
}
} // namespace

std::string CommandLineOption::RequireValue() const
{
    if (!Value)
        throw CommandLineError(std::format("Missing value for {}", Flag));

    return *Value;
}

int CommandLineOption::RequireInt() const
{
    const std::string value = RequireValue();
    try
    {
        size_t consumed = 0;
        const int result = std::stoi(value, &consumed);
        if (consumed != value.size())
            throw std::invalid_argument("trailing characters");

        return result;
    }
    catch (const std::exception&)
    {
        throw CommandLineError(std::format("Invalid integer value for {}: {}", Flag, value));
    }
}

uint64_t CommandLineOption::RequireUint64() const
{
    const std::string value = RequireValue();
    try
    {
        // stoull happily wraps a negative literal round to a huge positive
        // number, so reject the sign before converting.
        if (!value.empty() && value[0] == '-')
            throw std::invalid_argument("negative value");

        size_t consumed = 0;
        const uint64_t result = std::stoull(value, &consumed);
        if (consumed != value.size())
            throw std::invalid_argument("trailing characters");

        return result;
    }
    catch (const std::exception&)
    {
        throw CommandLineError(
            std::format("Invalid unsigned integer value for {}: {}", Flag, value));
    }
}

void CommandLineOption::RequireNoValue() const
{
    if (Value)
        throw CommandLineError(std::format("{} takes no value, but got: {}", Flag, *Value));
}

CommandLine::CommandLine(int argc, const char* const* argv)
{
    // argv[0] is the program name.
    for (int i = 1; i < argc; ++i)
    {
        const std::string_view token = argv[i];

        CommandLineOption option;

        if (const size_t equals = token.find('='); equals != std::string_view::npos)
        {
            option.Flag = token.substr(0, equals);
            option.Value = std::string(token.substr(equals + 1));
        }
        else
        {
            option.Flag = token;

            // Consume the next token as this option's value only if it does
            // not begin a new option.
            if (i + 1 < argc && !IsFlagStart(argv[i + 1]))
            {
                option.Value = argv[i + 1];
                ++i;
            }
        }

        m_Options.push_back(std::move(option));
    }
}
