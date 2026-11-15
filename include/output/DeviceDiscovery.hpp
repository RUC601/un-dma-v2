#pragma once

#include "output/DeviceRegistry.hpp"
#include <cstdint>
#include <functional>
#include <optional>
#include <string>
#include <vector>

namespace V2::Output {

struct SerialDiscoveryProfile {
    std::string port;
    std::string instanceId;
    std::uint32_t baud = 115200;
};

struct NetworkDiscoveryProfile {
    std::string ip;
    std::uint16_t commandPort = 0;
    std::string mac;
};

struct DeviceDiscoveryRequest {
    DeviceId preferredId = DeviceId::Makcu115200;
    SerialDiscoveryProfile makcu;
    SerialDiscoveryProfile ferrum;
    NetworkDiscoveryProfile network;
    std::function<bool()> cancelled;
    std::uint32_t replyTimeoutMs = 350;
};

enum class DeviceDiscoveryState { Candidate, Ready, Unavailable, Busy, Cancelled };

struct DeviceDiscoveryCandidate {
    DeviceId id = DeviceId::Makcu115200;
    std::string port;
    std::string instanceId;
    std::uint32_t baud = 115200;
    DeviceDiscoveryState state = DeviceDiscoveryState::Candidate;
    bool savedIdentity = false;
    bool savedPort = false;
    std::string reason;
};

struct DeviceDiscoverySelection {
    std::optional<std::size_t> index;
    bool ambiguous = false;
};

// Enumeration reads Windows device metadata only. It never opens COM/UDP handles.
// Unknown serial devices are excluded unless their exact saved instance ID matches.
std::vector<DeviceDiscoveryCandidate> EnumerateDeviceCandidates(const DeviceDiscoveryRequest& request);

// Uses identity/presence queries only. Do not invoke while a healthy runtime owns
// an endpoint: the caller keeps that runtime, and scans only while disconnected.
// KMBox Net here means the configured compatible endpoint, not a vendor identity.
std::vector<DeviceDiscoveryCandidate> DiscoverDevices(const DeviceDiscoveryRequest& request);

// A remembered family/instance wins only when unique. Otherwise select the sole
// ready device, or report ambiguity without choosing an arbitrary enumeration row.
DeviceDiscoverySelection SelectDiscoveredDevice(
    const std::vector<DeviceDiscoveryCandidate>& candidates, DeviceId preferredId);

}
