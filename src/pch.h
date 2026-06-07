#pragma once

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

#include "vulkan/vulkan.hpp"
#include "vulkan/vulkan_raii.hpp"

#include "glm/glm.hpp"
#include "glm/gtc/matrix_transform.hpp"
#include "glm/gtx/hash.hpp"
#include "glm/gtc/quaternion.hpp"

#include "SDL3/SDL.h"
#include "SDL3/SDL_vulkan.h"

#include "imgui.h"
#include "imgui_impl_sdl3.h"
#include "imgui_impl_vulkan.h"

