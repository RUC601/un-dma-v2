#include "vision/ContourDetection.hpp"
#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstring>
#include <limits>
#include <utility>

namespace V2::Vision {
bool PixelView::valid() const {
    if (!data || width <= 0 || height <= 0 || row0 > bytes ||
        bytes > static_cast<std::size_t>((std::numeric_limits<std::ptrdiff_t>::max)())) return false;
    const auto pitch = static_cast<std::int64_t>(rowStride);
    const auto line = static_cast<std::int64_t>(width) * 4;
    if (pitch < line && pitch > -line) return false;
    if (static_cast<std::uint64_t>(line) > bytes - row0) return false;
    if (height == 1) return true;
    if (pitch >= 0) return static_cast<std::uint64_t>(pitch) <=
        (static_cast<std::uint64_t>(bytes - row0) - line) / (height - 1);
    const auto magnitude = static_cast<std::uint64_t>(-(pitch + 1)) + 1;
    return magnitude <= row0 / (height - 1);
}
double MonotonicMs() {
    return std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now().time_since_epoch()).count();
}
bool ValidateSettings(const Settings& s, std::wstring* error) {
    const auto fail = [&](const wchar_t* text) { if (error) *error = text; return false; };
    if (error) error->clear();
    if (s.capture.width <= 0 || s.capture.height <= 0 || s.capture.width > 8192 || s.capture.height > 8192 ||
        static_cast<std::uint64_t>(s.capture.width) * s.capture.height > 33554432 ||
        !std::isfinite(s.capture.fps) || s.capture.fps < 1.0 || s.capture.fps > 1000.0)
        return fail(L"采集尺寸或帧率无效。");
    if (s.capture.devicePath.empty() && s.capture.deviceName.empty()) return fail(L"请选择采集设备。");
    if (s.capture.devicePath.size() > 32767 || s.capture.deviceName.size() > 1024 ||
        s.capture.devicePath.find(L'\0') != std::wstring::npos || s.capture.deviceName.find(L'\0') != std::wstring::npos)
        return fail(L"采集设备名称或路径无效。");
    if (s.tolerance < 0 || s.tolerance > 128 || s.roiWidth < 1 || s.roiHeight < 1 || s.roiWidth > 8192 || s.roiHeight > 8192)
        return fail(L"颜色容差或检测区域无效。");
    if (!std::isfinite(s.maxFrameAgeMs) || s.maxFrameAgeMs < 1.0 || s.maxFrameAgeMs > 1000.0 ||
        !std::isfinite(s.associationRadiusPx) || s.associationRadiusPx < 0.f || s.associationRadiusPx > 512.f ||
        !std::isfinite(s.contactRadiusPx) || s.contactRadiusPx < 0.f || s.contactRadiusPx > 64.f ||
        s.touchDelayMs < 0 || s.touchDelayMs > 1000 || s.lostGraceMs < 0 || s.lostGraceMs > 1000 ||
        s.minComponentPixels < 1 || s.minComponentPixels > 10000)
        return fail(L"帧龄、轮廓距离或确认时间无效。");
    return true;
}
bool IsFresh(const Frame& frame, const Settings& settings, double nowMs) {
    const double age = nowMs - frame.arrivalMs;
    return frame.sequence != 0 && frame.epoch != 0 && frame.width > 0 && frame.height > 0 &&
        std::isfinite(nowMs) && std::isfinite(frame.arrivalMs) &&
        std::isfinite(settings.maxFrameAgeMs) && age >= 0.0 && age <= settings.maxFrameAgeMs;
}
namespace {
std::uint8_t MatchColors(const unsigned char* pixel, const Settings& settings) {
    std::uint8_t bits = 0;
    for (std::size_t i = 0; i < settings.colors.size(); ++i) {
        const auto& color = settings.colors[i];
        if (std::abs(int(pixel[2]) - color.r) <= settings.tolerance &&
            std::abs(int(pixel[1]) - color.g) <= settings.tolerance &&
            std::abs(int(pixel[0]) - color.b) <= settings.tolerance)
            bits |= static_cast<std::uint8_t>(1u << i);
    }
    return bits;
}
}
bool DetectContours(const PixelView& image, const Settings& settings, Frame& frame, std::wstring* error) {
    frame.width = frame.height = 0;
    frame.pixels.clear(); frame.mask.clear(); frame.componentIds.clear(); frame.components.clear(); frame.detectionRoi = {};
    if (!ValidateSettings(settings, error)) return false;
    if (!image.valid() || image.originX != 0 || image.originY != 0 || image.width > 8192 || image.height > 8192 ||
        static_cast<std::uint64_t>(image.width) * image.height > 33554432) {
        if (error) *error = L"轮廓输入必须是完整、有效的原尺寸 BGRA 图像。";
        return false;
    }
    frame.width = image.width; frame.height = image.height;
    const auto count = static_cast<std::size_t>(image.width) * image.height;
    frame.pixels.resize(count * 4);
    for (int y = 0; y < image.height; ++y)
        std::memcpy(frame.pixels.data() + static_cast<std::size_t>(y) * image.width * 4,
            image.data + image.row0 + static_cast<std::ptrdiff_t>(y) * image.rowStride,
            static_cast<std::size_t>(image.width) * 4);
    frame.mask.assign(count, 0);
    frame.componentIds.assign(count, 0);
    const int rw = (std::min)(settings.roiWidth, image.width);
    const int rh = (std::min)(settings.roiHeight, image.height);
    const Rect roi{(image.width-rw)/2, (image.height-rh)/2, (image.width-rw)/2+rw, (image.height-rh)/2+rh};
    frame.detectionRoi = roi;
    for (int y = roi.top; y < roi.bottom; ++y) for (int x = roi.left; x < roi.right; ++x) {
        const auto index = static_cast<std::size_t>(y) * image.width + x;
        frame.mask[index] = MatchColors(frame.pixels.data() + index * 4, settings);
    }
    // 8-connected pieces of the union of all colours. A colour change never
    // splits the identity into a different target; target ownership is external.
    std::vector<std::uint8_t> visited(static_cast<std::size_t>(rw) * rh, 0);
    std::vector<std::uint32_t> pending;
    for (int y = roi.top; y < roi.bottom; ++y) for (int x = roi.left; x < roi.right; ++x) {
        const auto index = static_cast<std::uint32_t>(y * image.width + x);
        const auto local = static_cast<std::size_t>(y-roi.top) * rw + x-roi.left;
        if (!frame.mask[index] || visited[local]) continue;
        Component component;
        component.bounds = {x,y,x+1,y+1};
        pending.clear(); pending.push_back(index); visited[local] = 1;
        for (std::size_t next = 0; next < pending.size(); ++next) {
            const auto current = pending[next];
            const int px = static_cast<int>(current % image.width), py = static_cast<int>(current / image.width);
            component.pixelIndices.push_back(current);
            component.colorBits |= frame.mask[current];
            component.bounds.left = (std::min)(component.bounds.left, px);
            component.bounds.top = (std::min)(component.bounds.top, py);
            component.bounds.right = (std::max)(component.bounds.right, px+1);
            component.bounds.bottom = (std::max)(component.bounds.bottom, py+1);
            for (int dy = -1; dy <= 1; ++dy) for (int dx = -1; dx <= 1; ++dx) {
                const int nx = px+dx, ny = py+dy;
                if (!roi.Contains(nx, ny)) continue;
                const auto ni = static_cast<std::uint32_t>(ny * image.width + nx);
                const auto nl = static_cast<std::size_t>(ny-roi.top) * rw + nx-roi.left;
                if (frame.mask[ni] && !visited[nl]) { visited[nl] = 1; pending.push_back(ni); }
            }
        }
        if (component.pixelIndices.size() < static_cast<std::size_t>(settings.minComponentPixels)) {
            for (auto pixel : component.pixelIndices) frame.mask[pixel] = 0;
        } else {
            component.id = static_cast<std::uint32_t>(frame.components.size()+1);
            for (auto pixel : component.pixelIndices) frame.componentIds[pixel] = component.id;
            frame.components.push_back(std::move(component));
        }
    }
    return true;
}
Match NearestMatch(const Frame& frame, std::span<const Point> bones, const Rect& entityRoi, float radiusPx) {
    Match best;
    if (!std::isfinite(radiusPx) || radiusPx < 0.f || frame.width <= 0 || frame.height <= 0) return best;
    const auto pixelCount = static_cast<std::size_t>(frame.width) * frame.height;
    float limit = radiusPx * radiusPx;
    for (const auto& component : frame.components) {
        if (component.bounds.right <= entityRoi.left || component.bounds.left >= entityRoi.right ||
            component.bounds.bottom <= entityRoi.top || component.bounds.top >= entityRoi.bottom) continue;
        for (auto index : component.pixelIndices) {
            if (index >= pixelCount || index >= frame.mask.size() || !frame.mask[index]) continue;
            const int x = static_cast<int>(index % frame.width), y = static_cast<int>(index / frame.width);
            if (!entityRoi.Contains(x,y)) continue;
            for (std::size_t b = 0; b < bones.size(); ++b) {
                if (!std::isfinite(bones[b].x) || !std::isfinite(bones[b].y)) continue;
                const float dx = bones[b].x-x, dy = bones[b].y-y, distance = dx*dx+dy*dy;
                if (distance > limit || (best.found && distance == limit)) continue;
                limit = distance;
                best = {true,{static_cast<float>(x),static_cast<float>(y)},b,component.id,frame.mask[index],distance};
            }
        }
    }
    return best;
}
bool SampleColor(const Frame& frame, int x, int y, Rgb& color) {
    if (x < 0 || y < 0 || x >= frame.width || y >= frame.height) return false;
    const auto offset = (static_cast<std::size_t>(y) * frame.width + x) * 4;
    if (offset + 3 >= frame.pixels.size()) return false;
    color = {frame.pixels[offset+2],frame.pixels[offset+1],frame.pixels[offset]};
    return true;
}
} // namespace V2::Vision
