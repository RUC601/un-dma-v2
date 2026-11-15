#include <Windows.h>
#include "Backend.hpp"
#include "Session.hpp"
#include "output/transport/serial/SerialOutputDevice.hpp"

namespace V2::Output {
IOutputDevice& FerrumDevice() {
    static SerialOutputDevice device(std::make_unique<FerrumSession>(), true);
    return device;
}
}
