#include <catch2/catch_test_macros.hpp>

#include <string>
#include <vector>

#include <platform/CommandLine.h>

using namespace Hikari::Platform;

namespace
{
// Builds a CommandLine from the argument tokens, supplying the argv[0] the
// parser expects to skip.
CommandLine Parse(std::vector<const char*> args)
{
    args.insert(args.begin(), "HikariEngine");
    return CommandLine(static_cast<int>(args.size()), args.data());
}
} // namespace

TEST_CASE("A separated value is attached to its flag", "[CommandLine]")
{
    const CommandLine commandLine = Parse({"--frames", "30"});

    REQUIRE(commandLine.Options().size() == 1);
    REQUIRE(commandLine.Options()[0].Flag == "--frames");
    REQUIRE(commandLine.Options()[0].Value == "30");
}

TEST_CASE("An =-joined value is attached to its flag", "[CommandLine]")
{
    const CommandLine commandLine = Parse({"--frames=30"});

    REQUIRE(commandLine.Options().size() == 1);
    REQUIRE(commandLine.Options()[0].Flag == "--frames");
    REQUIRE(commandLine.Options()[0].Value == "30");
}

TEST_CASE("A flag with no value has no value", "[CommandLine]")
{
    const CommandLine commandLine = Parse({"--fixed-dt"});

    REQUIRE(commandLine.Options().size() == 1);
    REQUIRE(commandLine.Options()[0].Flag == "--fixed-dt");
    REQUIRE_FALSE(commandLine.Options()[0].Value.has_value());
}

TEST_CASE("A following flag is not consumed as a value", "[CommandLine]")
{
    const CommandLine commandLine = Parse({"--screenshot", "--report"});

    REQUIRE(commandLine.Options().size() == 2);
    REQUIRE_FALSE(commandLine.Options()[0].Value.has_value());
    REQUIRE_FALSE(commandLine.Options()[1].Value.has_value());
}

TEST_CASE("A negative number is a value, not a flag", "[CommandLine]")
{
    // The value test is for a double dash precisely so this keeps working.
    const CommandLine commandLine = Parse({"--camera-preset", "-1"});

    REQUIRE(commandLine.Options().size() == 1);
    REQUIRE(commandLine.Options()[0].RequireInt() == -1);
}

TEST_CASE("An =-joined empty value is present but empty", "[CommandLine]")
{
    const CommandLine commandLine = Parse({"--scene="});

    REQUIRE(commandLine.Options().size() == 1);
    REQUIRE(commandLine.Options()[0].Value == "");
}

TEST_CASE("Options are reported in the order given", "[CommandLine]")
{
    const CommandLine commandLine =
        Parse({"--fixed-dt", "--frames", "30", "--camera-preset=1", "--report"});

    REQUIRE(commandLine.Options().size() == 4);
    REQUIRE(commandLine.Options()[0].Flag == "--fixed-dt");
    REQUIRE(commandLine.Options()[1].Flag == "--frames");
    REQUIRE(commandLine.Options()[2].Flag == "--camera-preset");
    REQUIRE(commandLine.Options()[3].Flag == "--report");
}

TEST_CASE("No arguments produces no options", "[CommandLine]")
{
    REQUIRE(Parse({}).Options().empty());
}

TEST_CASE("An unrecognised token is surfaced so the caller can reject it", "[CommandLine]")
{
    // The parser has no notion of which flags exist; it reports what it saw
    // and the application decides that --nonsense is unknown.
    const CommandLine commandLine = Parse({"--nonsense"});

    REQUIRE(commandLine.Options().size() == 1);
    REQUIRE(commandLine.Options()[0].Flag == "--nonsense");
}

TEST_CASE("A bare positional token is surfaced too", "[CommandLine]")
{
    const CommandLine commandLine = Parse({"stray"});

    REQUIRE(commandLine.Options().size() == 1);
    REQUIRE(commandLine.Options()[0].Flag == "stray");
}

TEST_CASE("A missing value is an error", "[CommandLine]")
{
    const CommandLine commandLine = Parse({"--content"});
    const CommandLineOption& option = commandLine.Options()[0];

    REQUIRE_THROWS_AS(option.RequireValue(), CommandLineError);
    REQUIRE_THROWS_AS(option.RequireInt(), CommandLineError);
    REQUIRE_THROWS_AS(option.RequireUint64(), CommandLineError);
}

TEST_CASE("A non-numeric integer value is an error", "[CommandLine]")
{
    REQUIRE_THROWS_AS(Parse({"--jobs", "abc"}).Options()[0].RequireInt(), CommandLineError);
}

TEST_CASE("An integer value with trailing characters is an error", "[CommandLine]")
{
    REQUIRE_THROWS_AS(Parse({"--jobs", "4x"}).Options()[0].RequireInt(), CommandLineError);
}

TEST_CASE("A valid integer converts", "[CommandLine]")
{
    REQUIRE(Parse({"--jobs", "4"}).Options()[0].RequireInt() == 4);
}

TEST_CASE("A negative unsigned value is an error rather than wrapping round", "[CommandLine]")
{
    // stoull would silently turn -1 into 18446744073709551615.
    REQUIRE_THROWS_AS(Parse({"--frames", "-1"}).Options()[0].RequireUint64(), CommandLineError);
}

TEST_CASE("An unsigned value with trailing characters is an error", "[CommandLine]")
{
    REQUIRE_THROWS_AS(Parse({"--frames", "30frames"}).Options()[0].RequireUint64(),
                      CommandLineError);
}

TEST_CASE("A valid unsigned value converts", "[CommandLine]")
{
    REQUIRE(Parse({"--frames", "1000"}).Options()[0].RequireUint64() == 1000u);
}

TEST_CASE("A resolution converts, in either spelling of the separator", "[CommandLine]")
{
    const Extent2D lower = Parse({"--resolution", "1600x900"}).Options()[0].RequireExtent2D();
    REQUIRE(lower.Width == 1600u);
    REQUIRE(lower.Height == 900u);

    const Extent2D upper = Parse({"--resolution=2560X1440"}).Options()[0].RequireExtent2D();
    REQUIRE(upper.Width == 2560u);
    REQUIRE(upper.Height == 1440u);
}

TEST_CASE("A resolution missing a half is an error", "[CommandLine]")
{
    REQUIRE_THROWS_AS(Parse({"--resolution", "1600"}).Options()[0].RequireExtent2D(),
                      CommandLineError);
    REQUIRE_THROWS_AS(Parse({"--resolution", "1600x"}).Options()[0].RequireExtent2D(),
                      CommandLineError);
    REQUIRE_THROWS_AS(Parse({"--resolution", "x900"}).Options()[0].RequireExtent2D(),
                      CommandLineError);
}

TEST_CASE("A zero-sized resolution is an error rather than a request to choose", "[CommandLine]")
{
    REQUIRE_THROWS_AS(Parse({"--resolution", "0x900"}).Options()[0].RequireExtent2D(),
                      CommandLineError);
    REQUIRE_THROWS_AS(Parse({"--resolution", "1600x0"}).Options()[0].RequireExtent2D(),
                      CommandLineError);
}

TEST_CASE("A malformed resolution is an error", "[CommandLine]")
{
    // A signed half would otherwise wrap round, and a third component would be
    // silently dropped by a parser that stopped at the first separator.
    REQUIRE_THROWS_AS(Parse({"--resolution", "-1600x900"}).Options()[0].RequireExtent2D(),
                      CommandLineError);
    REQUIRE_THROWS_AS(Parse({"--resolution", "1600x-900"}).Options()[0].RequireExtent2D(),
                      CommandLineError);
    REQUIRE_THROWS_AS(Parse({"--resolution", "1600x900x2"}).Options()[0].RequireExtent2D(),
                      CommandLineError);
    REQUIRE_THROWS_AS(Parse({"--resolution", "wide"}).Options()[0].RequireExtent2D(),
                      CommandLineError);
    REQUIRE_THROWS_AS(Parse({"--resolution", "5000000000x900"}).Options()[0].RequireExtent2D(),
                      CommandLineError);
}

TEST_CASE("A resolution with no value at all is an error", "[CommandLine]")
{
    REQUIRE_THROWS_AS(Parse({"--resolution"}).Options()[0].RequireExtent2D(), CommandLineError);
}

TEST_CASE("A value on a flag that takes none is an error", "[CommandLine]")
{
    REQUIRE_THROWS_AS(Parse({"--fixed-dt=yes"}).Options()[0].RequireNoValue(), CommandLineError);
    REQUIRE_NOTHROW(Parse({"--fixed-dt"}).Options()[0].RequireNoValue());
}

TEST_CASE("Error messages name the offending flag", "[CommandLine]")
{
    try
    {
        Parse({"--jobs", "abc"}).Options()[0].RequireInt();
        FAIL("expected CommandLineError");
    }
    catch (const CommandLineError& e)
    {
        const std::string message = e.what();
        REQUIRE(message.find("--jobs") != std::string::npos);
        REQUIRE(message.find("abc") != std::string::npos);
    }
}
