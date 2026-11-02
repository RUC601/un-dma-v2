#pragma once
#include <string>
#include <string_view>
namespace V2::Output::MakcuProtocol {
inline bool Identity(std::string_view line) {
    return line == "km.MAKCU" || line.starts_with("km.MAKCU_");
}
inline std::string Mask(int button, bool enabled) {
    constexpr const char* names[] = {"left_mask", "right_mask", "middle_mask", "side1_mask", "side2_mask"};
    if (button < 0 || button > 4) return {};
    return std::string("km.") + names[button] + "(" + (enabled ? "1" : "0") + ")\r\n";
}
}
