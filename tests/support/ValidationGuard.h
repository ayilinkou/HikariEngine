#pragma once

#include <cstdint>
#include <exception>
#include <string>
#include <vector>

#include <catch2/catch_test_macros.hpp>

#include <rhi/Diagnostics.h>
#include <rhi/IDevice.h>

namespace RhiTest
{
// Fails the surrounding test case if the driver reported a validation error
// while this was alive, and says which.
//
// A guard rather than an assertion the test writes itself, because a validation
// error is not what any of these cases is looking for and every one of them
// would otherwise have to remember to check. The failure it catches is the one
// that otherwise passes: a copy recorded against the wrong layout still returns
// the right bytes on the driver it was written on.
//
// Only errors fail. Warnings are counted and reported but tolerated, because
// the best-practices layer objects to work the fallback configurations perform
// deliberately — a device pretending not to have VK_KHR_maintenance9 hands
// resources over that a real one would not have to, and is told so.
class ValidationGuard
{
public:
    explicit ValidationGuard(Rhi::IDevice& device) : m_Diagnostics(device.GetDiagnostics())
    {
        // The device is shared across the binary, so the counters carry whatever
        // the previous case produced. Resetting is what makes the check below
        // about this test rather than about every test that ran before it.
        m_Diagnostics.Reset();
    }

    ~ValidationGuard()
    {
        // A test already failing or skipping is unwinding through here, and its
        // real result is the one worth reporting. Adding a second failure on the
        // way out would bury it.
        if (std::uncaught_exceptions() > 0)
            return;

        const uint64_t errors = m_Diagnostics.ErrorCount();
        if (errors == 0u)
            return;

        std::string message = std::to_string(errors) + " validation error(s):";
        for (const std::string& recent : m_Diagnostics.RecentMessages())
            message += "\n  " + recent;

        // FAIL_CHECK rather than FAIL: this is a destructor, and the throwing
        // form would terminate the process the moment a case failed for two
        // reasons at once.
        FAIL_CHECK(message);
    }

    ValidationGuard(const ValidationGuard&) = delete;
    ValidationGuard& operator=(const ValidationGuard&) = delete;
    ValidationGuard(ValidationGuard&&) = delete;
    ValidationGuard& operator=(ValidationGuard&&) = delete;

    uint64_t ErrorCount() const { return m_Diagnostics.ErrorCount(); }
    uint64_t WarningCount() const { return m_Diagnostics.WarningCount(); }

private:
    Rhi::Diagnostics& m_Diagnostics;
};
} // namespace RhiTest
