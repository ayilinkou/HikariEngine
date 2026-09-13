#pragma once

#include <cstdint>
#include <string>

namespace Hikari::Rhi::D3D12
{
/** Which D3D12 runtime this process is running on. */
struct AgilitySdkInfo
{
    /** The D3D12SDKVersion the loaded D3D12Core.dll exports. */
    uint32_t Version = 0;

    /** Where that D3D12Core.dll was loaded from. */
    std::string CorePath;
};

/**
 * Proves the process runs on the Agility SDK the executable opted into, and
 * throws naming what answered instead when it does not.
 *
 * Needed because every failure mode is silent. An executable that does not
 * export D3D12SDKVersion runs on the runtime Windows ships; a D3D12\ folder left
 * over from an older build holds a core that disagrees with the export. Either
 * way device creation succeeds and every capability query answers — with the
 * wrong runtime's answers, and on the machine this backend is built on, with no
 * debug layer. So the check compares the version the executable exports with the
 * one the loaded D3D12Core.dll exports, rather than trusting the two to agree.
 *
 * Call once D3D12Core.dll is loaded, which the first D3D12CreateDevice
 * guarantees.
 */
AgilitySdkInfo VerifyLoadedAgilitySdk();

/** UTF-16 as Windows APIs return it, converted to the UTF-8 the engine logs. */
std::string Utf8FromWide(const wchar_t* text);
} // namespace Hikari::Rhi::D3D12
