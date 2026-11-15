#pragma once
#include <array>
#include <cstddef>
#include <string>
#include <string_view>
namespace V2::Output::FerrumProtocol {
inline constexpr std::array<std::string_view, 5> ButtonQueries = {
    "km.left()", "km.right()", "km.middle()", "km.side1()", "km.side2()"
};

enum class ButtonReply { Other, Released, Pressed, Invalid };

// Ferrum returns a physical 0/1, including while the output button is masked.
// Do not interpret MAKCU's 0..3 ownership bitmap as a Ferrum reply.
inline ButtonReply ParseButtonReply(std::string_view response, size_t button) {
    constexpr std::string_view whitespace = " \r\n\t";
    const auto trim = [whitespace](std::string_view text) {
        const auto first = text.find_first_not_of(whitespace);
        return first == std::string_view::npos ? std::string_view{} :
            text.substr(first, text.find_last_not_of(whitespace) - first + 1);
    };
    response = trim(response);
    if (button >= ButtonQueries.size()) return ButtonReply::Invalid;
    for (size_t i = 0; i < ButtonQueries.size(); ++i) {
        const auto echo = ButtonQueries[i];
        if (!response.starts_with(echo)) continue;
        if (i != button) return ButtonReply::Invalid;
        if (response.size() == echo.size()) return ButtonReply::Other;
        if (whitespace.find(response[echo.size()]) == std::string_view::npos)
            return ButtonReply::Invalid;
        response = trim(response.substr(echo.size()));
        break;
    }
    // A queued move, mask or key setter can echo between query replies.
    if (response.starts_with("km.")) return ButtonReply::Other;
    if (response == "0") return ButtonReply::Released;
    if (response == "1") return ButtonReply::Pressed;
    return response.empty() ? ButtonReply::Other : ButtonReply::Invalid;
}

inline bool Identity(std::string_view line) { return line == "kmbox: Ferrum"; }
inline std::string Mask(int button, bool enabled) {
    constexpr const char* names[] = {"ml", "mr", "mm", "ms1", "ms2"};
    if (button < 0 || button > 4) return {};
    return std::string("km.lock_") + names[button] + "(" + (enabled ? "1" : "0") + ")\r\n";
}
inline constexpr int physicalRestoreMaxMs = 175;
}
