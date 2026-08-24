#include <platform/SdlPlatform.h>

#include <format>

#include <SDL3/SDL.h>
#include <SDL3/SDL_vulkan.h>

#include <core/Log.h>

namespace
{
constexpr LogCategory LogSDL("SDL");

const char* WindowModeName(WindowMode mode)
{
    switch (mode)
    {
        case WindowMode::Windowed:
            return "windowed";
        case WindowMode::BorderlessFullscreen:
            return "borderless fullscreen";
        case WindowMode::ExclusiveFullscreen:
            return "exclusive fullscreen";
    }

    return "unknown";
}
} // namespace

SDLException::SDLException(const std::string& message)
    : std::runtime_error(std::format("{} {}", message, SDL_GetError()))
{
}

SdlPlatform::SdlPlatform(const WindowDesc& desc)
{
    if (!SDL_Init(SDL_INIT_VIDEO))
        throw SDLException("Failed to initialise SDL!");

    LogMsg(LogSeverity::Info, LogSDL, "SDL video driver: {}", SDL_GetCurrentVideoDriver());

    if (!SDL_Vulkan_LoadLibrary(nullptr))
        throw SDLException("Failed to load Vulkan library!");

    // Hidden so the window isn't visible while initialisation is taking place;
    // Show() reveals it.
    SDL_WindowFlags flags = SDL_WINDOW_VULKAN | SDL_WINDOW_HIDDEN;
    if (desc.bResizable)
        flags |= SDL_WINDOW_RESIZABLE;
    if (desc.bBorderless)
        flags |= SDL_WINDOW_BORDERLESS;

    m_pWindow = SDL_CreateWindow(desc.Title.c_str(), static_cast<int>(desc.Width),
                                 static_cast<int>(desc.Height), flags);
    if (m_pWindow == nullptr)
        throw SDLException("Failed to create window!");
}

SdlPlatform::~SdlPlatform()
{
    if (m_pWindow)
    {
        SDL_WarpMouseInWindow(m_pWindow, 0.f, 0.f);
        SDL_SetWindowRelativeMouseMode(m_pWindow, false);
        SDL_DestroyWindow(m_pWindow);
    }

    SDL_Vulkan_UnloadLibrary();
    SDL_Quit();
}

Extent2D SdlPlatform::GetFramebufferExtent() const
{
    // Pixels rather than screen coordinates — the two differ on high-DPI
    // displays, and the swapchain is sized in pixels.
    int width = 0;
    int height = 0;
    SDL_GetWindowSizeInPixels(m_pWindow, &width, &height);

    return {static_cast<uint32_t>(width), static_cast<uint32_t>(height)};
}

void SdlPlatform::Show()
{
    SDL_ShowWindow(m_pWindow);
}

void SdlPlatform::SetWindowMode(WindowMode mode)
{
    // The fullscreen mode is a property of the window that SDL remembers
    // whether or not it is fullscreen right now, so it has to be chosen before
    // the fullscreen request rather than passed with it. A null mode is what
    // asks for borderless desktop fullscreen.
    if (mode == WindowMode::ExclusiveFullscreen)
    {
        const SDL_DisplayID display = SDL_GetDisplayForWindow(m_pWindow);

        // Matched against the desktop mode rather than one of our choosing:
        // the point of this path is to change how the window is presented, not
        // to change the user's resolution behind their back. High-density
        // modes are included so a HiDPI display gives us its native pixels,
        // which is what the swapchain is sized in.
        const SDL_DisplayMode* pDesktopMode = SDL_GetDesktopDisplayMode(display);

        SDL_DisplayMode closest{};
        const bool bHaveMode =
            pDesktopMode != nullptr &&
            SDL_GetClosestFullscreenDisplayMode(display, pDesktopMode->w, pDesktopMode->h,
                                                pDesktopMode->refresh_rate, true, &closest);

        // A display that advertises no fullscreen modes cannot do this at all.
        // Falling back to borderless beats leaving the window windowed: the
        // user asked to go fullscreen, and that part is still possible.
        if (!bHaveMode)
        {
            LogMsg(LogSeverity::Warning, LogSDL,
                   "No exclusive fullscreen mode available ({}); using borderless instead.",
                   SDL_GetError());
            mode = WindowMode::BorderlessFullscreen;
        }
        else if (!SDL_SetWindowFullscreenMode(m_pWindow, &closest))
        {
            LogMsg(LogSeverity::Warning, LogSDL,
                   "Failed to select fullscreen display mode ({}); using borderless instead.",
                   SDL_GetError());
            mode = WindowMode::BorderlessFullscreen;
        }
    }

    if (mode != WindowMode::ExclusiveFullscreen && !SDL_SetWindowFullscreenMode(m_pWindow, nullptr))
    {
        LogMsg(LogSeverity::Warning, LogSDL, "Failed to clear the fullscreen display mode: {}",
               SDL_GetError());
    }

    // Deliberately not followed by SDL_SyncWindow: the transition is
    // asynchronous on some window systems, and the size change arrives as an
    // ordinary resize event that the renderer already handles. Blocking here
    // would stall the frame loop for the length of a compositor animation.
    if (!SDL_SetWindowFullscreen(m_pWindow, mode != WindowMode::Windowed))
    {
        LogMsg(LogSeverity::Warning, LogSDL, "Failed to change the fullscreen state: {}",
               SDL_GetError());
        return;
    }

    LogMsg(LogSeverity::Info, LogSDL, "Requested window mode: {}", WindowModeName(mode));
}

void SdlPlatform::SetRelativeMouseMode(bool bEnabled)
{
    SDL_SetWindowRelativeMouseMode(m_pWindow, bEnabled);
}

void SdlPlatform::WarpMouse(float x, float y)
{
    SDL_WarpMouseInWindow(m_pWindow, x, y);
}

void* SdlPlatform::GetNativeWindowHandle() const
{
    return m_pWindow;
}

void SdlPlatform::ShowErrorMessageBox(const char* title, const char* message)
{
    SDL_LogError(SDL_LOG_CATEGORY_ERROR, "SDL error: %s", message);
    SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, title, message, nullptr);
}
