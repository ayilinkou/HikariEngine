#include <catch2/catch_test_macros.hpp>
#include <catch2/reporters/catch_reporter_event_listener.hpp>
#include <catch2/reporters/catch_reporter_registrars.hpp>

#include "RhiTestFixture.h"

namespace
{
/**
 * Destroys the shared devices while main() is still running.
 *
 * They cannot be left to the static that owns them: exit() runs static
 * destructors in an order that is only defined within a translation unit, and
 * the validation layer's own globals go with them. On this machine
 * vkDestroyDevice then abort()s inside the layer, after every test has already
 * passed — a crash that would be blamed on whatever ran last rather than on
 * when the teardown happened.
 *
 * A listener rather than an explicit call at the end of each case, because the
 * whole point of the shared device is that a case does not own it.
 */
class RhiDeviceListener final : public Catch::EventListenerBase
{
public:
    using Catch::EventListenerBase::EventListenerBase;

    void testRunEnded(const Catch::TestRunStats&) override { RhiTest::ShutDownDevices(); }
};
} // namespace

CATCH_REGISTER_LISTENER(RhiDeviceListener)
