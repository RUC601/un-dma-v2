#pragma once
#include "vision/ContourTypes.hpp"
#include <memory>

namespace V2::Vision {
enum class ServiceState { Stopped, Starting, Running, Faulted };
struct ServiceStatus {
    ServiceState state = ServiceState::Stopped;
    std::wstring error;
    std::uint64_t receivedFrames = 0, processedFrames = 0, droppedFrames = 0;
    double frameAgeMs = 0.0;
    int width = 0, height = 0;
    double fps = 0.0;
};
class Service {
public:
    Service();
    ~Service();
    Service(const Service&) = delete;
    Service& operator=(const Service&) = delete;
    // Configure never opens hardware. Start is an explicit, nonblocking request.
    bool Configure(const Settings& settings, std::wstring* error = nullptr);
    // Change only the followed palette under the settings lock. A concurrent
    // UI device/start/stop edit must not be overwritten by palette refresh.
    void UpdateOutlinePalette(const std::array<Rgb, 3>& colors);
    Settings GetSettings() const;
    bool Start();
    void Stop();
    std::shared_ptr<const Frame> Snapshot() const;
    // Obtain frame evidence and the exact settings used to validate it under
    // one lock. Settings are also returned when no usable frame is available.
    std::shared_ptr<const Frame> Snapshot(Settings& settings) const;
    ServiceStatus Status() const;
    static bool EnumerateDevices(std::vector<CaptureCardDevice>& devices, std::wstring& error);
#ifdef UNLEASHED_ASSET_PREVIEW
    // Offline UI fixtures only. Zero metadata is stamped by the service;
    // explicit stale metadata is rejected. Publication takes a deep copy.
    bool PublishPreviewFrame(std::shared_ptr<Frame> frame);
#endif
private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
Service& GetService();
} // namespace V2::Vision
