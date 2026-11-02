#pragma once

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <mutex>
#include <thread>
#include <vector>
#include <utility>
#include <unordered_map>
#include "Game/EntityRegistry.hpp"
#include "Game/EntityRosterPolicy.hpp"
#include "Utils/ProcessConnection.hpp"

namespace OW {

inline std::atomic<bool> runtimeWorkersRunning{ true };
inline std::atomic<uint64_t> entitySceneGeneration{1};
inline std::atomic<uint64_t> rejectedEntitySceneResults{0};
inline uint64_t EntitySceneGeneration() noexcept {
    return entitySceneGeneration.load(std::memory_order_acquire);
}
inline std::mutex entityBodyValidityMutex;
inline std::unordered_map<uint64_t, uint64_t> activeEntityBodies;
inline std::unordered_map<uint64_t, EntityRosterPolicy::Discovery> entityBodyDiscoveries;
inline std::unordered_map<uint64_t, uint32_t> blockedEntityBodies;
inline std::unordered_map<uint64_t, uint64_t> entityBodyAbsenceRevisions;
inline EntityRegistry::Monitor entityRegistryMonitor;
struct EntityBodyAuthority {
    EntityRosterPolicy::Discovery identity{};
    uint64_t generation=0,epoch=0,scene=0,registry=0,absence=0,revision=0;
    bool alive=false,admitted=false;
};
inline std::unordered_map<uint64_t,EntityBodyAuthority> entityBodyAuthorities;
// Caller holds entityBodyValidityMutex after changing scene/registry or draining workers.
inline void ClearEntityBodyAuthoritiesLocked() {
    activeEntityBodies.clear();entityBodyAuthorities.clear();
}
// Never reset: a local actor generation may be reused after context cleanup.
inline uint64_t entityBodyAuthoritySequence=0;
struct EntityBodyObservation { uint64_t fence=0,absence=0; };
inline EntityBodyObservation CaptureEntityBodyObservation(uint64_t address) {
    std::lock_guard<std::mutex> lock(entityBodyValidityMutex);
    const auto found=entityBodyAbsenceRevisions.find(address);
    return {entityBodyAuthoritySequence,found==entityBodyAbsenceRevisions.end()?0:found->second};
}
inline uint64_t EntityBodyAbsenceRevision(uint64_t address) {
    std::lock_guard<std::mutex> lock(entityBodyValidityMutex);
    const auto found=entityBodyAbsenceRevisions.find(address);
    return found==entityBodyAbsenceRevisions.end()?0:found->second;
}
inline void RecordEntityBodyDiscovery(const EntityRosterPolicy::Discovery& value) {
    std::lock_guard<std::mutex> lock(entityBodyValidityMutex);
    const auto old=entityBodyDiscoveries.find(value.address);
    if(old!=entityBodyDiscoveries.end() && int32_t(value.tick-old->second.tick)<=0)return;
    if(old!=entityBodyDiscoveries.end() && (old->second.link!=value.link || old->second.hero!=value.hero || old->second.match!=value.match)) {
        activeEntityBodies.erase(value.address);
        if(auto authority=entityBodyAuthorities.find(value.address);authority!=entityBodyAuthorities.end()) {
            authority->second.revision=++entityBodyAuthoritySequence;authority->second.admitted=false;
        }
    }
    entityBodyDiscoveries[value.address]=value;
    const auto blocked=blockedEntityBodies.find(value.address);
    if(blocked!=blockedEntityBodies.end() && int32_t(value.tick-blocked->second)>0)blockedEntityBodies.erase(blocked);
}
inline void BlockEntityBody(uint64_t address,uint32_t now) {
    std::lock_guard<std::mutex> lock(entityBodyValidityMutex);
    activeEntityBodies.erase(address);blockedEntityBodies[address]=now;
    ++entityBodyAbsenceRevisions[address];
    if(auto authority=entityBodyAuthorities.find(address);authority!=entityBodyAuthorities.end()) {
        authority->second.revision=++entityBodyAuthoritySequence;authority->second.admitted=false;
    }
}
inline bool EntityBodyCurrent(uint64_t address, uint64_t generation) {
    std::lock_guard<std::mutex> lock(entityBodyValidityMutex);
    const auto found = activeEntityBodies.find(address);
    return found != activeEntityBodies.end() && found->second == generation;
}
inline void InvalidateEntityBody(uint64_t address,uint64_t generation,uint64_t revision) {
    std::lock_guard<std::mutex> lock(entityBodyValidityMutex);
    const auto authority=entityBodyAuthorities.find(address);
    if(authority!=entityBodyAuthorities.end() && authority->second.generation==generation && authority->second.revision==revision)
        activeEntityBodies.erase(address);
}
inline void RevokeEntityBodyObservation(uint64_t address,uint64_t generation,uint64_t revision) {
    std::lock_guard<std::mutex> lock(entityBodyValidityMutex);
    const auto authority=entityBodyAuthorities.find(address);
    if(authority!=entityBodyAuthorities.end() && authority->second.generation==generation && authority->second.revision==revision) {
        activeEntityBodies.erase(address);
        authority->second.admitted=false;
        authority->second.revision=++entityBodyAuthoritySequence;
    }
}
inline bool EntityBodyEvidenceCurrentLocked(const EntityRosterPolicy::Discovery& identity,
    uint64_t scene,uint32_t now,uint64_t absenceRevision) {
    const auto evidence=entityBodyDiscoveries.find(identity.address);
    const auto absence=entityBodyAbsenceRevisions.find(identity.address);
    const auto currentAbsence=absence==entityBodyAbsenceRevisions.end()?0:absence->second;
    if(scene!=EntitySceneGeneration() || absenceRevision!=currentAbsence || blockedEntityBodies.contains(identity.address) ||
        evidence==entityBodyDiscoveries.end() || evidence->second.link!=identity.link ||
        evidence->second.hero!=identity.hero || evidence->second.match!=identity.match ||
        EntityRosterPolicy::Expired(evidence->second.tick,now)) return false;
    return true;
}
// Only the verified observation/identity reconciliation path calls this, never publication.
inline uint64_t AdmitEntityBodyObservation(const EntityRosterPolicy::Discovery& identity,uint64_t generation,
    uint64_t scene,const EntityRegistry::Snapshot& registry,EntityBodyObservation observation,
    bool alive,bool healthObserved,uint32_t now) {
    std::lock_guard<std::mutex> registryLock(entityRegistryMonitor.mutex);
    std::lock_guard<std::mutex> lock(entityBodyValidityMutex);
    if(!generation || !runtimeWorkersRunning.load(std::memory_order_acquire) || !ProcessConnection::IsConnected() ||
        ProcessConnection::ConnectionEpoch()!=registry.epoch || !entityRegistryMonitor.state.Accept(registry) ||
        !EntityBodyEvidenceCurrentLocked(identity,scene,now,observation.absence))return 0;
    auto found=entityBodyAuthorities.find(identity.address);
    if(found!=entityBodyAuthorities.end() && found->second.revision>observation.fence)return 0;
    const bool changed=found==entityBodyAuthorities.end() || !found->second.admitted ||
        found->second.generation!=generation || found->second.epoch!=registry.epoch || found->second.scene!=scene ||
        found->second.registry!=registry.generation || found->second.absence!=observation.absence ||
        found->second.identity.link!=identity.link || found->second.identity.hero!=identity.hero ||
        found->second.identity.match!=identity.match || found->second.alive!=alive;
    if(changed) {
        // Cached health/discovery cannot establish a new lifetime or a trusted death.
        if(!healthObserved)return 0;
        const auto revision=++entityBodyAuthoritySequence;
        entityBodyAuthorities[identity.address]={identity,generation,registry.epoch,scene,registry.generation,
            observation.absence,revision,alive,true};
        activeEntityBodies.erase(identity.address);
        return revision;
    }
    return found->second.revision;
}
inline bool EntityBodyAuthorityMatchesLocked(const EntityRosterPolicy::Discovery& identity,uint64_t generation,
    uint64_t revision,uint64_t epoch,uint64_t scene,uint64_t absenceRevision) {
    const auto found=entityBodyAuthorities.find(identity.address);
    if(!revision || found==entityBodyAuthorities.end())return false;
    const auto& a=found->second;
    return a.admitted && a.revision==revision && a.generation==generation && a.epoch==epoch && a.scene==scene &&
        a.absence==absenceRevision && a.identity.link==identity.link && a.identity.hero==identity.hero && a.identity.match==identity.match;
}
// Caller holds registry then body locks through validation AND snapshot replacement.
inline bool TryActivateEntityBodyLocked(const EntityRosterPolicy::Discovery& identity,uint64_t generation,
    uint64_t scene,uint32_t now,uint64_t absenceRevision=0,uint64_t revision=0,uint64_t epoch=0) {
    if(!EntityBodyAuthorityMatchesLocked(identity,generation,revision,epoch,scene,absenceRevision) ||
        !entityBodyAuthorities.at(identity.address).alive || !EntityBodyEvidenceCurrentLocked(identity,scene,now,absenceRevision))return false;
    if(!ProcessConnection::IsConnected() || ProcessConnection::ConnectionEpoch()!=epoch || !entityRegistryMonitor.state.available ||
        entityBodyAuthorities.at(identity.address).registry!=entityRegistryMonitor.state.snapshot.generation)return false;
    activeEntityBodies[identity.address]=generation;
    return true;
}
inline bool TryActivateEntityBody(const EntityRosterPolicy::Discovery& identity,uint64_t generation,
    uint64_t scene,uint32_t now,uint64_t absenceRevision=0,uint64_t revision=0,uint64_t epoch=0) {
    std::lock_guard<std::mutex> registryLock(entityRegistryMonitor.mutex);
    std::lock_guard<std::mutex> lock(entityBodyValidityMutex);
    return TryActivateEntityBodyLocked(identity,generation,scene,now,absenceRevision,revision,epoch);
}
inline bool EntityBodyCurrent(const EntityRosterPolicy::Discovery& identity,uint64_t generation,
    uint64_t revision,uint64_t epoch,uint64_t scene,uint64_t absenceRevision) {
    // Match publication order; a committed table change revokes held copies before owner cleanup.
    std::lock_guard<std::mutex> registryLock(entityRegistryMonitor.mutex);
    std::lock_guard<std::mutex> lock(entityBodyValidityMutex);
    const auto active=activeEntityBodies.find(identity.address);
    return active!=activeEntityBodies.end() && active->second==generation &&
        entityRegistryMonitor.state.available && entityRegistryMonitor.state.snapshot.epoch==epoch &&
        entityBodyAuthorities.contains(identity.address) &&
        entityBodyAuthorities.at(identity.address).registry==entityRegistryMonitor.state.snapshot.generation &&
        scene==EntitySceneGeneration() && ProcessConnection::IsConnected() && ProcessConnection::ConnectionEpoch()==epoch &&
        EntityBodyAuthorityMatchesLocked(identity,generation,revision,epoch,scene,absenceRevision);
}
inline std::mutex runtimeWorkerWaitMutex;
inline std::condition_variable runtimeWorkerWaitCondition;

inline bool RuntimeWorkersRunning() noexcept
{
    return runtimeWorkersRunning.load(std::memory_order_acquire);
}

inline void SetRuntimeWorkersRunning(bool running)
{
    {
        std::lock_guard<std::mutex> lock(runtimeWorkerWaitMutex);
        runtimeWorkersRunning.store(running, std::memory_order_release);
    }
    if (!running)
        runtimeWorkerWaitCondition.notify_all();
}

// Lifecycle commits never wait for workers while holding this mutex.
inline std::mutex runtimeLifecycleMutex;
inline std::atomic<bool> runtimeShuttingDown{ false };

inline bool RuntimeShutdownRequested() noexcept
{
    return runtimeShuttingDown.load(std::memory_order_acquire);
}

template <typename Commit>
inline bool TryCommitRuntimeOperation(Commit&& commit)
{
    std::lock_guard<std::mutex> lock(runtimeLifecycleMutex);
    if (RuntimeShutdownRequested())
        return false;
    std::forward<Commit>(commit)();
    return true;
}

template <typename DisableOutput>
inline void RequestRuntimeShutdown(DisableOutput&& disableOutput)
{
    std::lock_guard<std::mutex> lock(runtimeLifecycleMutex);
    runtimeShuttingDown.store(true, std::memory_order_release);
    std::forward<DisableOutput>(disableOutput)();
    SetRuntimeWorkersRunning(false);
}

// The lock also lets offline tests establish that every waiter has entered wait.
inline bool RuntimeWorkerSleepLocked(std::unique_lock<std::mutex>& lock, uint32_t durationMs)
{
    runtimeWorkerWaitCondition.wait_for(lock, std::chrono::milliseconds(durationMs), [] {
        return !RuntimeWorkersRunning();
    });
    return RuntimeWorkersRunning();
}

#ifdef UNLEASHED_DISCOVERY_TEST
inline bool (*runtimeWorkerSleepTest)(uint32_t)=nullptr;
#endif
inline bool RuntimeWorkerSleep(uint32_t durationMs)
{
#ifdef UNLEASHED_DISCOVERY_TEST
    if(runtimeWorkerSleepTest)return runtimeWorkerSleepTest(durationMs);
#endif
    if (durationMs == 0) {
        std::this_thread::yield();
        return RuntimeWorkersRunning();
    }
    std::unique_lock<std::mutex> lock(runtimeWorkerWaitMutex);
    return RuntimeWorkerSleepLocked(lock, durationMs);
}

inline thread_local bool entityScanComplete = false;
inline thread_local bool entityScanReadComplete = false;
inline thread_local EntityRegistry::Snapshot entityScanRegistry{};
inline thread_local EntityRegistry::ScanStatus entityScanStatus=EntityRegistry::ScanStatus::Unavailable;
struct EntityTopologySample {
    std::vector<uint64_t> members;
    bool complete=false;
    uint64_t root=0;
    EntityRegistry::Snapshot registry{};
    std::vector<std::pair<uint64_t,uint32_t>> member_ids;
    bool member_ids_complete=false;
};
inline constexpr uint64_t kViewMatrixSampleGraceMs = 100;

struct ViewMatrixSampleState {
    bool valid = false;
    uint64_t lastSuccessfulSampleMs = 0;
    uint64_t connectionEpoch = 0;
    uint64_t readStartedMs = 0;
    uint64_t readCompletedMs = 0;
    uint64_t publishSequence = 0;
};

inline bool IsViewMatrixSampleFresh(const ViewMatrixSampleState& sample,
                                   uint64_t nowMs,
                                   uint64_t currentConnectionEpoch,
                                   bool connected) noexcept
{
    return sample.valid && connected && sample.connectionEpoch != 0 &&
        sample.connectionEpoch == currentConnectionEpoch &&
        sample.lastSuccessfulSampleMs != 0 &&
        nowMs >= sample.lastSuccessfulSampleMs &&
        nowMs - sample.lastSuccessfulSampleMs <= kViewMatrixSampleGraceMs;
}

inline bool IsPerkHotkeyReservedForMenu(int menuToggleKey, int perkHotkey) noexcept
{
    return menuToggleKey == perkHotkey;
}

struct SampledToggleState {
    bool previousDown = false;
    bool releaseRequired = false;
    uint64_t connectionEpoch = 0;
    int bindingVk = 0;
};

inline bool UpdateSampledToggle(SampledToggleState& state, bool sampleAvailable,
                                bool down, bool& active, uint64_t connectionEpoch = 0,
                                int bindingVk = 0) noexcept
{
    // A new session or binding cannot inherit a latch or held key from the old one.
    if (connectionEpoch != 0 &&
        (state.connectionEpoch != connectionEpoch || state.bindingVk != bindingVk)) {
        state.connectionEpoch = connectionEpoch;
        state.bindingVk = bindingVk;
        state.previousDown = false;
        state.releaseRequired = true;
        active = false;
    }
    if (!sampleAvailable) {
        active = false;
        state.previousDown = false;
        state.releaseRequired = true;
        return false;
    }
    if (state.releaseRequired) {
        if (!down)
            state.releaseRequired = false;
        state.previousDown = down;
        return false;
    }
    if (down && !state.previousDown)
        active = !active;
    state.previousDown = down;
    return active;
}

} // namespace OW
