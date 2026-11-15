#pragma once
#include <string>
#include <string_view>
namespace V2::Output::FerrumProtocol {
inline bool Identity(std::string_view line) { return line == "kmbox: Ferrum"; }
inline std::string Mask(int button, bool enabled) {
    constexpr const char* names[] = {"ml", "mr", "mm", "ms1", "ms2"};
    if (button < 0 || button > 4) return {};
    return std::string("km.lock_") + names[button] + "(" + (enabled ? "1" : "0") + ")\r\n";
}
inline constexpr int physicalRestoreMaxMs = 175;
}
