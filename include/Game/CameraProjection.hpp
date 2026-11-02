#pragma once
#include <cmath>
#include "Utils/Types.hpp"

namespace OW {

    inline Matrix MultiplyMatrices(const Matrix& a, const Matrix& b)
    {
        Matrix result{};
        result.m11 = a.m11 * b.m11 + a.m12 * b.m21 + a.m13 * b.m31 + a.m14 * b.m41;
        result.m12 = a.m11 * b.m12 + a.m12 * b.m22 + a.m13 * b.m32 + a.m14 * b.m42;
        result.m13 = a.m11 * b.m13 + a.m12 * b.m23 + a.m13 * b.m33 + a.m14 * b.m43;
        result.m14 = a.m11 * b.m14 + a.m12 * b.m24 + a.m13 * b.m34 + a.m14 * b.m44;
        result.m21 = a.m21 * b.m11 + a.m22 * b.m21 + a.m23 * b.m31 + a.m24 * b.m41;
        result.m22 = a.m21 * b.m12 + a.m22 * b.m22 + a.m23 * b.m32 + a.m24 * b.m42;
        result.m23 = a.m21 * b.m13 + a.m22 * b.m23 + a.m23 * b.m33 + a.m24 * b.m43;
        result.m24 = a.m21 * b.m14 + a.m22 * b.m24 + a.m23 * b.m34 + a.m24 * b.m44;
        result.m31 = a.m31 * b.m11 + a.m32 * b.m21 + a.m33 * b.m31 + a.m34 * b.m41;
        result.m32 = a.m31 * b.m12 + a.m32 * b.m22 + a.m33 * b.m32 + a.m34 * b.m42;
        result.m33 = a.m31 * b.m13 + a.m32 * b.m23 + a.m33 * b.m33 + a.m34 * b.m43;
        result.m34 = a.m31 * b.m14 + a.m32 * b.m24 + a.m33 * b.m34 + a.m34 * b.m44;
        result.m41 = a.m41 * b.m11 + a.m42 * b.m21 + a.m43 * b.m31 + a.m44 * b.m41;
        result.m42 = a.m41 * b.m12 + a.m42 * b.m22 + a.m43 * b.m32 + a.m44 * b.m42;
        result.m43 = a.m41 * b.m13 + a.m42 * b.m23 + a.m43 * b.m33 + a.m44 * b.m43;
        result.m44 = a.m41 * b.m14 + a.m42 * b.m24 + a.m43 * b.m34 + a.m44 * b.m44;
        return result;
    }

    inline bool ProjectionMatrixNeedsCompositionM14Zero(const Matrix& projection)
    {
        return std::isfinite(projection.m11) && std::isfinite(projection.m22) &&
            std::isfinite(projection.m14) && std::isfinite(projection.m24) &&
            std::isfinite(projection.m34) && std::isfinite(projection.m43) &&
            std::isfinite(projection.m44) &&
            std::fabs(projection.m11) > 0.01f &&
            std::fabs(projection.m22) > 0.01f &&
            std::fabs(projection.m14) > 0.5f &&
            std::fabs(projection.m24) < 0.01f &&
            std::fabs(projection.m34 - 1.0f) < 0.05f &&
            std::fabs(projection.m43) > 0.001f &&
            std::fabs(projection.m44) < 0.01f;
    }

    inline Matrix NormalizeProjectionMatrixForComposition(const Matrix& projection)
    {
        Matrix normalized = projection;
        if (ProjectionMatrixNeedsCompositionM14Zero(normalized))
            normalized.m14 = 0.0f;
        return normalized;
    }

    inline Matrix ComposeCameraProjection(const Matrix& cameraView, const Matrix& projection)
    {
        return MultiplyMatrices(cameraView, NormalizeProjectionMatrixForComposition(projection));
    }

} // namespace OW
