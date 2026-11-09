#include "MapReader.hpp"
#include "Game/Overwatch.hpp"

namespace V2::Maps {
namespace {
std::mutex stateMutex;
OW::MapData::Snapshot state;
OW::offset::RuntimeProfile stateProfile = OW::offset::kProductRuntimeProfile;
}

void Clear() {
    std::lock_guard lock(stateMutex);
    state = {};
}

OW::MapData::Snapshot Snapshot() {
    std::lock_guard lock(stateMutex);
    if (!OW::RuntimeWorkersRunning() || !OW::ProcessConnection::IsConnected() ||
        stateProfile != OW::offset::ActiveProfile() ||
        !state.Fresh(GetTickCount64(), OW::ProcessConnection::ConnectionEpoch(), OW::EntitySceneGeneration()))
        return {};
    return state;
}

void Sample() {
    thread_local uint64_t lastAttempt = 0, lastConnection = 0, lastScene = 0;
    thread_local auto lastProfile = OW::offset::kProductRuntimeProfile;
    const auto now = GetTickCount64();
    const auto connection = OW::ProcessConnection::ConnectionEpoch();
    const auto scene = OW::EntitySceneGeneration();
    const auto profile = OW::offset::ActiveProfile();
    if (lastAttempt && now - lastAttempt < 500 && connection == lastConnection &&
        scene == lastScene && profile == lastProfile) return;
    lastAttempt = now;
    lastConnection = connection;
    lastScene = scene;
    lastProfile = profile;

    OW::MapData::Snapshot next;
    const auto& offsets = OW::offset::ForProfile(profile);
    const bool supported = profile == OW::offset::RuntimeProfile::Cn ||
        profile == OW::offset::RuntimeProfile::World155284 || profile == OW::offset::RuntimeProfile::Nexon155284;
    const auto read = [](uint64_t address, void* data, size_t size) {
        return OW::SDK->read_range(address, data, size);
    };
    if (supported && OW::MapData::TryRead(read, OW::ResolveSingletonList,
            OW::SDK->dwGameBase.load(), offsets.GlobalAdmin_RVA, next.id)) {
        next.connection = connection;
        next.scene = scene;
        next.sampledAtMs = GetTickCount64();
    }
    std::lock_guard lock(stateMutex);
    if (!OW::RuntimeWorkersRunning() || !OW::ProcessConnection::IsConnected() ||
        connection != OW::ProcessConnection::ConnectionEpoch() || scene != OW::EntitySceneGeneration() ||
        profile != OW::offset::ActiveProfile()) return;
    if (next.id != state.id || (next.id && next.connection != state.connection)) {
        Diagnostics::Info("[MAP] profile=%s id=0x%llX name=%s readable=%d.", offsets.name,
            static_cast<unsigned long long>(next.id), OW::MapData::DisplayName(next.id).c_str(), next.id ? 1 : 0);
    }
    state = next;
    stateProfile = profile;
}
}
