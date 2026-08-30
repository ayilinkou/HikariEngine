#pragma once

#include <utility>

#include <rhi/IDevice.h>

namespace Hikari::Rhi
{
// Move-only ownership of one device resource, for the places where a handle's
// manual Destroy is a liability rather than a feature.
//
// Handles are the ABI (plan D2) because they cost nothing to pass and hide the
// backend for free, but they are worse than RAII for a scope-local resource:
// every early return and every throw has to remember to release. This puts the
// release back where the compiler does it, without putting a backend type in a
// public header. Long-lived resources whose lifetime is obvious can hold the
// bare handle instead — this is opt-in sugar, not a replacement.
//
// Destruction goes through IDevice::Destroy, which is overloaded per handle
// type, so one template covers every resource. The device must outlive the
// wrapper; in practice that means declaring the device *before* anything
// holding one, since members are destroyed in reverse declaration order.
template <typename HandleType>
class UniqueHandle
{
public:
    UniqueHandle() = default;

    UniqueHandle(IDevice& device, HandleType handle) : m_pDevice(&device), m_Handle(handle) {}

    ~UniqueHandle() { Reset(); }

    UniqueHandle(const UniqueHandle&) = delete;
    UniqueHandle& operator=(const UniqueHandle&) = delete;

    UniqueHandle(UniqueHandle&& other) noexcept
        : m_pDevice(other.m_pDevice), m_Handle(other.m_Handle)
    {
        other.Disown();
    }

    UniqueHandle& operator=(UniqueHandle&& other) noexcept
    {
        if (this != &other)
        {
            Reset();
            m_pDevice = other.m_pDevice;
            m_Handle = other.m_Handle;
            other.Disown();
        }
        return *this;
    }

    // The handle itself. Deliberately explicit rather than an implicit
    // conversion: a handle copied out of here outlives nothing on its own, and
    // storing one somewhere longer-lived is exactly the mistake the wrapper is
    // meant to make visible.
    HandleType Get() const { return m_Handle; }

    bool IsValid() const { return m_Handle.IsValid(); }

    // Destroys what is held, if anything, and becomes empty.
    void Reset()
    {
        if (m_pDevice != nullptr && m_Handle.IsValid())
            m_pDevice->Destroy(m_Handle);

        Disown();
    }

    // Gives up ownership without destroying, for handing the resource to
    // something that will own it instead.
    [[nodiscard]] HandleType Release()
    {
        const HandleType handle = m_Handle;
        Disown();
        return handle;
    }

private:
    void Disown()
    {
        m_pDevice = nullptr;
        m_Handle = HandleType{};
    }

    IDevice* m_pDevice = nullptr;
    HandleType m_Handle{};
};
} // namespace Hikari::Rhi
