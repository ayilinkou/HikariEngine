#include <platform/SdlPlatform.h>

#include <format>

#include <SDL3/SDL.h>
#include <SDL3/SDL_vulkan.h>

#include <core/Log.h>

namespace
{
constexpr LogCategory LogSDL("SDL");
}

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

    SDL_SetWindowFullscreen(m_pWindow, false);
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
