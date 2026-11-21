#pragma once
#include "Game/ModelWireMesh.hpp"
#include "Renderer/Renderer.hpp"
#include "Renderer/VisualSettings.hpp"
#include <algorithm>

namespace V2::Visuals {
inline Render::Color ClassColor(const V2::VisualSettings::HitboxPalette& palette,
                                OW::ModelShapes::CriticalState state, float opacity) {
    const ImVec4* rgba = &palette.unknown;
    if (state == OW::ModelShapes::CriticalState::Critical) rgba = &palette.critical;
    else if (state == OW::ModelShapes::CriticalState::Ordinary) rgba = &palette.ordinary;
    const auto byte = [](float value) {
        return int(std::clamp(value, 0.f, 1.f) * 255.f + .5f);
    };
    return {byte(rgba->x), byte(rgba->y), byte(rgba->z), byte(rgba->w * opacity)};
}

template<class World, class Project>
bool DrawShapeWire(const OW::ModelShapes::Shape& shape,
                   World world, Project project, const Render::Color& color,
                   float thickness, OW::Vector2 offset = {}) {
    if (!color.A) return false;
    return OW::ModelShapes::DrawCachedWire(shape, world, project, [&](OW::Vector2 a, OW::Vector2 b) {
        Render::DrawLine(a + offset, b + offset, color, thickness);
    });
}

template<class World, class Project>
bool DrawShapeFill(const OW::ModelShapes::Shape& shape, World world, Project project,
                   const Render::Color& color, OW::Vector2 offset) {
    if (!color.A) return false;
    const auto& mesh = OW::ModelShapes::RenderWireCache().Get(shape);
    std::array<OW::Vector2, 120> points{};
    if (mesh.vertices.size() > points.size()) return false;
    size_t count = 0;
    for (const auto& vertex : mesh.vertices) {
        OW::Vector2 screen;
        if (!project(world(vertex), screen) || !std::isfinite(screen.X) || !std::isfinite(screen.Y))
            continue;
        points[count++] = screen + offset;
    }
    if (count < 3) return false;
    std::sort(points.begin(), points.begin() + count, [](auto a, auto b) {
        return a.X == b.X ? a.Y < b.Y : a.X < b.X;
    });
    std::array<OW::Vector2, 240> hull{};
    size_t size = 0;
    auto append = [&](OW::Vector2 point, size_t start) {
        while (size >= start + 2) {
            const auto edge = hull[size - 1] - hull[size - 2];
            const auto next = point - hull[size - 1];
            if (edge.X * next.Y > edge.Y * next.X) break;
            --size;
        }
        hull[size++] = point;
    };
    for (size_t i = 0; i < count; ++i) append(points[i], 0);
    const size_t upperStart = size - 1;
    for (size_t i = count - 1; i > 0; --i) append(points[i - 1], upperStart);
    return Render::DrawFilledConvexPolygon({hull.data(), size - 1}, color);
}
}
