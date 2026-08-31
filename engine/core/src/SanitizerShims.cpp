// Interposition shims that exist only to keep a sanitizer build's output
// truthful. Deliberately not part of the Core library — see the OBJECT library
// in engine/core/CMakeLists.txt for why it cannot be, and note that this file
// compiles to nothing at all in a build without AddressSanitizer.

#if defined(__has_feature)
#if __has_feature(address_sanitizer)
#define HIKARI_ADDRESS_SANITIZER 1
#endif
#endif
#if defined(__SANITIZE_ADDRESS__)
#define HIKARI_ADDRESS_SANITIZER 1
#endif

#if defined(HIKARI_ADDRESS_SANITIZER) && !defined(_WIN32)

/**
 * Stops dlopen'd modules being unloaded, by interposing libc's dlclose with one
 * that does nothing.
 *
 * LeakSanitizer runs at exit and reports every block it cannot reach from a
 * root, and a module's globals stop being roots the moment that module is
 * unmapped. The Vulkan loader dlclose()s every ICD and layer inside
 * vkDestroyInstance, so a driver's live per-process allocation is orphaned just
 * before the check runs and is reported as leaked, with a stack of
 * `<unknown module>` frames because the code it points at is gone too. On a
 * Mesa/RADV machine that is 256 bytes allocated under a pthread_once while
 * initialising a physical device; vkCreateInstance plus
 * vkEnumeratePhysicalDevices reproduces it with no engine code involved.
 *
 * Leaving the modules mapped removes the cause instead of hiding the symptom.
 * The memory becomes reachable again, so LeakSanitizer correctly stops counting
 * it, while anything genuinely unreachable — every leak this codebase could
 * have — is still reported exactly as it was. The alternative was a
 * `leak:pthread_once` suppression, rejected because the unloaded module leaves
 * no name to match on anything narrower, and that rule would also swallow a
 * real leak allocated under a std::call_once.
 *
 * Not declared through <dlfcn.h> on purpose: glibc declares dlclose noexcept,
 * and a definition that disagrees about the exception specification is a
 * compile error, while one that agrees is not portable to platforms declaring
 * it without. Interposition matches on the symbol, so the ABI signature here is
 * what has to be right.
 */
extern "C" int dlclose(void* handle)
{
    (void)handle;
    return 0;
}

#endif
