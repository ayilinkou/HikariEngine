#ifdef DEBUG
#include <signal.h>
#if defined(_MSC_VER)
#define DEBUG_BREAK() __debugbreak()
#elif defined(__unix__)
#define DEBUG_BREAK() raise(SIGTRAP)
#else
#define DEBUG_BREAK()
#endif
#else
#define DEBUG_BREAK()
#endif
