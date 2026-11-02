#pragma once
#include <algorithm>
#include <cstdint>
#include <cstddef>

namespace OW::PresentationTiming {
inline constexpr uint32_t kDelayMs = 16;
inline constexpr uint32_t kMaxLeadMs = 100;
inline constexpr uint32_t kAnchorFreshMs = 100;
inline constexpr uint32_t kJumpConfirmMs = 32;

inline bool ObservationWindowFresh(uint64_t start,uint64_t completed,uint64_t now,uint64_t maxAge=100) {
    return start!=0 && completed>=start && now>=completed && now-start<=maxAge;
}

struct ActorIdentity {
    uint64_t address=0, link=0, generation=0, hero=0;
    uint32_t match=0;
    bool alive=false, fresh=false;
};
inline bool SameLifetime(const ActorIdentity& previous, const ActorIdentity& current) {
    return previous.address!=0 && previous.address==current.address && previous.link==current.link &&
        previous.match==current.match && previous.hero==current.hero && !(current.alive && !previous.alive);
}
inline bool CanReuseHistory(const ActorIdentity& previous, const ActorIdentity& current) {
    return SameLifetime(previous,current) && previous.generation==current.generation &&
        previous.alive && previous.fresh && current.alive && current.fresh;
}
inline uint64_t ActorControlKey(uint64_t address,uint64_t generation) {
    if(!generation)return address;
    const uint64_t key=address^(generation*0x9E3779B97F4A7C15ull);
    return key?key:generation;
}
inline size_t AdvanceBudgetCursor(size_t start,size_t count,size_t budget) {
    return count ? (start+(std::max)(size_t{1},budget))%count : 0;
}
inline size_t AdvanceMaintenanceCursor(size_t start,size_t count,size_t visited,size_t budget) {
    return AdvanceBudgetCursor(start,count,visited>=count?budget:visited);
}
inline bool BudgetEligible(size_t index,size_t count,size_t start,size_t budget) {
    return count!=0 && (index+count-start%count)%count<budget;
}

inline bool Fresh(uint32_t sample, uint32_t now) {
    return sample != 0 && now - sample <= kAnchorFreshMs;
}

inline bool ObservationWindowFresh32(uint32_t start,uint32_t completed,uint32_t now) {
    return Fresh(start,now) && completed-start<=kAnchorFreshMs && now-completed<=kAnchorFreshMs;
}

struct SampleTime { float alpha = 1.0f; uint32_t leadMs = 0; };
inline SampleTime Evaluate(uint32_t previous, uint32_t current, uint32_t now, bool history) {
    const uint32_t interval = current - previous;
    if (!history || !previous || !current || interval == 0 || interval > 250)
        return {};
    const int64_t sinceCurrent = static_cast<int32_t>(now - current) - static_cast<int64_t>(kDelayMs);
    if (sinceCurrent < 0)
        return {std::clamp(1.0f + static_cast<float>(sinceCurrent) / interval, 0.0f, 1.0f), 0};
    return {1.0f, static_cast<uint32_t>((std::min)(sinceCurrent, static_cast<int64_t>(kMaxLeadMs)))};
}

struct JumpConfirmation {
    bool pending = false;
    uint32_t since = 0;
    unsigned observations = 0;
    void Reset() { *this = {}; }
    bool Observe(bool sameCandidate, uint32_t now) {
        if (!pending || !sameCandidate) {
            pending = true;
            since = now;
            observations = 1;
            return false;
        }
        ++observations;
        return observations >= 2 && now - since >= kJumpConfirmMs;
    }
};
}
