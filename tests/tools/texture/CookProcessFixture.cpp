#include <cstdio>
#include <iostream>
#include <string>

#ifdef _WIN32
#include <fcntl.h>
#include <io.h>
#endif

int main(int argc, char** argv)
{
#ifdef _WIN32
    _setmode(_fileno(stdout), _O_BINARY);
#endif
    const std::string mode = argc > 1 ? argv[1] : "";
    if (mode == "echo")
    {
        for (int i = 2; i < argc; ++i)
            std::cout << argv[i] << '\n';
        return 0;
    }
    if (mode == "fallback")
    {
        std::cout << "CPU will be used\n" << std::string(70000, 'x');
        return 0;
    }
    std::cerr << "intentional child process failure";
    return 17;
}
