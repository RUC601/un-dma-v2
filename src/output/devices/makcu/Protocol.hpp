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
enum class ButtonDialect { Side, Ms };
inline constexpr std::array<std::string_view, 5> MsButtonQueries = {
    "km.left()", "km.right()", "km.middle()", "km.ms1()", "km.ms2()"
};

inline const auto& Queries(ButtonDialect dialect) {
    return dialect == ButtonDialect::Ms ? MsButtonQueries : ButtonQueries;
}

inline std::string Button(int button, bool down, ButtonDialect dialect) {
    if (button < 0 || button >= int(ButtonQueries.size())) return {};
    const auto query = Queries(dialect)[button];
    return std::string(query.substr(0, query.size() - 1)) + (down ? "1)\r\n" : "0)\r\n");
}

inline std::optional<bool> PhysicalButton(std::string_view response, size_t button,
    ButtonDialect dialect = ButtonDialect::Side) {
    if (button >= ButtonQueries.size()) return std::nullopt;
    constexpr std::string_view whitespace = " \r\n\t";
    const auto trim = [whitespace](std::string_view text) {
        const auto first = text.find_first_not_of(whitespace);
        return first == std::string_view::npos ? std::string_view{} :
            text.substr(first, text.find_last_not_of(whitespace) - first + 1);
    };
    response = trim(response);
    const auto echo = Queries(dialect)[button];
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
