#pragma once
#include <array>
#include <cmath>
#include <cstdint>

namespace V2::WorldObjects {
struct PositionPath { uint32_t component, offset; };
// Pure 1106 RVA 0x140CCB/0x140D1D: two transform types share +0x3D0.
// Keep the existing velocity path for objects without either transform.
constexpr auto PositionPaths(uint32_t primary, uint32_t alternate) {
    return std::array{PositionPath{primary,0x3D0},PositionPath{alternate,0x3D0},PositionPath{4,0x200}};
}
constexpr bool ValidPointer(uint64_t value) {
    return value >= 0x10000 && value < 0x800000000000ull;
}
template<class Position> bool ValidPosition(const Position& value) {
    return std::isfinite(value.x) && std::isfinite(value.y) && std::isfinite(value.z) &&
        (value.x != 0 || value.y != 0 || value.z != 0);
}
template<class Decrypt, class Read, class Position>
bool ResolvePosition(const std::array<PositionPath,3>& paths, Decrypt decrypt, Read read,
                     Position& position, uint64_t& component, uint32_t& positionOffset) {
    for(const auto path:paths) {
        if(!path.component)continue;
        const uint64_t candidate=decrypt(path.component);
        Position value{};
        if(!ValidPointer(candidate) || !read(candidate+path.offset,&value,sizeof(value)) || !ValidPosition(value))continue;
        position=value;component=candidate;positionOffset=path.offset;
        return true;
    }
    return false;
}
}
