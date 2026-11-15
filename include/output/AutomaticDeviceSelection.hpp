#pragma once
#include "output/DeviceDiscovery.hpp"
#include <chrono>
#include <string>

namespace V2::Output {
struct AutomaticDeviceStatus {
    bool scanning = false;
    bool ambiguous = false;
    std::string message;
    std::vector<DeviceDiscoveryCandidate> candidates;
};

// Configuration/UI thread: schedule discovery, then commit its result here.
// The discovery worker never writes configuration or initializes an output device.
int ApplyOutputConfiguration(std::chrono::milliseconds timeout = std::chrono::milliseconds(500),
    bool forceRescan = true);
void PollAutomaticDeviceSelection();
void StopAutomaticDeviceSelection();
AutomaticDeviceStatus GetAutomaticDeviceStatus();
}
