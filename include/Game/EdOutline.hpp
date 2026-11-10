#pragma once
#include <algorithm>
#include <bit>
#include <cmath>
#include <cstdint>

namespace OW::EdOutline {
// ED payload B 0x85040: 0x851A4 CN/Nexon; 0x8515B ordinary international.
constexpr uint64_t Encode(uint32_t mode, bool world = false) {
    if (world) {
        uint64_t value = (std::rotr(uint64_t(mode), 8) ^ 0x82ULL) - 0x4B5C673B1FBCA799ULL;
        value = (std::rotr(value, 59) ^ 0x21FAB311974C976EULL) - 0x317BA1CAEC63DB41ULL;
        return std::rotr(value, 46) ^ 0x9CA7CD6B36785C6BULL;
    }
    return (std::rotr(std::rotr(uint64_t(mode), 8) - 0x32CE3028894F970AULL, 14)
        - 0x79BA358412039C60ULL) ^ 0x4B6386359E28DB5DULL;
}
static_assert(Encode(1) == 0x61428576B372E53CULL);
static_assert(Encode(2) == 0x61428176B372E53CULL);
static_assert(Encode(1, true) == 0xBBAD06658A25CC53ULL);
inline uint32_t Color(float r, float g, float b) {
    auto byte = [](float v) { return uint32_t((std::isfinite(v) ? std::clamp(v, 0.f, 1.f) : 0.f) * 255.f); };
    return 0xFF000000u | byte(r) | (byte(g) << 8) | (byte(b) << 16);
}
inline uint32_t AnimatedColor(float r, float g, float b, float gain, bool rainbow, uint64_t nowMs) {
    if (rainbow) {
        const float h = float(nowMs % 8000) * (6.f / 8000.f);
        const auto channel = [h](float offset) {
            return std::clamp(std::abs(std::fmod(h + offset, 6.f) - 3.f) - 1.f, 0.f, 1.f);
        };
        r = channel(0); g = channel(4); b = channel(2);
    }
    gain = std::isfinite(gain) ? std::clamp(gain, .1f, 3.f) : 1.f;
    return Color(r * gain, g * gain, b * gain);
}
inline bool Pointer(uint64_t p) { return p >= 0x10000 && p <= 0x00007FFFFFFFF000ULL; }
struct Target {
    uint64_t component = 0, parameter = 0, color = 0;
    bool operator==(const Target&) const = default;
};
template<class Read>
bool Resolve(uint64_t component, Read read, Target& target) {
    struct List { uint64_t data; int32_t count; };
    List parameters{}, colors{}, parametersAfter{}, colorsAfter{};
    if (!Pointer(component) || !read(component + 0x80, &parameters, 12) ||
        !read(component + 0xF0, &colors, 12)) return false;
    if (!Pointer(parameters.data) || !Pointer(colors.data) || parameters.count <= 0 ||
        parameters.count > 4096 || colors.count <= 0 || colors.count > 4096) return false;
    // Ordinary player slots use +8. ED's alternate +16 field belongs to classified state-3 objects.
    target = {component, parameters.data + uint64_t(parameters.count - 1) * 32 + 8,
        colors.data + uint64_t(colors.count - 1) * 20 + 16};
    if (!Pointer(target.parameter) || !Pointer(target.color) ||
        !read(component + 0x80, &parametersAfter, 12) || !read(component + 0xF0, &colorsAfter, 12)) return false;
    return parameters.data == parametersAfter.data && parameters.count == parametersAfter.count &&
        colors.data == colorsAfter.data && colors.count == colorsAfter.count;
}

struct State {
    Target target{};
    uint64_t originalParameter = 0, writtenParameter = 0;
    uint32_t originalColor = 0, writtenColor = 0, nextAttempt = 0;
    bool parameterOwned = false, colorOwned = false, attempted = false, lastVisible = false, restoring = false;
    uint32_t lastColor = 0;
    bool Active() const { return parameterOwned || colorOwned; }
    bool Due(uint32_t now, bool enabled, bool visible, uint32_t color, bool animated = false) const {
        return (!enabled && Active() && (!restoring || int32_t(now - nextAttempt) >= 0)) || (enabled && (!attempted || visible != lastVisible ||
            (!animated && color != lastColor) || int32_t(now - nextAttempt) >= 0));
    }
    template<class Read, class Write, class Current>
    bool Restore(Read read, Write write, Current current) {
        auto restore = [&](uint64_t address, auto original, auto written, bool& owned) {
            if (!owned) return true;
            decltype(original) value{};
            if (!current() || !read(address, &value, sizeof(value))) return false;
            // The game may have replaced the value since our write.
            if (value != written || (current() && write(address, &original, sizeof(original)))) {
                owned = false;
                return true;
            }
            return false;
        };
        const bool a = restore(target.parameter, originalParameter, writtenParameter, parameterOwned);
        const bool b = restore(target.color, originalColor, writtenColor, colorOwned);
        return a && b;
    }
    template<class Read, class Write, class Current>
    bool Apply(Target next, bool visible, uint32_t color, uint32_t now, Read read, Write write, Current current, bool world = false, uint32_t refreshMs = 500) {
        restoring = false;
        attempted = true; nextAttempt = now + refreshMs; lastVisible = visible; lastColor = color;
        // A moved container invalidates old addresses; never restore into a retired allocation.
        if (!(target == next)) { target = next; parameterOwned = colorOwned = false; }
        uint64_t oldParameter = 0;
        uint32_t oldColor = 0;
        if (!current() || !read(target.parameter, &oldParameter, 8) || !read(target.color, &oldColor, 4)) return false;
        if (!parameterOwned || oldParameter != writtenParameter) originalParameter = oldParameter;
        if (!colorOwned || oldColor != writtenColor) originalColor = oldColor;
        const auto encoded = Encode(visible ? 1 : 2, world);
        if (!current()) return false;
        writtenParameter = encoded; parameterOwned = true;
        if (!write(target.parameter, &encoded, 8)) {
            Restore(read, write, current);
            return false;
        }
        if (!current()) { Restore(read, write, current); return false; }
        writtenColor = color; colorOwned = true;
        if (!write(target.color, &color, 4)) {
            Restore(read, write, current);
            return false;
        }
        return true;
    }
};
}
