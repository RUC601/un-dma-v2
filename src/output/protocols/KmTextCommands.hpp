#pragma once

#include "output/OutputTypes.hpp"
#include <string>
#include <string_view>

namespace V2::Output::KmTextCommands {

inline std::string Move(int x, int y, int duration = 0) {
    return "km.move(" + std::to_string(x) + "," + std::to_string(y) +
        (duration > 0 ? "," + std::to_string(duration) : "") + ")\r\n";
}

inline std::string Button(int button, bool down) {
    constexpr const char* names[] = {"left", "right", "middle", "side1", "side2"};
    if (button < 0 || button > 4) return {};
    return std::string("km.") + names[button] + "(" + (down ? "1" : "0") + ")\r\n";
}

inline std::string Key(unsigned usage, bool down) {
    return std::string(down ? "km.down(" : "km.up(") + std::to_string(usage) + ")\r\n";
}

inline int ButtonDomain(std::string_view command) {
    if (command.starts_with("km.left(")) return 0;
    if (command.starts_with("km.right(")) return 1;
    if (command.starts_with("km.middle(")) return 2;
    if (command.starts_with("km.side1(")) return 3;
    if (command.starts_with("km.side2(")) return 4;
    return -1;
}

inline bool IsButtonRelease(std::string_view command) {
    return command == "km.left(0)\r\n" || command == "km.right(0)\r\n" ||
        command == "km.middle(0)\r\n" || command == "km.side1(0)\r\n" ||
        command == "km.side2(0)\r\n";
}

inline int FlushInterval(OutputCommandType type) {
    return type == OutputCommandType::MouseButton
        ? OutputQueueConfig::MouseButtonFlushIntervalMs
        : OutputQueueConfig::CommandFlushIntervalMs;
}

template<class Identity>
bool IdentityMatches(std::string_view response, Identity identity) {
    while (!response.empty()) {
        const auto end = response.find_first_of("\r\n");
        auto line = response.substr(0, end);
        while (!line.empty() && line.front() == ' ') line.remove_prefix(1);
        while (!line.empty() && line.back() == ' ') line.remove_suffix(1);
        if (identity(line)) return true;
        if (end == std::string_view::npos) break;
        response.remove_prefix(end + 1);
    }
    return false;
}

}
