#pragma once
#include "output/OutputTypes.hpp"
#include <array>
#include <cstddef>
#include <vector>

namespace V2::Output {
// Existing public action contract accepts at most ten ordinary HID usages.
// Each device still validates its encoding and physical transport limits.
inline constexpr std::size_t kKeyboardUsageCapacity = 10;
inline bool ValidateKeyboardReport(const std::vector<unsigned char>& usages) {
    if (usages.size() > kKeyboardUsageCapacity) return false;
    std::array<bool, 256> seen{};
    for (const auto usage : usages) {
        if (usage < KEY_A || usage >= KEY_LEFTCONTROL || seen[usage]) return false;
        seen[usage] = true;
    }
    return true;
}
}
