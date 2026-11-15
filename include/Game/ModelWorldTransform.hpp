#pragma once

#include <cmath>
#include <cstddef>
#include <cstdint>
#include "Utils/Types.hpp"

namespace OW::ModelWorld {

// CN trial input at model +0x3B0. Keep all 48 captured bytes; the fourth scale
// and translation floats are padding and may contain NaN.
struct QstRecord {
    float q[4]{};
    float scale[4]{};
    float translation[4]{};

    bool Valid() const {
        float norm = 0.f;
        for (float x : q) {
            if (!std::isfinite(x)) return false;
            norm += x * x;
        }
        if (!std::isfinite(norm) || std::fabs(norm - 1.f) > .03f) return false;
        for (size_t i = 0; i < 3; ++i) {
            if (!std::isfinite(scale[i]) || scale[i] <= 0.f || scale[i] > 10.f ||
                !std::isfinite(translation[i]) || std::fabs(translation[i]) >= 5000.f)
                return false;
        }
        // Translation is a world origin: ModelShapes::Pose's local +/-20
        // bound is deliberately inapplicable. 5000 matches spatial anchors.
        return true;
    }
};
static_assert(sizeof(QstRecord) == 0x30);
static_assert(offsetof(QstRecord, scale) == 0x10);
static_assert(offsetof(QstRecord, translation) == 0x20);

// Copied with the entity snapshot. Rows implement scale -> quaternion ->
// translation, without silently normalizing the captured quaternion.
struct Sample {
    QstRecord record{};
    float matrix[3][4]{};
    Vector3 sampleAnchor{};
    uint64_t modelBase = 0;
    bool valid = false;

    bool Set(uint64_t model, const QstRecord& value) {
        const QstRecord captured = value; // Set may rebind this sample's own record.
        *this = {};
        if (!model || !captured.Valid()) return false;
        record = captured;
        modelBase = model;
        const float x=captured.q[0], y=captured.q[1], z=captured.q[2], w=captured.q[3];
        const float sx=captured.scale[0], sy=captured.scale[1], sz=captured.scale[2];
        matrix[0][0]=(1.f-2.f*(y*y+z*z))*sx;
        matrix[0][1]=2.f*(x*y-w*z)*sy;
        matrix[0][2]=2.f*(x*z+w*y)*sz;
        matrix[1][0]=2.f*(x*y+w*z)*sx;
        matrix[1][1]=(1.f-2.f*(x*x+z*z))*sy;
        matrix[1][2]=2.f*(y*z-w*x)*sz;
        matrix[2][0]=2.f*(x*z-w*y)*sx;
        matrix[2][1]=2.f*(y*z+w*x)*sy;
        matrix[2][2]=(1.f-2.f*(x*x+y*y))*sz;
        for (size_t i=0; i<3; ++i) matrix[i][3]=captured.translation[i];
        valid = true;
        return true;
    }

    Vector3 TransformVector(const Vector3& p) const {
        if (!valid) return {};
        return {matrix[0][0]*p.X+matrix[0][1]*p.Y+matrix[0][2]*p.Z,
                matrix[1][0]*p.X+matrix[1][1]*p.Y+matrix[1][2]*p.Z,
                matrix[2][0]*p.X+matrix[2][1]*p.Y+matrix[2][2]*p.Z};
    }

    Vector3 TransformPoint(const Vector3& p) const {
        if (!valid) return {};
        const Vector3 v = TransformVector(p);
        return {v.X+matrix[0][3], v.Y+matrix[1][3], v.Z+matrix[2][3]};
    }
};

} // namespace OW::ModelWorld
