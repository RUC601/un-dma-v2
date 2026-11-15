#pragma once
#include "vision/ContourTypes.hpp"
#include <span>

namespace V2::Vision {
// Independent, hardware-free detection. Metadata is preserved; invalid input
// clears pixel evidence. The source image must be a complete zero-origin frame.
bool DetectContours(const PixelView& image, const Settings& settings, Frame& frame,
                    std::wstring* error = nullptr);
struct Match {
    bool found = false;
    Point pixel;
    std::size_t boneIndex = 0;
    std::uint32_t componentId = 0;
    std::uint8_t colorBits = 0;
    float distanceSquared = 0.f;
};
// Finds support only within this entity's ROI and the permitted bone set.
// The caller owns cross-entity ambiguity and identity/body-generation checks.
Match NearestMatch(const Frame& frame, std::span<const Point> bones,
                   const Rect& entityRoi, float radiusPx);
bool SampleColor(const Frame& frame, int x, int y, Rgb& color);
} // namespace V2::Vision
