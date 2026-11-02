#pragma once

#include <cmath>
#include <atomic>

namespace OW {

inline thread_local bool calibrationOutputOwner = false;

inline bool CalibrationBlocksNormalOutput(bool inProgress) noexcept
{
    return inProgress && !calibrationOutputOwner;
}

// A batch owns the entire interval; its individual measurements borrow it.
class CalibrationRunGuard {
public:
    explicit CalibrationRunGuard(std::atomic<bool>& inProgress) : inProgress_(inProgress)
    {
        if (calibrationOutputOwner) {
            valid_ = true;
            return;
        }
        bool expected = false;
        acquired_ = inProgress_.compare_exchange_strong(expected, true, std::memory_order_acq_rel);
        valid_ = acquired_;
        if (acquired_)
            calibrationOutputOwner = true;
    }
    CalibrationRunGuard(const CalibrationRunGuard&) = delete;
    CalibrationRunGuard& operator=(const CalibrationRunGuard&) = delete;
    ~CalibrationRunGuard()
    {
        if (acquired_) {
            calibrationOutputOwner = false;
            inProgress_.store(false, std::memory_order_release);
        }
    }
    explicit operator bool() const noexcept { return valid_; }
    bool IsRoot() const noexcept { return acquired_; }
private:
    std::atomic<bool>& inProgress_;
    bool valid_ = false;
    bool acquired_ = false;
};

struct CalibrationMeasurement {
    float yaw = 0.0f;
    float pitch = 0.0f;
    float referenceSensitivity = 0.0f;
};

inline float CalibrationCountsPerRadian(int counts, float before, float after) noexcept
{
    if (counts <= 0 || !std::isfinite(before) || !std::isfinite(after))
        return 0.0f;
    constexpr float twoPi = 6.2831853071795864769f;
    const float delta = std::fabs(std::remainder(after - before, twoPi));
    if (!std::isfinite(delta) || delta <= 0.0001f)
        return 0.0f;
    const float ratio = static_cast<float>(counts) / delta;
    return std::isfinite(ratio) && ratio > 0.0f ? ratio : 0.0f;
}

inline bool CalibrationAxesReady(float yaw, float pitch, bool bothAxes) noexcept
{
    return std::isfinite(yaw) && yaw > 0.0f &&
        (!bothAxes || (std::isfinite(pitch) && pitch > 0.0f));
}

inline CalibrationMeasurement CalibrationMean(double yawTotal, double pitchTotal, int samples,
                                               float referenceSensitivity) noexcept
{
    if (samples <= 0 || !std::isfinite(referenceSensitivity) || referenceSensitivity <= 0.0f)
        return {};
    const float yaw = static_cast<float>(yawTotal / samples);
    const float pitch = static_cast<float>(pitchTotal / samples);
    return CalibrationAxesReady(yaw, pitch, true)
        ? CalibrationMeasurement{yaw, pitch, referenceSensitivity} : CalibrationMeasurement{};
}

} // namespace OW
