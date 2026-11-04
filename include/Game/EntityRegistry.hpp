#pragma once
#include <algorithm>
#include <array>
#include <cstdint>
#include <cstring>
#include <mutex>
#include "Game/Cn154905.hpp"

namespace OW::EntityRegistry {
inline constexpr uint64_t stateRva=Cn154905::entityState, lockRva=Cn154905::entityLock;
inline constexpr uint32_t maxSlots=0x200000, slotBytes=16;
inline constexpr uint64_t pollMs=250, retryMs=50;
enum class Error : uint8_t { None, ReadFailed, Busy, Unstable, Invalid, Changed, Disconnected };
enum class ScanStatus : uint8_t { Complete, Partial, Invalidated, Unavailable };
struct Snapshot {
    uint64_t epoch=0, module=0, root=0, generation=0, sampledMs=0;
    uint32_t state=0, capacity=0;
    uint64_t Bytes() const { return uint64_t(capacity)*slotBytes; }
    bool SameTable(const Snapshot& b) const {
        return epoch==b.epoch && module==b.module && root==b.root && state==b.state && capacity==b.capacity;
    }
};
inline bool Valid(const Snapshot& s) {
    return s.epoch && s.module && s.capacity && s.capacity<=maxSlots && s.root>=0x10000 &&
        !(s.root&7) && s.root<0x800000000000ull && s.Bytes()<=0x800000000000ull-s.root &&
        (s.root<s.module || s.root>=s.module+Cn154905::imageSize);
}
template<class Read>
Error ReadSnapshotAt(uint64_t epoch,uint64_t module,uint64_t stateLocation,uint64_t lockLocation,Read&& read,Snapshot& out) {
    uint32_t before=0,after=0;
    std::array<uint8_t,32> a{},b{};
    if(!read(module+lockLocation,&before,sizeof(before)))return Error::ReadFailed;
    if(before)return Error::Busy;
    if(!read(module+stateLocation,a.data(),a.size()) || !read(module+stateLocation,b.data(),b.size()) ||
       !read(module+lockLocation,&after,sizeof(after)))return Error::ReadFailed;
    if(after)return Error::Busy;
    Snapshot first{},second{};
    auto decode=[&](const auto& bytes,Snapshot& s) {
        s.epoch=epoch;s.module=module;
        std::memcpy(&s.state,bytes.data(),4);std::memcpy(&s.capacity,bytes.data()+4,4);
        std::memcpy(&s.root,bytes.data()+24,8);
    };
    decode(a,first);decode(b,second);
    // Used/free counters change normally; only table identity must be stable.
    if(!first.SameTable(second))return Error::Unstable;
    if(!Valid(second))return Error::Invalid;
    out=second;return Error::None;
}
template<class Read>
Error ReadSnapshot(uint64_t epoch,uint64_t module,Read&& read,Snapshot& out) {
    return ReadSnapshotAt(epoch,module,stateRva,lockRva,read,out);
}
template<class Read,class Pause>
Error ReadSnapshotRetryBusy(uint64_t epoch,uint64_t module,Read&& read,Pause&& pause,Snapshot& out) {
    for(unsigned attempt=0;;++attempt) {
        const auto error=ReadSnapshot(epoch,module,read,out);
        if(error!=Error::Busy || attempt==2)return error;
        if(!pause())return Error::Disconnected;
    }
}
inline size_t ChunkBytes(const Snapshot& s,uint64_t offset,size_t requested) {
    return offset<s.Bytes()?static_cast<size_t>((std::min)(s.Bytes()-offset,uint64_t((std::min)(requested,size_t{0x10000})))):0;
}
struct State {
    Snapshot snapshot{};
    uint64_t lastAttemptMs=0,lastIntervalMs=0,changes=0,reads=0,failures=0,readUs=0;
    uint64_t attemptedEpoch=0,attemptedModule=0;
    Error error=Error::Disconnected;
    bool available=false,rescanPending=false,observedEmpty=false;
    bool Due(uint64_t now) const { return !reads || now-lastAttemptMs>=(error==Error::None?pollMs:retryMs); }
    bool ShouldRead(uint64_t epoch,uint64_t module,uint64_t now,bool force) const {
        return force || attemptedEpoch!=epoch || attemptedModule!=module || Due(now);
    }
    void Observe(Error next,Snapshot incoming,uint64_t now) {
        lastIntervalMs=reads?now-lastAttemptMs:0;lastAttemptMs=now;++reads;error=next;
        if(next!=Error::None) { available=false;++failures;if(next==Error::Invalid)observedEmpty=true;return; }
        attemptedEpoch=incoming.epoch;attemptedModule=incoming.module;
        if(!snapshot.generation || !snapshot.SameTable(incoming) || observedEmpty) {
            incoming.generation=snapshot.generation+1;++changes;rescanPending=true;
        } else incoming.generation=snapshot.generation;
        incoming.sampledMs=now;snapshot=incoming;available=true;observedEmpty=false;
    }
    bool Accept(const Snapshot& s) const {
        return available && s.generation && s.generation==snapshot.generation && s.SameTable(snapshot);
    }
    bool Acknowledge(const Snapshot& s,ScanStatus status) {
        if(status!=ScanStatus::Complete || !Accept(s))return false;
        rescanPending=false;return true;
    }
};
struct Monitor {
    std::mutex readMutex;
    std::mutex mutex;
    State state;
};
} // namespace OW::EntityRegistry
