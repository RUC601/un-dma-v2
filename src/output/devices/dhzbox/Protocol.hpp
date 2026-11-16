#pragma once

#include <cstdint>
#include <string>
#include <string_view>

namespace V2::Output::DhzBoxProtocol {
inline std::string Shift(std::string_view text, unsigned shift) {
    std::string result(text);
    shift %= 26;
    for (char& c : result) {
        if (c >= 'a' && c <= 'z') c = static_cast<char>('a' + (c - 'a' + shift) % 26);
        else if (c >= 'A' && c <= 'Z') c = static_cast<char>('A' + (c - 'A' + shift) % 26);
    }
    return result;
}
inline std::string Move(int x, int y) { return "move(" + std::to_string(x) + "," + std::to_string(y) + ")"; }
inline std::string Button(int button, bool down) {
    constexpr const char* names[] = {"left", "right", "middle", "side1", "side2"};
    return button >= 0 && button < 5 ? std::string(names[button]) + "(" + (down ? "1)" : "0)") : std::string();
}
inline std::string Key(const char* name, unsigned usage, bool down) {
    return std::string(name) + "(" + std::to_string(usage) + ")";
}
inline std::string Mouse(std::uint32_t mask, int x, int y, int wheel) {
    return "mouse(" + std::to_string(mask) + "," + std::to_string(x) + "," +
        std::to_string(y) + "," + std::to_string(wheel) + ")";
}
}
