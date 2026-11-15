#include <Windows.h>
#include "output/OutputServices.hpp"
#include "Utils/Config.hpp"

namespace {
struct InstallProductOutputServices {
    InstallProductOutputServices() {
        V2::Output::SetOutputServices({
            [] { return OW::Config::OutputSuppressedByMenu(); },
            [] { return OW::Config::outputDebugLog; },
            [] { return OW::Config::aimVerboseLog; }
        });
    }
};
InstallProductOutputServices productOutputServices;
}
