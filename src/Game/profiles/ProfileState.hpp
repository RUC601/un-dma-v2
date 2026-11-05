#pragma once
#include "LayoutProbe.hpp"
#include <mutex>
#include <atomic>

namespace V2::Profiles {
inline std::atomic<int> requestedProfile{-1}; // -1 Auto; otherwise index into profiles.
inline const Profile* MatchedProfile(const Detection& detection) {
    if (detection.status != DetectionStatus::Matched || detection.selected < 0 ||
        size_t(detection.selected) >= profiles.size())
        return nullptr;
    return &profiles[detection.selected];
}
struct State {
    Detection detection{};
    uint64_t module=0;
    uint32_t pid=0;
};
inline std::mutex stateMutex;
inline State state;
inline State Snapshot() {std::lock_guard lock(stateMutex);return state;}
inline void Publish(State value) {std::lock_guard lock(stateMutex);state=std::move(value);}
inline const char* StatusText(DetectionStatus value) {
    switch(value) {
    case DetectionStatus::Matched:return "布局匹配";
    case DetectionStatus::NotReady:return "等待匹配区服布局";
    case DetectionStatus::Ambiguous:return "多个布局匹配，请手选区服";
    case DetectionStatus::Stale:return "会话已改变";
    default:return "等待匹配区服布局";
    }
}
}
