#include <catch2/catch_test_macros.hpp>

#include <SDL3/SDL_init.h>

#include <platform/HeadlessPlatform.h>

// Every case here runs with no SDL video subsystem initialised, which is the
// property that matters: a headless run must never touch the window system, so
// a test that quietly relied on SDL being up would prove nothing. The assertion
// below is what keeps that honest — nothing in this file may initialise it.
TEST_CASE("HeadlessPlatform needs no window system", "[platform][headless]")
{
    REQUIRE_FALSE(SDL_WasInit(SDL_INIT_VIDEO) & SDL_INIT_VIDEO);

    const HeadlessPlatform platform(WindowDesc{.Width = 800u, .Height = 600u});

    CHECK(platform.IsHeadless());
    CHECK(platform.GetFramebufferExtent().Width == 800u);
    CHECK(platform.GetFramebufferExtent().Height == 600u);

    // Null is what the RHI reads as "no surface".
    CHECK(platform.GetNativeWindowHandle() == nullptr);
}

TEST_CASE("HeadlessPlatform falls back to a fixed extent", "[platform][headless]")
{
    // Zero means "you decide" to either platform. SdlPlatform asks the display;
    // this one has none, so it has to answer with a documented constant.
    SECTION("both components zero")
    {
        const HeadlessPlatform platform{WindowDesc{}};
        CHECK(platform.GetFramebufferExtent().Width == 1280u);
        CHECK(platform.GetFramebufferExtent().Height == 720u);
    }

    // --resolution cannot produce this (CommandLine rejects a zero component),
    // but WindowDesc is constructible by hand and a half-specified size is not
    // a size.
    SECTION("one component zero")
    {
        const HeadlessPlatform platform{WindowDesc{.Width = 1920u, .Height = 0u}};
        CHECK(platform.GetFramebufferExtent().Width == 1280u);
        CHECK(platform.GetFramebufferExtent().Height == 720u);
    }
}

TEST_CASE("HeadlessPlatform's window operations are no-ops", "[platform][headless]")
{
    REQUIRE_FALSE(SDL_WasInit(SDL_INIT_VIDEO) & SDL_INIT_VIDEO);

    HeadlessPlatform platform(WindowDesc{.Width = 640u, .Height = 480u});

    // Calling these with no window system up is the whole point: each would be
    // a call into an uninitialised subsystem if it did anything.
    platform.Show();
    platform.SetWindowMode(WindowMode::BorderlessFullscreen);
    platform.SetWindowMode(WindowMode::ExclusiveFullscreen);
    platform.SetRelativeMouseMode(true);
    platform.SetRelativeMouseMode(false);
    platform.WarpMouse(10.f, 20.f);

    // Unmoved by any of it — there is no display for a window mode to mean
    // anything against.
    CHECK(platform.GetFramebufferExtent().Width == 640u);
    CHECK(platform.GetFramebufferExtent().Height == 480u);
    CHECK(platform.IsHeadless());
}
