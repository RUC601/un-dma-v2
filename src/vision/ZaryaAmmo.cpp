#include "vision/ZaryaAmmo.hpp"
#include "vision/ContourService.hpp"
#include "HudAmmoDetail.hpp"
#include "ZaryaAmmoTemplates.hpp"
#include <algorithm>
#include <cmath>
#include <vector>

namespace V2::Vision {
namespace {
using namespace HudAmmoDetail;

bool Adjacent(const Piece& left, const Piece& right, double height) {
    const double gap = right.left - left.right;
    // Native adjacent zero glyphs can have a one-pixel gap at 20px height.
    // Compare the lower geometric bound in whole pixels rather than rejecting
    // 1px because a fractional 1.2px bound was produced by scaling.
    return gap >= (std::max)(1.0, std::floor(height * 0.06)) && gap <= height * 0.65 &&
        std::abs(right.bottom-left.bottom) <= height * 0.30 &&
        std::abs(Height(right)-height) <= height * 0.30;
}

double CannonConfidence(const HudMask& mask, const std::vector<Piece>& pieces,
                        const Piece& maximum) {
    const double height = Height(maximum);
    double confidence = 0.0;
    for (const auto& icon : pieces) {
        if (Width(icon) < height*4.5 || Width(icon) > height*9.0 ||
            Height(icon) < height*1.5 || Height(icon) > height*2.9 ||
            maximum.left-icon.right < height*2.8 ||
            maximum.left-icon.right > height*7.2 ||
            std::abs(icon.bottom-maximum.bottom) > height*0.45) continue;
        confidence = (std::max)(confidence,
            Match(Normalize(mask, icon, 64), icon, ZaryaAmmoDetail::kCannon));
    }
    return confidence >= 0.70 ? confidence : 0.0;
}
}

void DetectZaryaAmmo(Frame& frame) {
    frame.zaryaAmmoRemaining = -1;
    frame.zaryaAmmoConfidence = 0.0;
    HudMask mask;
    std::vector<Piece> pieces;
    std::vector<Candidate> digits;
    if (!Prepare(frame, mask, pieces, digits, ZaryaAmmoDetail::kDigits)) return;

    int recognized = -1;
    double confidence = 0.0;
    // The native cannon silhouette and a complete, separate maximum "100"
    // are both required. An isolated 100 (or another hero's 100-ammo weapon)
    // is not enough to identify the ammunition display.
    for (const auto& one : digits) {
        if (one.digit.value != 1) continue;
        const auto& a = pieces[one.index];
        const double height = Height(a);
        const double cannon = CannonConfidence(mask, pieces, a);
        if (cannon == 0.0) continue;
        for (const auto& zero : digits) {
            if (zero.digit.value != 0) continue;
            const auto& b = pieces[zero.index];
            if (!Adjacent(a, b, height)) continue;
            for (const auto& lastZero : digits) {
                if (lastZero.digit.value != 0) continue;
                const auto& c = pieces[lastZero.index];
                if (!Adjacent(b, c, height)) continue;
                // A clipped/extra denominator digit must not turn a different
                // capacity into a qualifying 100-ammo anchor.
                bool extraMaximum = false;
                for (const auto& digit : digits) {
                    const auto& p = pieces[digit.index];
                    if (p.left >= c.right && p.left-c.right <= height*0.65 &&
                        std::abs(p.bottom-c.bottom) <= height*0.35) {
                        extraMaximum = true; break;
                    }
                }
                if (extraMaximum) continue;

                std::vector<Candidate> current;
                bool incomplete = false;
                for (const auto& digit : digits) {
                    const auto& p = pieces[digit.index];
                    if (p.left < a.left-height*4.8 || p.right > a.left-height*0.6 ||
                        p.bottom < a.top-height*1.5 || p.top > a.bottom+height*0.5) continue;
                    if (std::abs(p.bottom-a.bottom) > height*0.35 ||
                        std::abs(Height(p)-height) > height*0.35 ||
                        digit.digit.value < 0) {
                        incomplete = true; break;
                    }
                    current.push_back(digit);
                }
                if (incomplete || current.empty() || current.size() > 3) continue;
                std::sort(current.begin(), current.end(), [&](const auto& x, const auto& y) {
                    return pieces[x.index].left < pieces[y.index].left;
                });
                const auto& last = pieces[current.back().index];
                if (a.left-last.right < height*0.75 || a.left-last.right > height*1.6) continue;
                if (current.size() > 1 && current.front().digit.value == 0) continue;
                int value = 0;
                double score = (std::min)({cannon, one.digit.confidence,
                    zero.digit.confidence, lastZero.digit.confidence});
                for (size_t i=0; i<current.size(); ++i) {
                    const auto& digit = current[i];
                    if (i>0 && !Adjacent(pieces[current[i-1].index], pieces[digit.index], height)) {
                        incomplete = true; break;
                    }
                    value = value*10+digit.digit.value;
                    score = (std::min)(score, digit.digit.confidence);
                }
                if (incomplete || value > 100) continue;
                // Multiple qualifying HUDs are ambiguous even if their values
                // happen to match. Never select one by screen position alone.
                if (recognized >= 0) return;
                recognized = value;
                confidence = score;
            }
        }
    }
    frame.zaryaAmmoRemaining = recognized;
    frame.zaryaAmmoConfidence = recognized >= 0 ? confidence : 0.0;
}

ZaryaAmmoSnapshot ReadZaryaAmmo(bool localIsZarya) {
    ZaryaAmmoSnapshot result;
    if (!localIsZarya) return result;
    Settings settings;
    const auto frame = GetService().Snapshot(settings);
    if (!frame) return result;
    const double nowMs = MonotonicMs();
    if (!IsFresh(*frame, settings, nowMs)) return result;
    result.sequence = frame->sequence;
    result.epoch = frame->epoch;
    result.arrivalMs = frame->arrivalMs;
    result.ageMs = nowMs-frame->arrivalMs;
    if (frame->zaryaAmmoRemaining < 0 || frame->zaryaAmmoRemaining > 100) return result;
    result.known = true;
    result.remaining = frame->zaryaAmmoRemaining;
    result.confidence = frame->zaryaAmmoConfidence;
    return result;
}

bool RequestZaryaAmmoCapture() {
    auto& service = GetService();
    const auto status = service.Status();
    if (status.state == ServiceState::Running || status.state == ServiceState::Starting) return true;
    auto settings = service.GetSettings();
    settings.enabled = true;
    return service.Configure(settings) && service.Start();
}
}
