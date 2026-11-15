// Adapted 2026-11-15 from KOI-V3-DualPC/src/capture_card.cpp.
// This V2-owned adapter preserves native RGB24 and latest-frame semantics.
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <dshow.h>
#include <dvdmedia.h>
#include <wrl/client.h>
#include "vision/CaptureCard.hpp"
#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <condition_variable>
#include <cwctype>
#include <limits>
#include <mutex>
#include <sstream>
#include <utility>

namespace V2::Vision {
namespace {
using Microsoft::WRL::ComPtr;
// qedit.h is absent from current Windows SDKs. These are the published
// Sample Grabber interfaces; no external DirectShow helper library is used.
MIDL_INTERFACE("0579154A-2B53-4994-B0D0-E773148EFF85") SampleCallback : public IUnknown {
    virtual HRESULT STDMETHODCALLTYPE SampleCB(double sampleTime, IMediaSample* sample) = 0;
    virtual HRESULT STDMETHODCALLTYPE BufferCB(double sampleTime, BYTE* buffer, long length) = 0;
};
MIDL_INTERFACE("6B652FFF-11FE-4FCE-92AD-0266B5D7C78F") SampleGrabber : public IUnknown {
    virtual HRESULT STDMETHODCALLTYPE SetOneShot(BOOL oneShot) = 0;
    virtual HRESULT STDMETHODCALLTYPE SetMediaType(const AM_MEDIA_TYPE* type) = 0;
    virtual HRESULT STDMETHODCALLTYPE GetConnectedMediaType(AM_MEDIA_TYPE* type) = 0;
    virtual HRESULT STDMETHODCALLTYPE SetBufferSamples(BOOL bufferSamples) = 0;
    virtual HRESULT STDMETHODCALLTYPE GetCurrentBuffer(long* size, long* buffer) = 0;
    virtual HRESULT STDMETHODCALLTYPE GetCurrentSample(IMediaSample** sample) = 0;
    virtual HRESULT STDMETHODCALLTYPE SetCallback(SampleCallback* callback, long method) = 0;
};
const CLSID sampleGrabberClass = {0xc1f400a0, 0x3f08, 0x11d3, {0x9f,0x0b,0x00,0x60,0x08,0x03,0x9e,0x37}};
const CLSID nullRendererClass = {0xc1f400a4, 0x3f08, 0x11d3, {0x9f,0x0b,0x00,0x60,0x08,0x03,0x9e,0x37}};

double monotonicMs() {
    return std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now().time_since_epoch()).count();
}
std::wstring failure(const wchar_t* step, HRESULT result) {
    std::wostringstream out;
    out << step << L"（0x" << std::hex << static_cast<unsigned long>(result) << L"）";
    return out.str();
}
void freeMedia(AM_MEDIA_TYPE& media) {
    if (media.cbFormat != 0) CoTaskMemFree(media.pbFormat);
    if (media.pUnk) media.pUnk->Release();
    media = {};
}
struct MediaOwner {
    AM_MEDIA_TYPE value{};
    ~MediaOwner() { freeMedia(value); }
    MediaOwner() = default;
    MediaOwner(const MediaOwner&) = delete;
    MediaOwner& operator=(const MediaOwner&) = delete;
    void take(AM_MEDIA_TYPE* source) {
        freeMedia(value); value = *source; *source = {}; CoTaskMemFree(source);
    }
};
struct ComScope {
    HRESULT result = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    ~ComScope() { if (SUCCEEDED(result)) CoUninitialize(); }
};
struct ListedDevice { CaptureCardDevice device; ComPtr<IMoniker> moniker; };
std::wstring property(IPropertyBag* bag, const wchar_t* key) {
    VARIANT value; VariantInit(&value);
    std::wstring result;
    if (SUCCEEDED(bag->Read(key, &value, nullptr)) && value.vt == VT_BSTR && value.bstrVal)
        result = value.bstrVal;
    VariantClear(&value);
    return result;
}
bool listDevices(std::vector<ListedDevice>& devices, std::wstring& error) {
    devices.clear();
    ComPtr<ICreateDevEnum> creator;
    HRESULT hr = CoCreateInstance(CLSID_SystemDeviceEnum, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&creator));
    if (FAILED(hr)) { error = failure(L"无法创建视频设备枚举器", hr); return false; }
    ComPtr<IEnumMoniker> enumerator;
    hr = creator->CreateClassEnumerator(CLSID_VideoInputDeviceCategory, &enumerator, 0);
    if (hr == S_FALSE) return true;
    if (FAILED(hr)) { error = failure(L"无法枚举视频采集设备", hr); return false; }
    ComPtr<IMoniker> moniker;
    while (enumerator->Next(1, &moniker, nullptr) == S_OK) {
        ComPtr<IPropertyBag> bag;
        if (SUCCEEDED(moniker->BindToStorage(nullptr, nullptr, IID_PPV_ARGS(&bag)))) {
            ListedDevice listed;
            listed.device.friendlyName = property(bag.Get(), L"FriendlyName");
            listed.device.devicePath = property(bag.Get(), L"DevicePath");
            if (listed.device.devicePath.empty()) {
                LPOLESTR display = nullptr;
                if (SUCCEEDED(moniker->GetDisplayName(nullptr, nullptr, &display)) && display) {
                    listed.device.devicePath = display; CoTaskMemFree(display);
                }
            }
            if (!listed.device.devicePath.empty()) {
                listed.moniker = moniker; devices.push_back(std::move(listed));
            }
        }
        moniker.Reset();
    }
    return true;
}
std::wstring lower(std::wstring value) {
    std::transform(value.begin(), value.end(), value.begin(), [](wchar_t ch) { return static_cast<wchar_t>(std::towlower(ch)); });
    return value;
}
bool mediaInfo(const AM_MEDIA_TYPE& media, CaptureCardInfo& info) {
    if (media.majortype != MEDIATYPE_Video || media.subtype != MEDIASUBTYPE_RGB24 || !media.pbFormat) return false;
    const BITMAPINFOHEADER* bitmap = nullptr;
    REFERENCE_TIME interval = 0;
    if (media.formattype == FORMAT_VideoInfo && media.cbFormat >= sizeof(VIDEOINFOHEADER)) {
        const auto* header = reinterpret_cast<const VIDEOINFOHEADER*>(media.pbFormat);
        bitmap = &header->bmiHeader; interval = header->AvgTimePerFrame;
    } else if (media.formattype == FORMAT_VideoInfo2 && media.cbFormat >= sizeof(VIDEOINFOHEADER2)) {
        const auto* header = reinterpret_cast<const VIDEOINFOHEADER2*>(media.pbFormat);
        if ((header->dwInterlaceFlags & AMINTERLACE_IsInterlaced) != 0) return false;
        bitmap = &header->bmiHeader; interval = header->AvgTimePerFrame;
    }
    if (!bitmap || bitmap->biSize < sizeof(BITMAPINFOHEADER) || bitmap->biPlanes != 1 ||
        bitmap->biCompression != BI_RGB || bitmap->biBitCount != 24 || bitmap->biWidth <= 0 || bitmap->biWidth > 16384 ||
        bitmap->biHeight == 0 || bitmap->biHeight < -16384 || bitmap->biHeight > 16384 || interval <= 0) return false;
    info.width = bitmap->biWidth;
    info.height = bitmap->biHeight < 0 ? -bitmap->biHeight : bitmap->biHeight;
    info.bitsPerPixel = 24; info.bottomUp = bitmap->biHeight > 0;
    const auto minimumStride = (static_cast<std::size_t>(info.width) * 3 + 3) & ~std::size_t{3};
    std::size_t stride = minimumStride;
    if (bitmap->biSizeImage != 0) {
        if (bitmap->biSizeImage % static_cast<unsigned>(info.height) != 0) return false;
        stride = bitmap->biSizeImage / static_cast<unsigned>(info.height);
        if (stride < minimumStride || stride > minimumStride + 65536) return false;
    }
    info.rowStride = static_cast<std::ptrdiff_t>(stride);
    info.framesPerSecond = 10000000.0 / static_cast<double>(interval);
    info.format = L"RGB24";
    return true;
}
bool pin(IBaseFilter* filter, PIN_DIRECTION direction, ComPtr<IPin>& found) {
    ComPtr<IEnumPins> pins;
    if (FAILED(filter->EnumPins(&pins))) return false;
    ComPtr<IPin> current;
    while (pins->Next(1, &current, nullptr) == S_OK) {
        PIN_DIRECTION actual{};
        if (SUCCEEDED(current->QueryDirection(&actual)) && actual == direction) { found = current; return true; }
        current.Reset();
    }
    return false;
}
struct FrameMailbox {
    std::mutex mutex;
    std::condition_variable ready;
    std::vector<unsigned char> bytes;
    std::size_t requiredBytes = 0;
    CaptureCardInfo format;
    std::uint64_t received = 0, consumed = 0, dropped = 0, sequence = 0;
    bool pending = false, closed = false, invalidSample = false;
    bool sampleTimeValid = false;
    double arrival = 0.0, sampleTime = 0.0;
};
class Callback final : public SampleCallback {
public:
    explicit Callback(std::shared_ptr<FrameMailbox> mailbox) : mailbox_(std::move(mailbox)) {}
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID id, void** object) override {
        if (!object) return E_POINTER;
        *object = nullptr;
        if (id == IID_IUnknown || id == __uuidof(SampleCallback)) {
            *object = static_cast<SampleCallback*>(this); AddRef(); return S_OK;
        }
        return E_NOINTERFACE;
    }
    ULONG STDMETHODCALLTYPE AddRef() override { return ++references_; }
    ULONG STDMETHODCALLTYPE Release() override {
        const ULONG result = --references_; if (result == 0) delete this; return result;
    }
    HRESULT STDMETHODCALLTYPE SampleCB(double, IMediaSample* sample) override {
        if (!sample) return E_POINTER;
        // Driver callbacks cross a COM ABI. In particular, allocation failure
        // while interpreting a dynamic format must end capture, not unwind
        // through a driver thread and terminate the process before cleanup.
        try {
        // A source may attach a dynamic media type after an HDMI mode change.
        // Stop if it changes the bytes we interpret, even when buffer size fits.
        AM_MEDIA_TYPE* changed = nullptr;
        const HRESULT typeResult = sample->GetMediaType(&changed);
        if (typeResult == S_OK && changed) {
            MediaOwner changedMedia;
            changedMedia.take(changed);
            CaptureCardInfo actual;
            const auto& expected = mailbox_->format;
            const bool same = mediaInfo(changedMedia.value, actual) && actual.width == expected.width && actual.height == expected.height &&
                actual.rowStride == expected.rowStride && actual.bottomUp == expected.bottomUp &&
                std::abs(actual.framesPerSecond - expected.framesPerSecond) <= 0.01;
            if (!same) {
                std::lock_guard<std::mutex> lock(mailbox_->mutex);
                mailbox_->invalidSample = true; mailbox_->ready.notify_one(); return E_FAIL;
            }
        } else if (FAILED(typeResult)) {
            std::lock_guard<std::mutex> lock(mailbox_->mutex);
            mailbox_->invalidSample = true; mailbox_->ready.notify_one(); return typeResult;
        }
        BYTE* bytes = nullptr;
        const HRESULT pointerResult = sample->GetPointer(&bytes);
        if (FAILED(pointerResult)) {
            std::lock_guard<std::mutex> lock(mailbox_->mutex);
            mailbox_->invalidSample = true; mailbox_->ready.notify_one(); return pointerResult;
        }
        REFERENCE_TIME begin = 0, end = 0;
        const HRESULT timeResult = sample->GetTime(&begin, &end);
        const bool timeValid = timeResult == S_OK || timeResult == VFW_S_NO_STOP_TIME;
        return publish(timeValid ? static_cast<double>(begin) / 10000000.0 : 0.0,
            bytes, sample->GetActualDataLength(), timeValid);
        } catch (...) {
            std::lock_guard<std::mutex> lock(mailbox_->mutex);
            mailbox_->invalidSample = true; mailbox_->ready.notify_one(); return E_OUTOFMEMORY;
        }
    }
    HRESULT STDMETHODCALLTYPE BufferCB(double sampleTime, BYTE* buffer, long length) override {
        return publish(sampleTime, buffer, length, false);
    }
    HRESULT publish(double sampleTime, BYTE* buffer, long length, bool sampleTimeValid) {
        try {
            std::lock_guard<std::mutex> lock(mailbox_->mutex);
            if (mailbox_->closed) return S_OK;
            if (!buffer || length < 0 || static_cast<std::size_t>(length) < mailbox_->requiredBytes || !std::isfinite(sampleTime)) {
                mailbox_->invalidSample = true; mailbox_->ready.notify_one(); return E_FAIL;
            }
            if (mailbox_->pending) ++mailbox_->dropped;
            mailbox_->bytes.assign(buffer, buffer + mailbox_->requiredBytes);
            ++mailbox_->received; mailbox_->sequence = mailbox_->received;
            mailbox_->arrival = monotonicMs(); mailbox_->sampleTime = sampleTime; mailbox_->pending = true;
            mailbox_->sampleTimeValid = sampleTimeValid;
            mailbox_->ready.notify_one(); return S_OK;
        } catch (...) {
            std::lock_guard<std::mutex> lock(mailbox_->mutex);
            mailbox_->invalidSample = true; mailbox_->ready.notify_one(); return E_OUTOFMEMORY;
        }
    }
private:
    std::atomic<ULONG> references_{1};
    std::shared_ptr<FrameMailbox> mailbox_;
};
} // namespace

namespace capture_card_detail {
bool convertRgb24(const unsigned char* source, std::size_t bytes, int width, int height,
                  std::ptrdiff_t sourceStride, bool bottomUp,
                  std::vector<unsigned char>& bgra, std::wstring& error) {
    error.clear();
    if (!source || width <= 0 || height <= 0 || width > 16384 || height > 16384 || sourceStride < 0 ||
        static_cast<std::size_t>(sourceStride) < static_cast<std::size_t>(width) * 3) {
        error = L"RGB24 图像尺寸或行跨度无效。"; return false;
    }
    const auto stride = static_cast<std::size_t>(sourceStride);
    const auto lastRowBytes = static_cast<std::size_t>(width) * 3;
    if (lastRowBytes > bytes || (height > 1 && stride > (bytes - lastRowBytes) / static_cast<std::size_t>(height - 1))) {
        error = L"RGB24 图像字节不足。"; return false;
    }
    bgra.resize(static_cast<std::size_t>(width) * static_cast<std::size_t>(height) * 4);
    for (int y = 0; y < height; ++y) {
        const auto inputRow = static_cast<std::size_t>(bottomUp ? height - 1 - y : y);
        const auto* input = source + inputRow * stride;
        auto* output = bgra.data() + static_cast<std::size_t>(y) * static_cast<std::size_t>(width) * 4;
        for (int x = 0; x < width; ++x) {
            output[0] = input[0]; output[1] = input[1]; output[2] = input[2]; output[3] = 255;
            input += 3; output += 4;
        }
    }
    return true;
}
} // namespace capture_card_detail

struct CaptureCard::Impl {
    bool comInitialized = false, running = false;
    DWORD ownerThread = 0;
    CaptureCardInfo format;
    CaptureCardStats latest;
    std::shared_ptr<FrameMailbox> mailbox = std::make_shared<FrameMailbox>();
    ComPtr<IGraphBuilder> graph;
    ComPtr<ICaptureGraphBuilder2> builder;
    ComPtr<IBaseFilter> source, grabberFilter, sink;
    ComPtr<SampleGrabber> grabber;
    ComPtr<IMediaControl> control;
    ComPtr<IMediaEvent> events;
    ComPtr<SampleCallback> callback;
    std::vector<unsigned char> raw, bgra;
};
CaptureCard::CaptureCard() : impl_(std::make_unique<Impl>()) {}
CaptureCard::~CaptureCard() { close(); }
bool CaptureCard::enumerate(std::vector<CaptureCardDevice>& devices, std::wstring& error) {
    devices.clear(); error.clear(); ComScope com;
    // Enumeration also works in an existing UI STA; do not uninitialize it.
    if (FAILED(com.result) && com.result != RPC_E_CHANGED_MODE) { error = failure(L"无法初始化 COM", com.result); return false; }
    std::vector<ListedDevice> listed;
    if (!listDevices(listed, error)) return false;
    for (const auto& item : listed) devices.push_back(item.device);
    return true;
}
bool CaptureCard::open(const CaptureCardSettings& settings, std::wstring& error) {
    close(); error.clear();
    if (settings.width <= 0 || settings.height <= 0 || settings.width > 16384 || settings.height > 16384 ||
        !std::isfinite(settings.fps) || settings.fps <= 0.0 || settings.fps > 1000.0) {
        error = L"采集尺寸或帧率无效。"; return false;
    }
    const HRESULT initialized = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    if (FAILED(initialized)) { error = failure(L"采集必须在独立工作线程初始化 COM", initialized); return false; }
    impl_->comInitialized = true; impl_->ownerThread = GetCurrentThreadId();
    // End the attempt's local COM references before close() uninitializes COM.
    const auto attempt = [&]() -> bool {
    auto failed = [&](const wchar_t* step, HRESULT hr) { error = failure(step, hr); return false; };
    std::vector<ListedDevice> devices;
    if (!listDevices(devices, error)) return false;
    std::vector<std::size_t> matches;
    for (std::size_t i = 0; i < devices.size(); ++i) {
        const bool match = !settings.devicePath.empty() ? _wcsicmp(settings.devicePath.c_str(), devices[i].device.devicePath.c_str()) == 0 :
            !settings.deviceName.empty() && lower(devices[i].device.friendlyName).find(lower(settings.deviceName)) != std::wstring::npos;
        if (match) matches.push_back(i);
    }
    if (matches.size() != 1) {
        error = matches.empty() ? L"未找到指定采集卡。请检查 GC573 驱动或设备路径。" : L"有多个匹配的采集卡，请明确选择设备路径。";
        return false;
    }
    const auto& selected = devices[matches[0]];
    impl_->format.device = selected.device;
    HRESULT hr = CoCreateInstance(CLSID_FilterGraph, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&impl_->graph));
    if (FAILED(hr)) return failed(L"无法创建采集图", hr);
    hr = CoCreateInstance(CLSID_CaptureGraphBuilder2, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&impl_->builder));
    if (FAILED(hr)) return failed(L"无法创建采集图构建器", hr);
    hr = impl_->builder->SetFiltergraph(impl_->graph.Get());
    if (FAILED(hr)) return failed(L"无法绑定采集图", hr);
    hr = selected.moniker->BindToObject(nullptr, nullptr, IID_PPV_ARGS(&impl_->source));
    if (FAILED(hr)) return failed(L"无法打开采集卡，可能已被其他程序占用", hr);
    hr = impl_->graph->AddFilter(impl_->source.Get(), L"GC573 video source");
    if (FAILED(hr)) return failed(L"无法加入视频源", hr);
    ComPtr<IPin> sourcePin;
    hr = impl_->builder->FindPin(impl_->source.Get(), PINDIR_OUTPUT, &PIN_CATEGORY_CAPTURE, &MEDIATYPE_Video, FALSE, 0, &sourcePin);
    if (FAILED(hr)) return failed(L"无法找到采集输出引脚", hr);
    ComPtr<IAMStreamConfig> config;
    hr = sourcePin.As(&config);
    if (FAILED(hr)) return failed(L"采集卡没有格式配置接口", hr);
    int count = 0, capsSize = 0;
    hr = config->GetNumberOfCapabilities(&count, &capsSize);
    if (FAILED(hr) || count <= 0 || count > 10000 || capsSize < static_cast<int>(sizeof(VIDEO_STREAM_CONFIG_CAPS)) || capsSize > 65536)
        return failed(L"采集卡格式枚举无效", FAILED(hr) ? hr : E_UNEXPECTED);
    std::vector<unsigned char> caps(static_cast<std::size_t>(capsSize));
    MediaOwner selectedMedia;
    double bestDifference = (std::numeric_limits<double>::max)();
    for (int index = 0; index < count; ++index) {
        AM_MEDIA_TYPE* media = nullptr;
        hr = config->GetStreamCaps(index, &media, caps.data());
        if (FAILED(hr) || !media) continue;
        CaptureCardInfo candidate;
        if (mediaInfo(*media, candidate) && candidate.width == settings.width && candidate.height == settings.height) {
            const double difference = std::abs(candidate.framesPerSecond - settings.fps);
            // Preserve the driver's native interval (144.001 is a valid 144 Hz
            // mode). Do not manufacture a rate from a different advertised mode.
            if (difference <= 0.75 && difference < bestDifference) {
                selectedMedia.take(media); media = nullptr; bestDifference = difference;
            }
        }
        if (media) { freeMedia(*media); CoTaskMemFree(media); }
    }
    if (!selectedMedia.value.pbFormat) {
        error = L"采集卡未提供指定尺寸和帧率的原生 RGB24 模式；未插入 YUV 转换。";
        return false;
    }
    hr = config->SetFormat(&selectedMedia.value);
    if (FAILED(hr)) return failed(L"采集卡拒绝 RGB24 采集模式", hr);
    hr = CoCreateInstance(sampleGrabberClass, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&impl_->grabberFilter));
    if (FAILED(hr)) return failed(L"系统 Sample Grabber 不可用", hr);
    hr = impl_->grabberFilter.As(&impl_->grabber);
    if (FAILED(hr)) return failed(L"无法取得 Sample Grabber 接口", hr);
    hr = impl_->grabber->SetMediaType(&selectedMedia.value);
    if (FAILED(hr)) return failed(L"无法约束 RGB24 输入", hr);
    hr = impl_->graph->AddFilter(impl_->grabberFilter.Get(), L"Raw RGB24 samples");
    if (FAILED(hr)) return failed(L"无法加入 RGB24 接收器", hr);
    hr = CoCreateInstance(nullRendererClass, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&impl_->sink));
    if (FAILED(hr)) return failed(L"无法创建空渲染器", hr);
    hr = impl_->graph->AddFilter(impl_->sink.Get(), L"No video display");
    if (FAILED(hr)) return failed(L"无法加入空渲染器", hr);
    ComPtr<IPin> grabInput, grabOutput, sinkInput;
    if (!pin(impl_->grabberFilter.Get(), PINDIR_INPUT, grabInput) || !pin(impl_->grabberFilter.Get(), PINDIR_OUTPUT, grabOutput) ||
        !pin(impl_->sink.Get(), PINDIR_INPUT, sinkInput)) return failed(L"采集图引脚不完整", E_NOINTERFACE);
    hr = impl_->graph->ConnectDirect(sourcePin.Get(), grabInput.Get(), &selectedMedia.value);
    if (FAILED(hr)) return failed(L"原生 RGB24 直连失败，未尝试颜色转换", hr);
    hr = impl_->graph->ConnectDirect(grabOutput.Get(), sinkInput.Get(), nullptr);
    if (FAILED(hr)) return failed(L"RGB24 输出直连失败", hr);
    MediaOwner connected;
    hr = impl_->grabber->GetConnectedMediaType(&connected.value);
    if (FAILED(hr)) return failed(L"无法读取实际协商格式", hr);
    CaptureCardInfo actual;
    if (!mediaInfo(connected.value, actual) || actual.width != settings.width || actual.height != settings.height ||
        std::abs(actual.framesPerSecond - settings.fps) > 0.75) return failed(L"实际协商格式不符合 RGB24 尺寸和帧率要求", VFW_E_TYPE_NOT_ACCEPTED);
    actual.device = selected.device; impl_->format = actual;
    impl_->mailbox = std::make_shared<FrameMailbox>();
    impl_->mailbox->requiredBytes = static_cast<std::size_t>(actual.rowStride) * static_cast<std::size_t>(actual.height);
    impl_->mailbox->format = actual;
    impl_->callback.Attach(new Callback(impl_->mailbox));
    hr = impl_->grabber->SetBufferSamples(FALSE);
    if (FAILED(hr)) return failed(L"无法关闭额外采样缓存", hr);
    hr = impl_->grabber->SetOneShot(FALSE);
    if (FAILED(hr)) return failed(L"无法启用连续采样", hr);
    hr = impl_->grabber->SetCallback(impl_->callback.Get(), 0);
    if (FAILED(hr)) return failed(L"无法注册采集回调", hr);
    // Keep the graph's default reference clock. GC573 produces useful sample
    // timestamps with it; disabling it does not improve capture throughput.
    hr = impl_->graph.As(&impl_->control);
    if (FAILED(hr)) return failed(L"无法取得采集运行接口", hr);
    impl_->graph.As(&impl_->events);
    hr = impl_->control->Run();
    if (FAILED(hr)) return failed(L"采集卡无法开始传输", hr);
    impl_->running = true;
    return true;
    };
    const bool success = attempt();
    if (!success) close();
    return success;
}
bool CaptureCard::grab(unsigned timeoutMs, std::wstring& error) {
    error.clear();
    if (!impl_->running || GetCurrentThreadId() != impl_->ownerThread) {
        error = L"采集未运行，或读取线程与打开线程不同。"; return false;
    }
    if (timeoutMs == 0 || timeoutMs > 60000) { error = L"采集等待时间必须在 1–60000 毫秒之间。"; return false; }
    if (impl_->events) {
        long code = 0; LONG_PTR first = 0, second = 0;
        while (impl_->events->GetEvent(&code, &first, &second, 0) == S_OK) {
            impl_->events->FreeEventParams(code, first, second);
            if (code == EC_ERRORABORT || code == EC_DEVICE_LOST || code == EC_COMPLETE) {
                error = L"采集流已结束或设备连接中断。"; return false;
            }
        }
    }
    const auto mailbox = impl_->mailbox;
    {
        std::unique_lock<std::mutex> lock(mailbox->mutex);
        if (!mailbox->ready.wait_for(lock, std::chrono::milliseconds(timeoutMs), [&] { return mailbox->pending || mailbox->closed || mailbox->invalidSample; })) {
            error = L"等待新的采集帧超时，已停止使用旧画面。"; return false;
        }
        if (mailbox->invalidSample || mailbox->closed) { error = L"采集数据不完整或采集已经关闭。"; return false; }
        impl_->raw.swap(mailbox->bytes); mailbox->pending = false; ++mailbox->consumed;
        impl_->latest.sequence = mailbox->sequence;
        impl_->latest.arrivalMs = mailbox->arrival;
        impl_->latest.sampleTimeSeconds = mailbox->sampleTime;
        impl_->latest.sampleTimeValid = mailbox->sampleTimeValid;
    }
    return capture_card_detail::convertRgb24(impl_->raw.data(), impl_->raw.size(), impl_->format.width, impl_->format.height,
        impl_->format.rowStride, impl_->format.bottomUp, impl_->bgra, error);
}
PixelView CaptureCard::view() const {
    return {impl_->bgra.empty() ? nullptr : impl_->bgra.data(), impl_->bgra.size(), 0,
        impl_->format.width, impl_->format.height, 0, 0, static_cast<std::ptrdiff_t>(impl_->format.width) * 4};
}
int CaptureCard::width() const { return impl_->format.width; }
int CaptureCard::height() const { return impl_->format.height; }
CaptureCardInfo CaptureCard::info() const { return impl_->format; }
CaptureCardStats CaptureCard::stats() const {
    CaptureCardStats result = impl_->latest;
    std::lock_guard<std::mutex> lock(impl_->mailbox->mutex);
    result.receivedFrames = impl_->mailbox->received;
    result.consumedFrames = impl_->mailbox->consumed;
    result.droppedFrames = impl_->mailbox->dropped;
    if (result.sequence != 0) result.frameAgeMs = (std::max)(0.0, monotonicMs() - result.arrivalMs);
    return result;
}
void CaptureCard::close() {
    if (!impl_) return;
    if (impl_->control) impl_->control->Stop();
    if (impl_->grabber) impl_->grabber->SetCallback(nullptr, 1);
    { std::lock_guard<std::mutex> lock(impl_->mailbox->mutex); impl_->mailbox->closed = true; }
    impl_->mailbox->ready.notify_all();
    impl_->callback.Reset(); impl_->events.Reset(); impl_->control.Reset();
    impl_->grabber.Reset(); impl_->grabberFilter.Reset(); impl_->sink.Reset(); impl_->source.Reset();
    impl_->builder.Reset(); impl_->graph.Reset();
    impl_->running = false; impl_->raw.clear(); impl_->bgra.clear();
    impl_->latest = {}; impl_->format = {};
    if (impl_->comInitialized && impl_->ownerThread == GetCurrentThreadId()) CoUninitialize();
    impl_->comInitialized = false; impl_->ownerThread = 0;
}
} // namespace V2::Vision
