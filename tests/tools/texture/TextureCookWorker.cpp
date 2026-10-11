#include <chrono>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <string>
#include <thread>
#include <vector>

#include <asset/TextureCooker.h>

#include "CookHash.h"
#include "CookLock.h"
#include "CookPublication.h"
#include "CookRecord.h"
#include "TextureCookKtxFixture.h"

using namespace Hikari::Asset;

// A separate process exercises kernel locking and genuine abrupt termination,
// without mocking filesystem operations or adding fault hooks to the service.
int main(int argc, char** argv)
{
    if (argc != 4)
        return 2;
    const TextureCooker cooker(argv[1]);
    const TextureCookRequest request{.Source = argv[2]};
    const std::string mode = argv[3];
    auto encoder = TextureCookFixtureEncoder(37);
    if (mode == "cook" || mode == "interrupt-encoding")
    {
        encoder.Encode = [&](const auto& prepared, const auto& output, auto& error)
        {
            auto attempts = prepared.Paths.CookedPath;
            attempts += ".attempts";
            {
                std::ofstream file(attempts, std::ios::app);
                file << "encoded\n";
            }
            if (mode == "interrupt-encoding")
                std::_Exit(23);
            std::this_thread::sleep_for(std::chrono::milliseconds(40));
            return WriteTextureCookKtxFixture(prepared, output, error, 37);
        };
        const auto result = cooker.Cook(request, encoder);
        if (result.Status != TextureCookStatus::Failed)
            return 0;
        std::cerr << result.Error << '\n';
        return 1;
    }
    std::string error;
    std::vector<std::string> warnings;
    const auto prepared = cooker.Prepare(request, error, warnings);
    if (!prepared)
    {
        std::cerr << error << '\n';
        return 1;
    }
    std::filesystem::create_directories(prepared->Paths.CookedPath.parent_path());
    const auto lock = CookLock::Acquire(prepared->Paths, error);
    if (!lock || !CookPublication::Recover(prepared->Paths, error))
        return 1;
    encoder.Versions.Pipeline += "-replacement";
    const auto fingerprint = FingerprintTextureCook(*prepared, encoder.Versions, error);
    const auto publication = CookPublication::Begin(prepared->Paths, error);
    if (!fingerprint || !publication ||
        !encoder.Encode(*prepared, publication->OutputPath(), error) ||
        !ValidateCookOutput(*prepared, publication->OutputPath(), error))
        return 1;
    const auto hash = HashCookFile(publication->OutputPath(), error);
    if (!hash || !publication->Stage({*fingerprint, *hash, encoder.Versions}, error))
        return 1;
    if (mode == "interrupt-staged")
        std::_Exit(23);
    if (!publication->PublishOutput(error))
        return 1;
    if (mode == "interrupt-output")
        std::_Exit(23);
    if (!publication->PublishRecord(error))
        return 1;
    if (mode == "interrupt-record")
        std::_Exit(23);
    return 2;
}
