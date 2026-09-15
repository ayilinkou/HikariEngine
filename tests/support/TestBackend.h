#pragma once

#include <algorithm>
#include <optional>
#include <span>
#include <string>

#include <catch2/catch_test_macros.hpp>

#include <rhi/Backend.h>

#include "TestEnvironment.h"

/**
 * Which backend and adapter a test process runs on, as CTest's registration of the
 * binary says. Shared by the GPU suite's fixture and by the scene suite, which build
 * their devices differently but have to be asked the same question the same way.
 */
namespace RhiTest
{
/**
 * The backend this process's tests run on: HIKARI_TEST_BACKEND, or Vulkan when it
 * is unset.
 *
 * A name the build does not contain fails rather than skips. A registration
 * asking for a backend and getting skips would read as that backend passing —
 * the green run of nothing that HIKARI_TESTS_REQUIRE_DEVICE exists to prevent,
 * reached from a different direction.
 */
inline Hikari::Rhi::Backend TestBackend()
{
    const std::string requested = TestEnvironment::Value("HIKARI_TEST_BACKEND");
    if (requested.empty())
        return Hikari::Rhi::Backend::Vulkan;

    const std::optional<Hikari::Rhi::Backend> backend = Hikari::Rhi::BackendFromString(requested);
    const std::span<const Hikari::Rhi::Backend> available = Hikari::Rhi::AvailableBackends();
    if (!backend || std::ranges::find(available, *backend) == available.end())
        FAIL("HIKARI_TEST_BACKEND names a backend this build does not contain: " + requested);

    return *backend;
}

/**
 * The adapter, as --gpu names one for the apps: HIKARI_TEST_GPU, or empty for the
 * backend's own choice. What runs D3D12's suites on WARP on a machine that also has
 * a GPU, which would otherwise always win.
 */
inline std::string TestGpu()
{
    return TestEnvironment::Value("HIKARI_TEST_GPU");
}
} // namespace RhiTest
