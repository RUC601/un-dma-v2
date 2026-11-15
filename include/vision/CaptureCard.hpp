#pragma once
// V2-owned adaptation of KOI-V3-DualPC/src/capture_card.hpp (2026-11-15).
#include "vision/ContourTypes.hpp"
#include <memory>

namespace V2::Vision {
struct CaptureCardInfo {
    CaptureCardDevice device;
    int width = 0, height = 0, bitsPerPixel = 0;
    std::ptrdiff_t rowStride = 0;
    bool bottomUp = false;
    double framesPerSecond = 0.0;
    std::wstring format;
};
struct CaptureCardStats {
    std::uint64_t receivedFrames = 0, consumedFrames = 0, droppedFrames = 0, sequence = 0;
    double arrivalMs = 0.0, sampleTimeSeconds = 0.0, frameAgeMs = 0.0;
    bool sampleTimeValid = false;
};
// Open, grab and close on the same owner thread. A driver callback publishes
// the latest complete RGB24 frame; view is top-down BGRA and caller-borrowed.
class CaptureCard {
public:
    CaptureCard();
    ~CaptureCard();
    CaptureCard(const CaptureCard&) = delete;
    CaptureCard& operator=(const CaptureCard&) = delete;
    static bool enumerate(std::vector<CaptureCardDevice>& devices, std::wstring& error);
    bool open(const CaptureCardSettings& settings, std::wstring& error);
    bool grab(unsigned timeoutMs, std::wstring& error);
    PixelView view() const;
    int width() const;
    int height() const;
    CaptureCardInfo info() const;
    CaptureCardStats stats() const;
    void close();
private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
namespace capture_card_detail {
bool convertRgb24(const unsigned char* source, std::size_t bytes, int width, int height,
                  std::ptrdiff_t sourceStride, bool bottomUp,
                  std::vector<unsigned char>& bgra, std::wstring& error);
}
} // namespace V2::Vision
