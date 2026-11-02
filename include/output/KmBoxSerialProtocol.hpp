#pragma once
#include <string>
namespace V2::Output::KmBoxSerialProtocol {
inline std::string Move(int x, int y, int duration = 0) {
    return "km.move(" + std::to_string(x) + "," + std::to_string(y) +
        (duration > 0 ? "," + std::to_string(duration) : "") + ")\r\n";
}
inline std::string Button(int button, bool down) {
    constexpr const char* names[] = {"left", "right", "middle", "side1", "side2"};
    if (button < 0 || button > 4) return {};
    return std::string("km.") + names[button] + "(" + (down ? "1" : "0") + ")\r\n";
}
}
