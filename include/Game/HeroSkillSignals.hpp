#pragma once
#include "Game/SkillRead.hpp"

#include <array>
#include <cmath>
#include <cstdint>
#include <cstring>

namespace OW::HeroSkillSignals {
// Source-backed candidates, not a claim of current-client semantic validation.
// GOSU's scalar IDs/layout; the newer UC raw-list root is +0x1928.
// See Scenario reports/skills-audit-20261115/research-states.md.
inline constexpr uint64_t kScalarListOffset = 0x1928;
struct Sample {
    int lucioSong = -1; // 0 speed, 1 healing, -1 unknown
    uint32_t mercyTargetId = 0;
    bool mercyBeamConnected = false;
    bool mercyBeamKnown = false;
    float sojournRailCharge = -1.0f; // normalized rail charge, -1 unknown
    bool sojournRailChargeKnown = false;
    uint32_t sojournRailChargeTick = 0;
    uint32_t sampledTick = 0;
    bool dvaMechKnown = false;
    bool dvaInMech = false;
    uint32_t dvaMechTick = 0;
};

// Roster-local discovery cache. A large table is discovered in bounded pieces;
// after discovery only the requested entries are sampled. No cached value is
// published after a failed read or a changed list identity.
struct ScalarCache {
    static constexpr size_t kCapacity = 4;
    static constexpr size_t kEntriesPerDiscovery = 128;
    uint64_t skillBase = 0, container = 0;
    SkillRead::Header header{};
    std::array<uint16_t,kCapacity> ids{};
    std::array<uint64_t,kCapacity> nodes{};
    size_t count = 0, scanned = 0;
    bool complete = false;
    void Reset() { *this = {}; }
};

template<size_t N, class Reader>
bool ReadCachedScalars(uint64_t skillBase,const std::array<uint16_t,N>& ids,
                       std::array<float,N>& values,Reader&& read,ScalarCache& cache) {
    static_assert(N <= ScalarCache::kCapacity);
    const auto fail = [&]() { cache.Reset(); return false; };
    if (!SkillRead::UserRange(skillBase,kScalarListOffset+sizeof(uint64_t))) return fail();
    for (size_t i=0;i<N;++i) {
        if (!ids[i]) return fail();
        for (size_t j=0;j<i;++j) if (ids[i]==ids[j]) return fail();
    }
    uint64_t container=0;
    SkillRead::Header header{};
    if (!read(skillBase+kScalarListOffset,&container,sizeof(container)) ||
        !SkillRead::UserRange(container,0x20) ||
        !read(container+0x10,&header,sizeof(header)) || !header.count || header.count>4096 ||
        !SkillRead::UserRange(header.entries,size_t(header.count)*0x80)) return fail();
    bool sameRequest=cache.skillBase==skillBase && cache.count==N;
    for (size_t i=0;i<N && sameRequest;++i) sameRequest=cache.ids[i]==ids[i];
    if (!sameRequest) {
        cache.Reset(); cache.skillBase=skillBase; cache.container=container;
        cache.header=header; cache.count=N;
        for (size_t i=0;i<N;++i) cache.ids[i]=ids[i];
    } else if (cache.container!=container || cache.header.entries!=header.entries ||
               cache.header.count!=header.count) return fail();
    const auto stable = [&]() {
        uint64_t currentContainer=0;
        SkillRead::Header current{};
        return read(skillBase+kScalarListOffset,&currentContainer,sizeof(currentContainer)) &&
            currentContainer==container && read(container+0x10,&current,sizeof(current)) &&
            current.entries==header.entries && current.count==header.count;
    };
    if (!cache.complete) {
        std::array<unsigned char,32*0x80> chunk{};
        const size_t limit=(std::min)(size_t(header.count),cache.scanned+ScalarCache::kEntriesPerDiscovery);
        while (cache.scanned<limit) {
            const size_t count=(std::min)(limit-cache.scanned,size_t(32));
            if (!read(header.entries+cache.scanned*0x80,chunk.data(),count*0x80)) return fail();
            for (size_t i=0;i<count;++i) {
                uint16_t id=0;
                std::memcpy(&id,chunk.data()+i*0x80,sizeof(id));
                for (size_t j=0;j<N;++j) if (id==ids[j]) {
                    if (cache.nodes[j]) return fail();
                    cache.nodes[j]=header.entries+(cache.scanned+i)*0x80;
                }
            }
            cache.scanned+=count;
        }
        if (!stable()) return fail();
        if (cache.scanned<header.count) return false;
        for (size_t i=0;i<N;++i) if (!cache.nodes[i]) return fail();
        cache.complete=true;
    }
    std::array<float,N> found{};
    for (size_t i=0;i<N;++i) {
        // ID and value are read together, so a recycled entry cannot inherit an ID.
        std::array<unsigned char,0x34> node{};
        if (!read(cache.nodes[i],node.data(),node.size())) return fail();
        uint16_t id=0;
        std::memcpy(&id,node.data(),sizeof(id));
        std::memcpy(&found[i],node.data()+0x30,sizeof(float));
        if (id!=ids[i] || !std::isfinite(found[i])) return fail();
    }
    if (!stable()) return fail();
    values=found;
    return true;
}

// A toggle is sent once and waits for an observed state change. Repeated old
// samples or a missing acknowledgement must not toggle back and forth.
struct SongSwitch {
    int desired = -1, requestedFrom = -1;
    uint32_t conditionSince = 0, requestedTick = 0;
    bool waiting = false;
    void Reset() { *this = {}; }
    bool Ready(int current, int target, uint32_t sample, uint32_t now, uint32_t returnDelay = 800) {
        if ((current != 0 && current != 1) || (target != 0 && target != 1) ||
            !sample || uint32_t(now-sample)>250) { desired=-1; conditionSince=0; return false; }
        if (waiting) {
            if (int32_t(sample-requestedTick)>0 && current != requestedFrom) waiting=false;
            else return false;
        }
        if (desired != target) { desired=target; conditionSince=now; }
        return current != target && (target==1 || uint32_t(now-conditionSince)>=returnDelay);
    }
    void Submitted(int current,uint32_t now) { requestedFrom=current; requestedTick=now; waiting=true; }
};

// This flat 0x80-stride scalar list is distinct from the StateScript bucket table.
// Read all requested values together and reject ambiguous, partial or changing lists.
template<size_t N, class Reader>
bool ReadScalars(uint64_t skillBase, const std::array<uint16_t, N>& ids,
                 std::array<float, N>& values, Reader&& read, ScalarCache* cache = nullptr) {
    if (cache) return ReadCachedScalars(skillBase,ids,values,read,*cache);
    if (!SkillRead::UserRange(skillBase, kScalarListOffset + sizeof(uint64_t))) return false;
    uint64_t container = 0;
    if (!read(skillBase + kScalarListOffset, &container, sizeof(container)) ||
        !SkillRead::UserRange(container, 0x20)) return false;
    SkillRead::Header header{};
    if (!read(container + 0x10, &header, sizeof(header)) || !header.count || header.count > 4096 ||
        !SkillRead::UserRange(header.entries, size_t(header.count) * 0x80)) return false;
    std::array<float, N> found{};
    std::array<bool, N> present{};
    std::array<unsigned char, 32 * 0x80> chunk{};
    for (size_t offset = 0; offset < header.count;) {
        const size_t count = (std::min)(size_t(header.count) - offset, size_t(32));
        if (!read(header.entries + offset * 0x80, chunk.data(), count * 0x80)) return false;
        for (size_t i = 0; i < count; ++i) {
            uint16_t id = 0;
            std::memcpy(&id, chunk.data() + i * 0x80, sizeof(id));
            for (size_t j = 0; j < N; ++j) {
                if (!ids[j] || id != ids[j]) continue;
                if (present[j]) return false;
                std::memcpy(&found[j], chunk.data() + i * 0x80 + 0x30, sizeof(float));
                if (!std::isfinite(found[j])) return false;
                present[j] = true;
            }
        }
        offset += count;
    }
    uint64_t currentContainer = 0;
    SkillRead::Header current{};
    if (!read(skillBase + kScalarListOffset, &currentContainer, sizeof(currentContainer)) ||
        currentContainer != container || !read(container + 0x10, &current, sizeof(current)) ||
        current.entries != header.entries || current.count != header.count) return false;
    for (const bool exists : present) if (!exists) return false;
    values = found;
    return true;
}

template<class Reader>
int ReadLucioSong(uint64_t skillBase, Reader&& read, ScalarCache* cache = nullptr) {
    std::array<float, 1> values{};
    if (!ReadScalars(skillBase, std::array<uint16_t, 1>{0x3D9}, values, read, cache)) return -1;
    if (values[0] == 0.0f) return 0;
    if (values[0] == 1.0f) return 1;
    if (cache) cache->Reset();
    return -1;
}

// Forum capture 516727/388 identifies Sojourn's continuously changing rail
// resource as scalar script 0xC9 in component 0x36. The current reader uses
// the retained flat scalar-list candidate at +0x1928 and publishes only a
// finite normalized value in [0, 1]. This is a candidate until live CN
// sampling confirms the current client layout.
template<class Reader>
bool ReadSojournRailCharge(uint64_t skillBase, float& fraction, Reader&& read,
                           ScalarCache* cache = nullptr) {
    fraction = -1.0f;
    std::array<float, 1> values{};
    if (!ReadScalars(skillBase, std::array<uint16_t, 1>{0xC9}, values, read, cache))
        return false;
    if (!std::isfinite(values[0]) || values[0] < 0.0f || values[0] > 1.0f)
        return false;
    fraction = values[0];
    return true;
}

// Existing V2 GetHeroEngNames reads these same two LinkBase halfwords. They
// distinguish D.Va from Hana; this is a candidate state, not a health heuristic.
template<class Reader>
bool ReadDvaMech(uint64_t linkBase,bool& inMech,Reader&& read) {
    inMech=false;
    std::array<uint16_t,4> state{};
    if (!SkillRead::UserRange(linkBase,0xDC) || !read(linkBase+0xD4,state.data(),sizeof(state))) return false;
    inMech=state[0]!=state[2];
    return true;
}

template<class Reader>
Sample ReadMercyBeam(uint64_t skillBase, uint64_t targetState, Reader&& read, ScalarCache* cache = nullptr) {
    Sample result;
    const auto fail = [&]() { if (cache) cache->Reset(); return Sample{}; };
    if (!SkillRead::UserRange(targetState, 0x70)) return fail();
    // CN154905N retained raw bytes identify 0x217's current-value tag as 10 (entity
    // handle); +0x48 can be zero while +0x60 refers to a real connected target.
    std::array<uint32_t, 4> target{};
    if (!read(targetState + 0x60, target.data(), sizeof(target)) || target[3] != 10) return fail();
    std::array<float, 2> values{};
    // An incomplete bounded discovery is retained for the next sampling pass.
    if (!ReadScalars(skillBase, std::array<uint16_t, 2>{0xC7, 0x5E2}, values, read, cache)) return result;
    if ((values[0] != 0.0f && values[0] != 1.0f) ||
        (values[1] != 1.0f && values[1] != 2.0f)) return fail();
    std::array<uint32_t, 4> current{};
    if (!read(targetState + 0x60, current.data(), sizeof(current)) ||
        current[0] != target[0] || current[3] != target[3]) return fail();
    result.mercyBeamKnown = true;
    result.mercyBeamConnected = values[0] == 1.0f && values[1] == 1.0f && target[0] != 0;
    if (result.mercyBeamConnected) result.mercyTargetId = target[0];
    return result;
}
}
