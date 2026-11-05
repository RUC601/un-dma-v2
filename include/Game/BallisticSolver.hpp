#pragma once

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>

#include "Utils/Types.hpp"

namespace OW {
namespace BallisticDetail {

using Polynomial = std::array<double, 5>;

inline double Evaluate(const Polynomial& coefficients, int degree, double time)
{
    double value = coefficients[degree];
    while (degree > 0)
        value = value * time + coefficients[--degree];
    return value;
}

// Derivative roots partition the polynomial into monotone intervals, including tangent roots.
inline int RootsInInterval(const Polynomial& coefficients, int degree, double lower, double upper,
                           std::array<double, 4>& roots)
{
    if (degree == 1) {
        const double root = -coefficients[0] / coefficients[1];
        if (std::isfinite(root) && root >= lower && root <= upper) {
            roots[0] = root;
            return 1;
        }
        return 0;
    }
    Polynomial derivative{};
    for (int i = 1; i <= degree; ++i)
        derivative[i - 1] = i * coefficients[i];
    std::array<double, 4> extrema{};
    const int count = RootsInInterval(derivative, degree - 1, lower, upper, extrema);
    int found = 0;
    const auto add = [&](double root) {
        if (found < degree && (found == 0 || root - roots[found - 1] > 1e-9 * (1.0 + root)))
            roots[found++] = root;
    };
    const auto isZero = [&](double time, double value) {
        double scale = std::abs(coefficients[degree]);
        for (int i = degree - 1; i >= 0; --i)
            scale = scale * time + std::abs(coefficients[i]);
        return std::abs(value) <= 1e-12 * (std::max)(1.0, scale);
    };
    double left = lower;
    double leftValue = Evaluate(coefficients, degree, left);
    if (isZero(left, leftValue))
        add(left);
    for (int i = 0; i <= count; ++i) {
        const double right = i < count ? extrema[i] : upper;
        const double rightValue = Evaluate(coefficients, degree, right);
        if (!isZero(left, leftValue) && !isZero(right, rightValue) &&
            std::signbit(leftValue) != std::signbit(rightValue)) {
            double lo = left, hi = right;
            for (int step = 0; step < 64; ++step) {
                const double middle = (lo + hi) * 0.5;
                if (std::signbit(Evaluate(coefficients, degree, middle)) == std::signbit(leftValue))
                    lo = middle;
                else
                    hi = middle;
            }
            add((lo + hi) * 0.5);
        }
        if (isZero(right, rightValue))
            add(right);
        left = right;
        leftValue = rightValue;
    }
    return found;
}

} // namespace BallisticDetail

struct BallisticIntercept {
    Vector3 aimPoint{};
    float flightSeconds = 0.0f;
    bool solved = false;
};

inline BallisticIntercept SolveBallisticIntercept(const Vector3& camera, const Vector3& target,
                                                 const Vector3& velocity, float speed, float gravity)
{
    BallisticIntercept result{ target };
    const auto finite = [](const Vector3& v) {
        return std::isfinite(v.X) && std::isfinite(v.Y) && std::isfinite(v.Z);
    };
    if (!finite(camera) || !finite(target) || !finite(velocity) ||
        !std::isfinite(speed) || speed <= 0.0f || !std::isfinite(gravity) || gravity < 0.0f)
        return result;
    const double x = double(target.X) - camera.X, y = double(target.Y) - camera.Y, z = double(target.Z) - camera.Z;
    const double vx = velocity.X, vy = velocity.Y, vz = velocity.Z, s = speed, g = gravity;
    const double distance2 = x*x + y*y + z*z, velocity2 = vx*vx + vy*vy + vz*vz;
    if (distance2 <= 1e-12)
        return result;
    const double a = velocity2 - s*s, b = 2.0 * (x*vx + y*vy + z*vz);
    double time = (std::numeric_limits<double>::infinity)();
    const auto accept = [&](double root) {
        if (std::isfinite(root) && root > 0.0)
            time = (std::min)(time, root);
    };
    if (g == 0.0) {
        if (std::abs(a) <= 1e-12 * (std::max)(1.0, velocity2 + s*s)) {
            if (b != 0.0)
                accept(-distance2 / b);
        } else {
            const double discriminant = b*b - 4.0*a*distance2;
            if (discriminant >= 0.0) {
                const double q = -0.5 * (b + std::copysign(std::sqrt(discriminant), b));
                accept(q / a);
                if (q != 0.0)
                    accept(distance2 / q);
            }
        }
    } else {
        const BallisticDetail::Polynomial coefficients{ distance2, b, a + g*y, g*vy, 0.25*g*g };
        const double speedBound = s + std::sqrt(velocity2);
        const double upper = (speedBound + std::sqrt(speedBound*speedBound + 2.0*g*std::sqrt(distance2))) / g;
        std::array<double, 4> roots{};
        const int count = BallisticDetail::RootsInInterval(coefficients, 4, 0.0, upper, roots);
        for (int i = 0; i < count; ++i)
            accept(roots[i]);
    }
    if (!std::isfinite(time))
        return result;
    const Vector3 point(float(target.X + vx*time), float(target.Y + vy*time + 0.5*g*time*time),
                        float(target.Z + vz*time));
    const double requiredSpeed = (point - camera).Size() / time;
    if (!finite(point) || std::abs(requiredSpeed - s) > s * 0.0001)
        return result;
    result.aimPoint = point;
    result.flightSeconds = static_cast<float>(time);
    result.solved = true;
    return result;
}

} // namespace OW
