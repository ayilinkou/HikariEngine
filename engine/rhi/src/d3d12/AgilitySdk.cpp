#include "d3d12/AgilitySdk.h"

#include <windows.h>

#include <format>
#include <stdexcept>
#include <string>

namespace Hikari::Rhi::D3D12
{

namespace
{
/** A module's full path, grown until it fits rather than cut at MAX_PATH. */
std::string ModulePath(HMODULE module)
{
    std::wstring path(MAX_PATH, L'\0');
    for (;;)
    {
        const DWORD length =
            GetModuleFileNameW(module, path.data(), static_cast<DWORD>(path.size()));
        if (length == 0)
            return "(unknown path)";

        // A result that fills the buffer exactly is a truncated one.
        if (length < path.size())
        {
            path.resize(length);
            return Utf8FromWide(path.c_str());
        }

        path.resize(path.size() * 2);
    }
}

/** An exported `UINT` data symbol, or nothing when the module does not export it. */
const UINT* FindExportedVersion(HMODULE module)
{
    // GetProcAddress resolves data exports as well as functions; the runtime reads
    // the executable's D3D12SDKVersion the same way.
    return reinterpret_cast<const UINT*>(GetProcAddress(module, "D3D12SDKVersion"));
}
} // namespace

AgilitySdkInfo VerifyLoadedAgilitySdk()
{
    const UINT* pRequested = FindExportedVersion(GetModuleHandleW(nullptr));
    if (pRequested == nullptr)
    {
        throw std::runtime_error(
            "This executable does not export D3D12SDKVersion, so D3D12 runs on the runtime "
            "Windows ships, which has no debug layer on this machine and answers capability "
            "queries for an older API. Give the executable's CMake target "
            "hikari_deploy_d3d12().");
    }

    const HMODULE core = GetModuleHandleW(L"D3D12Core.dll");
    if (core == nullptr)
    {
        throw std::runtime_error(
            "D3D12Core.dll is not loaded, though a D3D12 device has been created: the D3D12 "
            "runtime in this process predates the Agility SDK.");
    }

    const std::string corePath = ModulePath(core);
    const UINT* pLoaded = FindExportedVersion(core);
    if (pLoaded == nullptr || *pLoaded != *pRequested)
    {
        const std::string loaded = pLoaded ? std::to_string(*pLoaded) : "no version";
        throw std::runtime_error(std::format(
            "The D3D12Core.dll this process loaded ({}) exports D3D12SDKVersion {}, but the "
            "executable asked for {}. Either the runtime Windows ships answered, or D3D12\\ "
            "beside the executable holds a copy from another Agility SDK release; rebuilding "
            "redeploys it.",
            corePath, loaded, *pRequested));
    }

    return AgilitySdkInfo{.Version = *pLoaded, .CorePath = corePath};
}

std::wstring WideFromUtf8(const std::string& text)
{
    if (text.empty())
        return {};

    const int length =
        MultiByteToWideChar(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), nullptr, 0);
    std::wstring wide(static_cast<size_t>(length), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), wide.data(),
                        length);
    return wide;
}

std::string Utf8FromWide(const wchar_t* text)
{
    const int length = WideCharToMultiByte(CP_UTF8, 0, text, -1, nullptr, 0, nullptr, nullptr);
    if (length <= 1)
        return {};

    // The count includes the terminator, which std::string supplies itself.
    std::string utf8(static_cast<size_t>(length), '\0');
    WideCharToMultiByte(CP_UTF8, 0, text, -1, utf8.data(), length, nullptr, nullptr);
    utf8.resize(static_cast<size_t>(length) - 1);
    return utf8;
}

} // namespace Hikari::Rhi::D3D12
