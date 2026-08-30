#pragma once

#include <stdexcept>
#include <string>

#include <platform/IPlatform.h>

struct SDL_Window;

namespace Hikari::Platform
{

/** Thrown when an SDL call fails; appends SDL_GetError() to the message. */
class SDLException : public std::runtime_error
{
public:
    explicit SDLException(const std::string& message);
};

/**
 * Owns SDL init, the Vulkan loader and the window — brought up in that order
 * by the constructor, torn down in reverse by the destructor.
 *
 * Because the destructor calls SDL_Vulkan_UnloadLibrary(), an SdlPlatform must
 * outlive every object holding a Vulkan handle.
 */
class SdlPlatform final : public IPlatform
{
public:
    explicit SdlPlatform(const WindowDesc& desc);
    ~SdlPlatform() override;

    SdlPlatform(const SdlPlatform&) = delete;
    SdlPlatform& operator=(const SdlPlatform&) = delete;

    bool IsHeadless() const override { return false; }
    Extent2D GetFramebufferExtent() const override;
    void Show() override;
    void SetWindowMode(WindowMode mode) override;
    void SetRelativeMouseMode(bool bEnabled) override;
    void WarpMouse(float x, float y) override;
    void* GetNativeWindowHandle() const override;

    /**
     * Static because it is called from the handler for "constructing an
     * SdlPlatform failed", where there is no instance to call it on.
     */
    static void ShowErrorMessageBox(const char* title, const char* message);

private:
    SDL_Window* m_pWindow = nullptr;
};
} // namespace Hikari::Platform
