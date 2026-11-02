#pragma once
#include "LayoutProbe.hpp"
#include <mutex>
#include <atomic>

namespace V2::Profiles {
enum class Route { Rejected, Cn, Candidate };
inline std::atomic<int> requestedProfile{-1}; // -1 自动，0 CN，1 KR 候选，2 BZ 候选
inline Route ResolveRoute(bool cnImage, const Detection& detection, int requested) {
    if(detection.status==DetectionStatus::Ambiguous || detection.status==DetectionStatus::Stale)return Route::Rejected;
    if(requested<=0 && cnImage && detection.status==DetectionStatus::Matched && detection.selected==0)return Route::Cn;
    if(requested==-1 && cnImage && detection.status==DetectionStatus::NotReady)return Route::Cn;
    if(requested>=1 && requested<=2 && detection.status==DetectionStatus::Matched && detection.selected==requested)return Route::Candidate;
    return Route::Rejected;
}
struct State {
    Detection detection{};
    uint64_t module=0;
    uint32_t pid=0, timestamp=0, imageSize=0;
    bool cnImage=false;
    bool candidateSession=false;
    int candidateStatus=0;
    uint32_t candidateScanned=0,candidateCapacity=0,candidateCount=0;
};
inline std::mutex stateMutex;
inline State state;
inline State Snapshot() {std::lock_guard lock(stateMutex);return state;}
inline void Publish(State value) {std::lock_guard lock(stateMutex);state=std::move(value);}
inline const char* StatusText(DetectionStatus value) {
    switch(value) {
    case DetectionStatus::Matched:return "布局匹配";
    case DetectionStatus::NotReady:return "相机未就绪／读取未完成";
    case DetectionStatus::Ambiguous:return "多个布局匹配";
    case DetectionStatus::Stale:return "会话已改变";
    default:return "没有兼容布局";
    }
}
}
