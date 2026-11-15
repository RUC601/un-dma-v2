#pragma once
#include <cstdint>
#include <vector>
#include "Utils/Types.hpp"
namespace OW::OutputMotionTelemetry {


    struct Sample {
        uint64_t sequence = 0;
        uint64_t capturedAtMs = 0;
        float deltaPitchRad = 0.0f;
        float deltaYawRad = 0.0f;
        int countX = 0;
        int countY = 0;
        int automoveRuntimeMs = 0;
        int status = 0;
        bool split = false;
        int steps = 1;
        float speedDegS = 0.0f;
        float accelDegS2 = 0.0f;
        float jerkDegS3 = 0.0f;
    };





    void RecordMouseMove(
        const Vector3& delta,
        int countX,
        int countY,
        int automoveRuntimeMs,
        int status,
        bool split,
        int steps);

    std::vector<Sample> SnapshotSince(uint64_t capturedAtMs, uint64_t windowMs);


}
