#include <platform/CommandLine.h>

#include <format>
#include <limits>
#include <string_view>

namespace Hikari::Platform
{

namespace
{
/**
 * Whether `token` reads as the start of a new option rather than a value.
 *
 * Deliberately tests for a double dash: a single dash must still read as a
 * value so that negative numbers (`--camera-preset -1`) keep working.
 */
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

Extent2D CommandLineOption::RequireExtent2D() const
{
    const std::string value = RequireValue();

    // Both spellings, because "1600X900" is what a user who typed it in a
    // launcher config is as likely to write as "1600x900".
    const size_t separator = value.find_first_of("xX");

    const auto parseComponent = [&value](size_t begin, size_t end) -> uint32_t
    {
        const std::string component = value.substr(begin, end - begin);

        // stoul takes a leading sign and stoull wraps a negative round to a
        // huge positive, so reject the sign before converting.
        if (component.empty() || component[0] == '-' || component[0] == '+')
            throw std::invalid_argument("not a plain unsigned number");

        size_t consumed = 0;
        const unsigned long long parsed = std::stoull(component, &consumed);
        if (consumed != component.size() || parsed == 0 ||
            parsed > std::numeric_limits<uint32_t>::max())
            throw std::invalid_argument("out of range");

        return static_cast<uint32_t>(parsed);
    };

    try
    {
        if (separator == std::string::npos)
            throw std::invalid_argument("no separator");

        return {parseComponent(0, separator), parseComponent(separator + 1, value.size())};
    }
    catch (const std::exception&)
    {
        throw CommandLineError(
            std::format("Invalid <width>x<height> value for {}: {}", Flag, value));
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
} // namespace Hikari::Platform
