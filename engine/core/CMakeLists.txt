engine_module(Core SOURCES src/ThreadPool.cpp src/SerialJobSystem.cpp
              src/SharedQueueJobSystem.cpp)

# An OBJECT library, and deliberately not part of Core, because the shim inside
# has to reach the final binary and an archive member would not. A static
# library is searched rather than merged: the linker extracts a member only to
# resolve a symbol something already left undefined, and nothing in this
# codebase calls dlclose — the Vulkan loader does, from inside libvulkan. The
# member would be passed over, the shim would silently not exist, and the
# sanitizer noise it removes would quietly come back. An OBJECT library's files
# go straight onto the consuming executable's link line, so they are always
# there.
#
# Object files do not propagate through a transitively linked target, so every
# executable has to link this one directly: the app in the root
# CMakeLists.txt, and every test binary through engine_test().
add_library(SanitizerShims OBJECT src/SanitizerShims.cpp)
engine_set_warnings(SanitizerShims)
