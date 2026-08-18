#pragma once

#include <cstdint>

#include "vulkan/vulkan_raii.hpp"

// Attaches a human-readable name to a Vulkan object, so that validation
// messages and capture tools name it instead of printing a handle value.
//
// Extracted from src/Utility.h ahead of R4, because it is the only thing the
// pipeline builders needed from that header and they could not move into this
// module while depending on a header in src/ — the app depends on the engine,
// never the reverse. R4 dissolves the rest of Utility.h alongside this.
//
// Compiles to nothing unless DEBUG is defined, which is why the RHI target
// defines it PUBLIC in Debug configurations. That is load-bearing rather than
// tidy: this is a template, so its body is instantiated in whichever
// translation unit calls it. If the module's own sources disagreed with the
// application's about DEBUG, the same instantiation would have two different
// definitions — an ODR violation, with the linker free to keep either. The
// visible symptom would be debug names going missing from some objects and not
// others, which is a miserable thing to chase.
template <typename T>
inline void SetVkDebugName([[maybe_unused]] vk::raii::Device& device, [[maybe_unused]] T handle,
                           [[maybe_unused]] vk::ObjectType objectType,
                           [[maybe_unused]] const char* name)
{
#ifdef DEBUG
    // convert vk:: C++ types into C types
    // eg. vk::Image -> VkImage
    using CType = decltype(static_cast<typename T::CType>(handle));

    vk::DebugUtilsObjectNameInfoEXT nameInfo{
        .objectType = objectType,
        .objectHandle = reinterpret_cast<uint64_t>(static_cast<CType>(handle)),
        .pObjectName = name};
    device.setDebugUtilsObjectNameEXT(nameInfo);
#endif
}
