#include "Features/visuals/HitboxContour.hpp"
#include "Game/ModelShapes.hpp"
#include "Renderer/Renderer.hpp"
#include <algorithm>
#include <cfloat>
#include <cmath>
#include <span>
#include <vector>

namespace OverlayRenderDetail {
namespace {

bool Reasonable(Vector2 p) {
    return std::isfinite(p.X) && std::isfinite(p.Y) &&
        p.X >= -OW::WX * 4.0f && p.X <= OW::WX * 4.0f &&
        p.Y >= -OW::WY * 4.0f && p.Y <= OW::WY * 4.0f;
}

template <class WorldTransform>
bool DrawProjectedHitboxContour(std::span<const OW::ModelShapes::Shape> shapes,
                                std::span<const OW::ModelShapes::Pose> poses,
                                std::span<const uint8_t> valid,
                                WorldTransform&& world,
                                const OW::Matrix& view,
                                ImU32 outline,
                                const ImVec4& fill,
                                float thickness,
                                Vector2 offset,
                                bool drawContour,
                                bool drawFill) {
    struct ScreenCircle { Vector2 center{}; float radius{}; };
    std::vector<ScreenCircle> samples;
    samples.reserve(shapes.size() * 5);
    for (size_t i = 0; i < shapes.size(); ++i) {
        if (i >= poses.size() || (!valid.empty() && (i >= valid.size() || !valid[i])))
            continue;
        const auto& shape = shapes[i];
        const float length = (shape.b - shape.a).Size();
        const int count = length > 0.0001f ? 5 : 1;
        for (int n = 0; n < count; ++n) {
            const float t = count == 1 ? 0.0f : float(n) / float(count - 1);
            const Vector3 p = shape.a + (shape.b - shape.a) * t;
            Vector2 center{};
            if (!view.WorldToScreen(world(i, p), &center, Vector2(OW::WX, OW::WY)) ||
                !Reasonable(center))
                continue;
            float radius = 0.0f;
            for (const Vector3 axis : {Vector3(1, 0, 0), Vector3(0, 1, 0), Vector3(0, 0, 1)}) {
                Vector2 edge{};
                if (view.WorldToScreen(world(i, p + axis * shape.radius), &edge,
                                        Vector2(OW::WX, OW::WY))) {
                    radius = (std::max)(radius, std::fabs(edge.X - center.X));
                    radius = (std::max)(radius, std::fabs(edge.Y - center.Y));
                }
                if (view.WorldToScreen(world(i, p - axis * shape.radius), &edge,
                                        Vector2(OW::WX, OW::WY))) {
                    radius = (std::max)(radius, std::fabs(edge.X - center.X));
                    radius = (std::max)(radius, std::fabs(edge.Y - center.Y));
                }
            }
            if (std::isfinite(radius))
                samples.push_back({center + offset, (std::clamp)(radius, 1.5f, 200.0f)});
        }
    }
    if (samples.empty())
        return false;

    float minX = FLT_MAX, minY = FLT_MAX, maxX = -FLT_MAX, maxY = -FLT_MAX;
    for (const auto& circle : samples) {
        minX = (std::min)(minX, circle.center.X - circle.radius);
        minY = (std::min)(minY, circle.center.Y - circle.radius);
        maxX = (std::max)(maxX, circle.center.X + circle.radius);
        maxY = (std::max)(maxY, circle.center.Y + circle.radius);
    }
    const float width = maxX - minX, height = maxY - minY;
    if (!(width > 0.0f && height > 0.0f) || !std::isfinite(width) || !std::isfinite(height))
        return false;

    const float step = (std::max)(2.0f, (std::max)(height / 96.0f, width / 64.0f));
    const int cols = (std::clamp)(int(std::ceil(width / step)) + 1, 2, 160);
    const int rows = (std::clamp)(int(std::ceil(height / step)) + 1, 2, 320);
    const size_t stride = size_t(cols + 1);
    std::vector<float> grid(stride * size_t(rows + 1), -FLT_MAX);
    for (const auto& circle : samples) {
        const int x0 = (std::max)(0, int((circle.center.X - circle.radius - minX) / step) - 1);
        const int x1 = (std::min)(cols, int((circle.center.X + circle.radius - minX) / step) + 1);
        const int y0 = (std::max)(0, int((circle.center.Y - circle.radius - minY) / step) - 1);
        const int y1 = (std::min)(rows, int((circle.center.Y + circle.radius - minY) / step) + 1);
        for (int y = y0; y <= y1; ++y)
            for (int x = x0; x <= x1; ++x) {
                const float px = minX + x * step, py = minY + y * step;
                auto& value = grid[size_t(y) * stride + size_t(x)];
                value = (std::max)(value, circle.radius - std::hypot(px - circle.center.X, py - circle.center.Y));
            }
    }

    auto value = [&](int x, int y) { return grid[size_t(y) * stride + size_t(x)]; };
    auto interp = [](float a, float b) {
        const float delta = b - a;
        return std::fabs(delta) < 1e-5f ? 0.5f : (std::clamp)(-a / delta, 0.0f, 1.0f);
    };
    const Render::Color contourColor(
        (outline >> IM_COL32_R_SHIFT) & 255,
        (outline >> IM_COL32_G_SHIFT) & 255,
        (outline >> IM_COL32_B_SHIFT) & 255,
        (outline >> IM_COL32_A_SHIFT) & 255);
    static constexpr int edges[16][4] = {
        {-1, -1, -1, -1}, {3, 0, -1, -1}, {0, 1, -1, -1}, {3, 1, -1, -1},
        {1, 2, -1, -1}, {3, 2, 0, 1}, {0, 2, -1, -1}, {3, 2, -1, -1},
        {2, 3, -1, -1}, {0, 2, -1, -1}, {0, 1, 2, 3}, {1, 2, -1, -1},
        {1, 3, -1, -1}, {0, 1, -1, -1}, {3, 0, -1, -1}, {-1, -1, -1, -1},
    };
    bool drew = false;
    for (int y = 0; y < rows; ++y)
        for (int x = 0; x < cols; ++x) {
            const float a = value(x, y), b = value(x + 1, y);
            const float c = value(x + 1, y + 1), d = value(x, y + 1);
            const int code = (a > 0 ? 1 : 0) | (b > 0 ? 2 : 0) | (c > 0 ? 4 : 0) | (d > 0 ? 8 : 0);
            if (!code)
                continue;
            drew = true;
            const Vector2 cellMin(minX + x * step, minY + y * step);
            if (drawFill)
                Render::DrawFilledRect(cellMin, step, step, ImColor(fill));
            const Vector2 points[4] = {
                {cellMin.X + interp(a, b) * step, cellMin.Y},
                {cellMin.X + step, cellMin.Y + interp(b, c) * step},
                {cellMin.X + interp(d, c) * step, cellMin.Y + step},
                {cellMin.X, cellMin.Y + interp(a, d) * step},
            };
            auto line = [&](int i, int j) {
                if (drawContour)
                    Render::DrawLine(points[i], points[j], contourColor, thickness);
            };
            if (edges[code][0] >= 0)
                line(edges[code][0], edges[code][1]);
            if (edges[code][2] >= 0)
                line(edges[code][2], edges[code][3]);
        }
    return drew;
}

}

bool DrawNativeHitboxContour(const OW::c_entity& entity,
                             const OW::Matrix& view,
                             ImU32 outline,
                             const ImVec4& fill,
                             float thickness,
                             Vector2 offset,
                             bool drawContour,
                             bool drawFill) {
    const auto& native = entity.native_hitboxes;
    if (!entity.HasCurrentModelWorldTransform() ||
        !native.Matches(entity.VelocityBase, entity.skeleton_revision) ||
        GetTickCount() - native.tick > 100)
        return false;
    const auto delta = entity.pos - native.world.sampleAnchor;
    return DrawProjectedHitboxContour(
        native.shapes, native.poses, {},
        [&](size_t index, Vector3 point) {
            return native.world.TransformPoint(OW::ModelShapes::Transform(point, native.poses[index])) + delta;
        }, view, outline, fill, thickness, offset, drawContour, drawFill);
}

bool DrawModelHitboxContour(const OW::c_entity& entity,
                            const OW::Matrix& view,
                            ImU32 outline,
                            const ImVec4& fill,
                            float thickness,
                            Vector2 offset,
                            bool drawContour,
                            bool drawFill) {
    if (!OW::ModelShapes::Supported(entity.HeroID) || entity.model_shapes.modelIndex < 0 ||
        !entity.model_shapes.MatchesSkeleton(entity.skeleton_revision) ||
        !entity.model_shapes.tick || GetTickCount() - entity.model_shapes.tick > 100)
        return false;
    const auto& model = OW::ModelShapes::kModels[entity.model_shapes.modelIndex];
    if (model.heroId != entity.HeroID || entity.model_shapes.poses.size() != model.shapes.size() ||
        entity.model_shapes.valid.size() != model.shapes.size())
        return false;
    const auto yaw = DirectX::XMMatrixRotationY(entity.Rot.X);
    return DrawProjectedHitboxContour(
        model.shapes, entity.model_shapes.poses, entity.model_shapes.valid,
        [&](size_t index, Vector3 point) {
            const Vector3 local = OW::ModelShapes::Transform(point, entity.model_shapes.poses[index]);
            DirectX::XMFLOAT3 value{local.X, local.Y, local.Z}, out{};
            DirectX::XMStoreFloat3(&out, DirectX::XMVector3Transform(
                DirectX::XMLoadFloat3(&value), yaw));
            return Vector3(out.x, out.y, out.z) + entity.pos;
        }, view, outline, fill, thickness, offset, drawContour, drawFill);
}

}
