#pragma once

#include <cstddef>
#include <cstdio>

namespace UI::ValueFormat {

// Display conversion is explicit. This never changes the stored slider value.
struct Spec {
    int decimals = 0;
    const char* suffix = "";
    double scale = 1.0;
    const char* prefix = "";
    const char* lowerLabel = nullptr;
    double lowerThreshold = 0.0;
    const char* upperLabel = nullptr;
    double upperThreshold = 0.0;
};

constexpr Spec Number(int decimals, const char* suffix = "",
                      double scale = 1.0, const char* prefix = "")
{
    return { decimals, suffix, scale, prefix };
}

constexpr Spec AtOrBelow(const char* label, double threshold, Spec number)
{
    number.lowerLabel = label;
    number.lowerThreshold = threshold;
    return number;
}

constexpr Spec AtOrAbove(const char* label, double threshold, Spec number)
{
    number.upperLabel = label;
    number.upperThreshold = threshold;
    return number;
}

inline const char* Format(char* buffer, std::size_t size,
                          double value, const Spec& spec)
{
    if (spec.lowerLabel && value <= spec.lowerThreshold)
        return spec.lowerLabel;
    if (spec.upperLabel && value >= spec.upperThreshold)
        return spec.upperLabel;
    if (!buffer || size == 0)
        return "";

    const int decimals = spec.decimals < 0 ? 0 : (spec.decimals > 6 ? 6 : spec.decimals);
    std::snprintf(buffer, size, "%s%.*f%s",
                  spec.prefix ? spec.prefix : "",
                  decimals, value * spec.scale,
                  spec.suffix ? spec.suffix : "");
    return buffer;
}

} // namespace UI::ValueFormat
