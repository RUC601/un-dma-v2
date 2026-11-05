#pragma once
#include <array>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
namespace V2::Output::MakcuProtocol {
inline constexpr std::string_view PhysicalButtonsRequest{"\xDE\xAD\x00\x00\x55", 5};
inline constexpr std::string_view FirmwareVersionRequest{"\xDE\xAD\x00\x00\x04", 5};
inline constexpr std::array<std::string_view, 5> ButtonQueries = {
    "km.left()", "km.right()", "km.middle()", "km.side1()", "km.side2()"
};

inline std::optional<bool> PhysicalButton(std::string_view response, size_t button) {
    constexpr std::string_view whitespace = " \r\n\t";
    const auto trim = [whitespace](std::string_view text) {
        const auto first = text.find_first_not_of(whitespace);
        return first == std::string_view::npos ? std::string_view{} :
            text.substr(first, text.find_last_not_of(whitespace) - first + 1);
    };
    response = trim(response);
    const auto echo = ButtonQueries[button];
    if (response.starts_with(echo) && response.size() > echo.size() &&
        whitespace.find(response[echo.size()]) != std::string_view::npos)
        response = trim(response.substr(echo.size()));
    if (response.size() != 1 || response.front() < '0' || response.front() > '3')
        return std::nullopt;
    return ((response.front() - '0') & 1) != 0;
}

inline bool HasLegacyPrefixedButtonStream(uint32_t version) {
    return version == 4030 || version == 4031 || version == 4033;
}

inline bool Identity(std::string_view line) {
    return line == "km.MAKCU" || line.starts_with("km.MAKCU_");
}
inline std::string Mask(int button, bool enabled) {
    constexpr const char* names[] = {"left_mask", "right_mask", "middle_mask", "side1_mask", "side2_mask"};
    if (button < 0 || button > 4) return {};
    return std::string("km.") + names[button] + "(" + (enabled ? "1" : "0") + ")\r\n";
}
}
