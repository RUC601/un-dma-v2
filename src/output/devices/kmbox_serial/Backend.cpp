#include <Windows.h>
#include "Backend.hpp"
#include "Session.hpp"
#include "output/transport/serial/SerialOutputDevice.hpp"

namespace V2::Output {
IOutputDevice& KmBoxSerialDevice() {
    static SerialOutputDevice device(std::make_unique<KmBoxSerialSession>(), false);
    return device;
}
}
