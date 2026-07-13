#pragma once

#include <array>
#include <vector>
#include <memory>
#include <cstdint>
#include <stdexcept>
#include <string>
#include <format>
#include <unordered_map>
#include <functional>
#include <filesystem>
#include <iostream>
#include <algorithm>
#include <chrono>
#include <cmath>
#include <limits>
#include <csignal>
#include <thread>
#include <future>
#include <queue>

#if defined(__APPLE__)
// Enable Vulkan Metal-surface (VK_EXT_metal_surface) support on macOS so we
// can create the swapchain surface directly through our Vulkan loader instead
// of relying on SDL_Vulkan_CreateSurface (which is unreliable on macOS).
#define VK_USE_PLATFORM_METAL_EXT
// VK_KHR_portability_subset (required by MoltenVK on the logical device) is a
// "beta" extension; its vk:: wrappers only exist when this is defined.
#define VK_ENABLE_BETA_EXTENSIONS
#include "SDL3/SDL_metal.h"
#endif

#include "vulkan/vulkan.hpp"
#include "vulkan/vulkan_raii.hpp"

#include "glm/glm.hpp"
#include "glm/gtc/matrix_transform.hpp"
#include "glm/gtx/hash.hpp"
#include "glm/gtc/quaternion.hpp"
#include "glm/gtc/type_ptr.hpp"

#include "SDL3/SDL.h"
#include "SDL3/SDL_vulkan.h"

#include "imgui.h"
#include "imgui_impl_sdl3.h"
#include "imgui_impl_vulkan.h"

