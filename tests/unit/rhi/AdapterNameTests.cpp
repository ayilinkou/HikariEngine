#include <catch2/catch_test_macros.hpp>

#include <string>
#include <vector>

#include "AdapterName.h"

using Hikari::Rhi::AdapterNameMatches;
using Hikari::Rhi::DescribeAdapters;

TEST_CASE("An empty request matches every adapter", "[rhi][adapter]")
{
    // Leaving --gpu alone has to keep each backend's own rule, which means the
    // filter must not exclude anything.
    CHECK(AdapterNameMatches("Radeon RX 580 Series", ""));
    CHECK(AdapterNameMatches("", ""));
}

TEST_CASE("A request matches part of a name, in any case", "[rhi][adapter]")
{
    // The case that made the flag necessary: WARP beside a GPU, asked for by the
    // part of its name DXGI reports.
    CHECK(AdapterNameMatches("Microsoft Basic Render Driver", "Basic Render"));
    CHECK(AdapterNameMatches("Microsoft Basic Render Driver", "basic render"));
    CHECK(AdapterNameMatches("Radeon RX 580 Series", "rx 580"));
    CHECK(AdapterNameMatches("llvmpipe (LLVM 19.1.7, 256 bits)", "LLVMPIPE"));
}

TEST_CASE("A request that is not in the name does not match", "[rhi][adapter]")
{
    CHECK_FALSE(AdapterNameMatches("Radeon RX 580 Series", "Basic Render"));
    CHECK_FALSE(AdapterNameMatches("RX 580", "Radeon RX 580 Series"));
    CHECK_FALSE(AdapterNameMatches("", "anything"));
}

TEST_CASE("A refusal lists every adapter considered, or says there were none", "[rhi][adapter]")
{
    const std::vector<std::string> considered = {"First — reason one", "Second — reason two"};
    CHECK(DescribeAdapters(considered) == "\n  First — reason one\n  Second — reason two");

    // A machine with no adapter at all still gets a message that says so, rather
    // than a refusal that ends at its colon.
    CHECK(DescribeAdapters({}) == "\n  (none)");
}
