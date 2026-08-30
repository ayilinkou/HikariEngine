#include <platform/SdlPlatform.h>

#include <format>

#include <SDL3/SDL.h>

#include <core/Log.h>

namespace Hikari::Platform
{

namespace
{
constexpr Core::LogCategory LogSDL("SDL");

/**
 * How much of the display a window with no size of its own takes. A window the
 * size of the display looks like borderless fullscreen but is not one: its
 * title bar and its edges sit off-screen or under the taskbar, so it can be
 * neither moved nor resized. Three quarters leaves all of them reachable.
 */
constexpr float kDefaultDisplayFraction = 0.75f;

/**
 * Only used when the display cannot be queried at all. Small enough to fit on
 * any display that could plausibly be attached.
 */
constexpr Core::Extent2D kFallbackWindowSize{1280u, 720u};

Core::Extent2D DefaultWindowSize(SDL_DisplayID display)
{
    // Screen coordinates, which is what SDL_CreateWindow sizes in — the pixel
    // count differs from this on a high-density display, and is the swapchain's
    // business rather than the window's.
    SDL_Rect bounds{};
    if (!SDL_GetDisplayBounds(display, &bounds) || bounds.w <= 0 || bounds.h <= 0)
    {
        Core::LogMsg(Core::LogSeverity::Warning, LogSDL,
                     "Failed to query the display bounds ({}); using {}x{}.", SDL_GetError(),
                     kFallbackWindowSize.Width, kFallbackWindowSize.Height);
        return kFallbackWindowSize;
    }

    return {static_cast<uint32_t>(static_cast<float>(bounds.w) * kDefaultDisplayFraction),
            static_cast<uint32_t>(static_cast<float>(bounds.h) * kDefaultDisplayFraction)};
}

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

    Core::LogMsg(Core::LogSeverity::Info, LogSDL, "SDL video driver: {}",
                 SDL_GetCurrentVideoDriver());

    // No SDL_Vulkan_LoadLibrary here: SDL_CreateWindow does it. SDL_video.h
    // documents that a window created with SDL_WINDOW_VULKAN calls
    // SDL_Vulkan_LoadLibrary itself, and that SDL_DestroyWindow calls the
    // matching unload — and this window always carries that flag.
    //
    // Hidden so the window isn't visible while initialisation is taking place;
    // Show() reveals it.
    SDL_WindowFlags flags = SDL_WINDOW_VULKAN | SDL_WINDOW_HIDDEN;
    if (desc.bResizable)
        flags |= SDL_WINDOW_RESIZABLE;
    if (desc.bBorderless)
        flags |= SDL_WINDOW_BORDERLESS;

    const SDL_DisplayID display = SDL_GetPrimaryDisplay();
    Core::Extent2D size{desc.Width, desc.Height};
    if (size.Width == 0u || size.Height == 0u)
        size = DefaultWindowSize(display);

    m_pWindow = SDL_CreateWindow(desc.Title.c_str(), static_cast<int>(size.Width),
                                 static_cast<int>(size.Height), flags);
    // Names the Vulkan driver, because this is where a machine without one
    // fails: SDL_video.h says "if SDL_WINDOW_VULKAN is specified and there
    // isn't a working Vulkan driver, SDL_CreateWindow() will fail, because
    // SDL_Vulkan_LoadLibrary() will fail". Blaming the window would send a
    // reader looking at window flags and display bounds instead. SDLException
    // appends SDL_GetError(), which names the real cause.
    if (m_pWindow == nullptr)
        throw SDLException("Failed to create the window — SDL loads the Vulkan library as part of "
                           "this, so a missing or broken driver fails here:");

    // SDL_CreateWindow takes no position, so without this the window manager
    // places the window — on Windows, cascaded down from the top left. Setting
    // it while the window is still hidden means it is never seen anywhere else.
    // SDL centres within the display's usable bounds, so the taskbar does not
    // push the title bar off-screen.
    //
    // Logged rather than warned about because a Wayland client cannot position
    // itself at all: SDL_SetWindowPosition fails by design there, and the
    // compositor's placement is the correct outcome, not a fallback.
    if (!SDL_SetWindowPosition(m_pWindow, SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED))
        Core::LogMsg(Core::LogSeverity::Info, LogSDL,
                     "The window system placed the window itself: {}", SDL_GetError());

    Core::LogMsg(Core::LogSeverity::Info, LogSDL, "Created a {}x{} window", size.Width,
                 size.Height);
}

SdlPlatform::~SdlPlatform()
{
    if (m_pWindow)
    {
        SDL_WarpMouseInWindow(m_pWindow, 0.f, 0.f);
        SDL_SetWindowRelativeMouseMode(m_pWindow, false);
        SDL_DestroyWindow(m_pWindow);
    }

    SDL_Quit();
}

Core::Extent2D SdlPlatform::GetFramebufferExtent() const
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
            Core::LogMsg(Core::LogSeverity::Warning, LogSDL,
                         "No exclusive fullscreen mode available ({}); using borderless instead.",
                         SDL_GetError());
            mode = WindowMode::BorderlessFullscreen;
        }
        else if (!SDL_SetWindowFullscreenMode(m_pWindow, &closest))
        {
            Core::LogMsg(Core::LogSeverity::Warning, LogSDL,
                         "Failed to select fullscreen display mode ({}); using borderless instead.",
                         SDL_GetError());
            mode = WindowMode::BorderlessFullscreen;
        }
    }

    if (mode != WindowMode::ExclusiveFullscreen && !SDL_SetWindowFullscreenMode(m_pWindow, nullptr))
    {
        Core::LogMsg(Core::LogSeverity::Warning, LogSDL,
                     "Failed to clear the fullscreen display mode: {}", SDL_GetError());
    }

    // Deliberately not followed by SDL_SyncWindow: the transition is
    // asynchronous on some window systems, and the size change arrives as an
    // ordinary resize event that the renderer already handles. Blocking here
    // would stall the frame loop for the length of a compositor animation.
    if (!SDL_SetWindowFullscreen(m_pWindow, mode != WindowMode::Windowed))
    {
        Core::LogMsg(Core::LogSeverity::Warning, LogSDL,
                     "Failed to change the fullscreen state: {}", SDL_GetError());
        return;
    }

    Core::LogMsg(Core::LogSeverity::Info, LogSDL, "Requested window mode: {}",
                 WindowModeName(mode));
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
} // namespace Hikari::Platform
