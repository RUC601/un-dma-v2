#pragma once
#include <array>
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace V2::Vision {
struct Rgb {
    std::uint8_t r = 0, g = 0, b = 0;
    bool operator==(const Rgb&) const = default;
};
struct CaptureCardSettings {
    std::wstring devicePath;
    std::wstring deviceName = L"GC573";
    int width = 2560, height = 1440;
    double fps = 144.0;
    bool operator==(const CaptureCardSettings&) const = default;
};
struct CaptureCardDevice { std::wstring friendlyName, devicePath; };
struct Settings {
    bool enabled = false;
    CaptureCardSettings capture;
    bool useOutlineColors = true;
    // Bit 1: hidden, bit 2: visible, bit 4: selected. All are equally eligible;
    // colour is evidence, never an entity identity or a replacement for Vis.
    std::array<Rgb, 3> colors{{{255,255,255}, {255,0,0}, {255,255,0}}};
    int tolerance = 16;
    int roiWidth = 1000, roiHeight = 800;
    double maxFrameAgeMs = 80.0;
    float associationRadiusPx = 48.f;
    float contactRadiusPx = 3.f;
    int touchDelayMs = 12, lostGraceMs = 35;
    int minComponentPixels = 2;
    bool operator==(const Settings&) const = default;
};
struct Point { float x = 0.f, y = 0.f; };
struct Rect {
    int left = 0, top = 0, right = 0, bottom = 0;
    bool Contains(int x, int y) const { return x >= left && x < right && y >= top && y < bottom; }
};
// Caller-owned BGRA32, absolute origin and signed row stride. Adapted from
// KOI-V3-DualPC/vendor/koi/koi_pixels.hpp; no KOI controller dependency.
struct PixelView {
    const unsigned char* data = nullptr;
    std::size_t bytes = 0, row0 = 0;
    int width = 0, height = 0, originX = 0, originY = 0;
    std::ptrdiff_t rowStride = 0;
    bool valid() const;
};
struct Component {
    std::uint32_t id = 0;
    Rect bounds;
    std::uint8_t colorBits = 0;
    // Full-image linear indices, allowing target association without filling
    // an enclosed silhouette or inventing pixels behind an occluder.
    std::vector<std::uint32_t> pixelIndices;
};
struct Frame {
    int width = 0, height = 0;
    std::uint64_t sequence = 0, epoch = 0, settingsRevision = 0;
    double arrivalMs = 0.0, sampleTimeSeconds = 0.0;
    bool sampleTimeValid = false;
    Rect detectionRoi;
    std::vector<std::uint8_t> pixels;
    std::vector<std::uint8_t> mask;
    // Same full-image indexing as mask; zero is no retained piece. This lets
    // entity consumers inspect only local bone neighbourhoods at aim rate.
    std::vector<std::uint32_t> componentIds;
    std::vector<Component> components;
    std::uint8_t MatchPixel(int x, int y) const {
        if (x < 0 || y < 0 || x >= width || y >= height) return 0;
        const auto index = static_cast<std::size_t>(y) * width + x;
        return index < mask.size() ? mask[index] : 0;
    }
};
// Same local steady clock as acquisition arrivalMs; this is receiver age,
// not measured HDMI latency or a game-render timestamp.
double MonotonicMs();
bool ValidateSettings(const Settings& settings, std::wstring* error = nullptr);
bool IsFresh(const Frame& frame, const Settings& settings, double nowMs);
} // namespace V2::Vision
