#include <iostream>

#include "TextureCookerCli.h"

int main(int argc, char** argv)
{
    return Hikari::Tools::RunTextureCooker(argc, argv, HIKARI_COOKER_CONTENT_DIR, std::cout,
                                           std::cerr);
}
