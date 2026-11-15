#include "vision/ContourService.hpp"
#include "vision/CaptureCard.hpp"
#include "vision/ContourDetection.hpp"
#include "vision/AsheAmmo.hpp"
#include "vision/ZaryaAmmo.hpp"
#include <algorithm>
#include <atomic>
#include <exception>
#include <mutex>
#include <thread>
#include <utility>

namespace V2::Vision {
struct Service::Impl {
    mutable std::mutex mutex;
    std::mutex lifecycle;
    Settings settings;
    ServiceStatus status;
    std::uint64_t revision = 1, nextEpoch = 0;
    double lastArrivalMs = 0.0;
    std::shared_ptr<const Frame> latest;
    std::thread worker;
    std::atomic<bool> stop{false}, active{false};

    void InvalidateLocked() { latest.reset(); }
    void Fault(const std::wstring& error) {
        std::lock_guard lock(mutex);
        InvalidateLocked();
        status.state = ServiceState::Faulted;
        status.error = error;
    }
    void Run() {
        // All DirectShow lifetime, including error cleanup, belongs to this
        // thread. The game/aim/UI threads only consume immutable snapshots.
        try {
            CaptureCard capture;
            bool opened = false;
            CaptureCardSettings openedSettings;
            std::uint64_t epoch = 0;
            while (!stop.load(std::memory_order_acquire)) {
                Settings current;
                std::uint64_t currentRevision = 0;
                {
                    std::lock_guard lock(mutex);
                    current = settings; currentRevision = revision;
                    if (!current.enabled) break;
                }
                if (!opened || !(openedSettings == current.capture)) {
                    {
                        std::lock_guard lock(mutex);
                        InvalidateLocked(); status.state = ServiceState::Starting;
                    }
                    std::wstring error;
                    if (!capture.open(current.capture, error)) { Fault(error); break; }
                    opened = true; openedSettings = current.capture;
                    const auto info = capture.info();
                    {
                        std::lock_guard lock(mutex);
                        epoch = ++nextEpoch;
                        status.width = info.width; status.height = info.height;
                        status.fps = info.framesPerSecond;
                        status.receivedFrames = status.processedFrames = status.droppedFrames = 0;
                        status.error.clear(); lastArrivalMs = 0.0;
                    }
                }
                std::wstring error;
                if (!capture.grab(250, error)) {
                    if (!stop.load(std::memory_order_acquire)) Fault(error);
                    break;
                }
                if (stop.load(std::memory_order_acquire)) break;
                const auto stats = capture.stats();
                auto frame = std::make_shared<Frame>();
                frame->sequence = stats.sequence; frame->epoch = epoch;
                frame->settingsRevision = currentRevision;
                frame->arrivalMs = stats.arrivalMs;
                frame->sampleTimeSeconds = stats.sampleTimeSeconds;
                frame->sampleTimeValid = stats.sampleTimeValid;
                if (!DetectContours(capture.view(), current, *frame, &error)) { Fault(error); break; }
                DetectAsheAmmo(*frame);
                DetectZaryaAmmo(*frame);
                {
                    std::lock_guard lock(mutex);
                    if (stop.load(std::memory_order_acquire) || !settings.enabled) break;
                    // A frame processed with a superseded palette/ROI cannot
                    // confirm a shot under a newly applied configuration.
                    if (revision != currentRevision) continue;
                    status.state = ServiceState::Running;
                    status.receivedFrames = stats.receivedFrames;
                    status.droppedFrames = stats.droppedFrames;
                    ++status.processedFrames;
                    lastArrivalMs = stats.arrivalMs;
                    if (IsFresh(*frame, settings, MonotonicMs())) latest = std::move(frame);
                    else InvalidateLocked();
                }
            }
            // capture's destructor stops the graph before leaving its owner.
        } catch (const std::exception&) {
            Fault(L"采集或轮廓处理发生异常，已停止使用图像。");
        } catch (...) {
            Fault(L"采集线程发生异常，已停止使用图像。");
        }
        {
            std::lock_guard lock(mutex);
            InvalidateLocked();
            if (status.state != ServiceState::Faulted) status.state = ServiceState::Stopped;
        }
        active.store(false, std::memory_order_release);
    }
};
Service::Service() : impl_(std::make_unique<Impl>()) {}
Service::~Service() { Stop(); }
bool Service::Configure(const Settings& settings, std::wstring* error) {
    if (!ValidateSettings(settings, error)) return false;
    std::lock_guard lock(impl_->mutex);
    if (impl_->settings == settings) return true;
    impl_->settings = settings; ++impl_->revision;
    impl_->InvalidateLocked();
    return true;
}
void Service::UpdateOutlinePalette(const std::array<Rgb, 3>& colors) {
    std::lock_guard lock(impl_->mutex);
    if (!impl_->settings.useOutlineColors || impl_->settings.colors == colors) return;
    impl_->settings.colors = colors; ++impl_->revision;
    impl_->InvalidateLocked();
}
Settings Service::GetSettings() const {
    std::lock_guard lock(impl_->mutex);
    return impl_->settings;
}
bool Service::Start() {
#ifdef UNLEASHED_ASSET_PREVIEW
    std::lock_guard lock(impl_->mutex);
    impl_->status.error = L"离线界面预览不会启动采集设备。";
    return false;
#else
    std::lock_guard lifecycle(impl_->lifecycle);
    if (impl_->active.load(std::memory_order_acquire)) return true;
    if (impl_->worker.joinable()) impl_->worker.join();
    {
        std::lock_guard lock(impl_->mutex);
        if (!impl_->settings.enabled) {
            impl_->status.state = ServiceState::Stopped;
            impl_->status.error = L"请先启用采集轮廓。";
            return false;
        }
        impl_->status = {};
        impl_->status.state = ServiceState::Starting;
        impl_->InvalidateLocked(); impl_->lastArrivalMs = 0.0;
    }
    impl_->stop.store(false, std::memory_order_release);
    impl_->active.store(true, std::memory_order_release);
    try { impl_->worker = std::thread([state=impl_.get()] { state->Run(); }); }
    catch (...) {
        impl_->active.store(false, std::memory_order_release);
        impl_->Fault(L"无法创建采集工作线程。");
        return false;
    }
    return true;
#endif
}
void Service::Stop() {
    std::lock_guard lifecycle(impl_->lifecycle);
    impl_->stop.store(true, std::memory_order_release);
    {
        std::lock_guard lock(impl_->mutex);
        impl_->InvalidateLocked();
    }
    if (impl_->worker.joinable()) impl_->worker.join();
    impl_->active.store(false, std::memory_order_release);
    std::lock_guard lock(impl_->mutex);
    impl_->status.state = ServiceState::Stopped;
    impl_->status.error.clear();
    impl_->InvalidateLocked();
}
std::shared_ptr<const Frame> Service::Snapshot() const {
    std::lock_guard lock(impl_->mutex);
    const auto frame = impl_->latest;
    if (!impl_->settings.enabled || impl_->status.state != ServiceState::Running || !frame ||
        frame->settingsRevision != impl_->revision || !IsFresh(*frame, impl_->settings, MonotonicMs())) return {};
    return frame;
}
std::shared_ptr<const Frame> Service::Snapshot(Settings& settings) const {
    std::lock_guard lock(impl_->mutex);
    settings = impl_->settings;
    const auto frame = impl_->latest;
    if (!settings.enabled || impl_->status.state != ServiceState::Running || !frame ||
        frame->settingsRevision != impl_->revision || !IsFresh(*frame, settings, MonotonicMs())) return {};
    return frame;
}
ServiceStatus Service::Status() const {
    std::lock_guard lock(impl_->mutex);
    auto result = impl_->status;
    result.frameAgeMs = impl_->lastArrivalMs > 0.0 ? (std::max)(0.0, MonotonicMs()-impl_->lastArrivalMs) : 0.0;
    return result;
}
bool Service::EnumerateDevices(std::vector<CaptureCardDevice>& devices, std::wstring& error) {
#ifdef UNLEASHED_ASSET_PREVIEW
    devices.clear();
    error = L"离线界面预览不会访问采集设备。";
    return false;
#else
    return CaptureCard::enumerate(devices, error);
#endif
}
#ifdef UNLEASHED_ASSET_PREVIEW
bool Service::PublishPreviewFrame(std::shared_ptr<Frame> frame) {
    if (!frame || frame->width <= 0 || frame->height <= 0 || frame->width > 8192 || frame->height > 8192 ||
        frame->sequence == 0) return false;
    const auto count = static_cast<std::size_t>(frame->width) * frame->height;
    if (count > 33554432 || frame->pixels.size() != count*4 || frame->mask.size() != count ||
        frame->componentIds.size() != count) return false;
    std::lock_guard lock(impl_->mutex);
    if (!impl_->settings.enabled || (frame->settingsRevision && frame->settingsRevision != impl_->revision)) return false;
    if (impl_->nextEpoch == 0) ++impl_->nextEpoch;
    if (frame->epoch && frame->epoch != impl_->nextEpoch) return false;
    auto copy = std::make_shared<Frame>(*frame);
    copy->settingsRevision = impl_->revision; copy->epoch = impl_->nextEpoch;
    if (copy->arrivalMs == 0.0) copy->arrivalMs = MonotonicMs();
    if (!IsFresh(*copy,impl_->settings,MonotonicMs())) return false;
    impl_->status = {};
    impl_->status.state = ServiceState::Running;
    impl_->status.width = copy->width; impl_->status.height = copy->height;
    impl_->status.processedFrames = copy->sequence;
    impl_->lastArrivalMs = copy->arrivalMs;
    impl_->latest = std::move(copy);
    return true;
}
#endif
Service& GetService() {
    static Service service;
    return service;
}
} // namespace V2::Vision
