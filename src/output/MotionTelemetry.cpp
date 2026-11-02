#include "MotionTelemetry.hpp"
#include <chrono>
#include <mutex>
#include <cmath>
#include <algorithm>
namespace OW::OutputMotionTelemetry {
    namespace {
    uint64_t TimestampMs() {
        const auto now = std::chrono::system_clock::now();
        return static_cast<uint64_t>(
            std::chrono::duration_cast<std::chrono::milliseconds>(now.time_since_epoch()).count());
    }

    std::mutex& Mutex() {
        static std::mutex mutex;
        return mutex;
    }

    std::vector<Sample>& Samples() {
        static std::vector<Sample> samples;
        return samples;
    }

    uint64_t& Sequence() {
        static uint64_t sequence = 0;
        return sequence;
    }

    }

    void RecordMouseMove(
        const Vector3& delta,
        int pixelX,
        int pixelY,
        int automoveRuntimeMs,
        int status,
        bool split,
        int steps) {
        constexpr float kRadToDeg = 57.29577951308232f;
        const uint64_t nowMs = TimestampMs();
        const float deltaMagnitudeDeg = std::sqrt(delta.X * delta.X + delta.Y * delta.Y) * kRadToDeg;

        std::lock_guard<std::mutex> lock(Mutex());
        auto& samples = Samples();
        Sample sample{};
        sample.sequence = ++Sequence();
        sample.capturedAtMs = nowMs;
        sample.deltaPitchRad = delta.X;
        sample.deltaYawRad = delta.Y;
        sample.pixelX = pixelX;
        sample.pixelY = pixelY;
        sample.automoveRuntimeMs = automoveRuntimeMs;
        sample.status = status;
        sample.split = split;
        sample.steps = (std::max)(1, steps);

        if (!samples.empty()) {
            const Sample& previous = samples.back();
            constexpr float kMinTelemetryDtS = 0.008f;
            float dt = kMinTelemetryDtS;
            if (nowMs > previous.capturedAtMs)
                dt = (std::max)(kMinTelemetryDtS, static_cast<float>(nowMs - previous.capturedAtMs) / 1000.0f);
            sample.speedDegS = deltaMagnitudeDeg / dt;
            sample.accelDegS2 = (sample.speedDegS - previous.speedDegS) / dt;
            sample.jerkDegS3 = (sample.accelDegS2 - previous.accelDegS2) / dt;
        }

        samples.push_back(sample);
        constexpr size_t kCapacity = 512;
        if (samples.size() > kCapacity) {
            samples.erase(samples.begin(),
                samples.begin() + static_cast<std::ptrdiff_t>(samples.size() - kCapacity));
        }
    }

    std::vector<Sample> SnapshotSince(uint64_t capturedAtMs, uint64_t windowMs) {
        std::vector<Sample> snapshot;
        std::lock_guard<std::mutex> lock(Mutex());
        const auto& samples = Samples();
        snapshot.reserve(samples.size());
        for (const Sample& sample : samples) {
            if (sample.capturedAtMs <= capturedAtMs && capturedAtMs - sample.capturedAtMs <= windowMs)
                snapshot.push_back(sample);
        }
        return snapshot;
    }
}
