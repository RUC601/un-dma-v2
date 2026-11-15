#pragma once

#include "output/DeviceRegistry.hpp"
#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <string>
#include <utility>

namespace V2::Output::ProfileConfig {

enum class Family : std::size_t { Network, KmBoxSerial, Mock, Makcu, Ferrum, CatBox, Count };
inline constexpr std::size_t kFamilyCount = static_cast<std::size_t>(Family::Count);
inline constexpr std::array<const char*, kFamilyCount> kFamilyNames{
    "network", "kmboxSerial", "mock", "makcu", "ferrum", "catbox" };

inline constexpr bool IsDeviceId(int id) noexcept { return id >= 0 && id <= 6; }
inline constexpr int NormalizeDeviceId(int id) noexcept { return IsDeviceId(id) ? id : 0; }
inline constexpr Family FamilyForDeviceId(int id) noexcept
{
    switch (static_cast<DeviceId>(id)) {
    case DeviceId::KmBoxSerial: return Family::KmBoxSerial;
    case DeviceId::Mock: return Family::Mock;
    case DeviceId::Makcu4M:
    case DeviceId::Makcu115200: return Family::Makcu;
    case DeviceId::Ferrum: return Family::Ferrum;
    case DeviceId::CatBox: return Family::CatBox;
    default: return Family::Network;
    }
}
inline constexpr bool IsSerialFamily(Family family) noexcept
{
    return family == Family::KmBoxSerial || family == Family::Makcu || family == Family::Ferrum;
}
inline constexpr int DefaultDeviceId(Family family) noexcept
{
    constexpr std::array<int, kFamilyCount> ids{ 0, 1, 2, 3, 5, 6 };
    return ids[static_cast<std::size_t>(family)];
}

struct Profile {
    std::string port;
    std::string instanceId;
    int preferredDeviceId = 0;
    bool hasSavedParameters = false;
    float countsPerRadian = 100.0f;
    float calibratedYaw = 0.0f;
    float calibratedPitch = 0.0f;
    float referenceGameSensitivity = 15.0f;
    bool autoScaleByGameSensitivity = false;
    int inputDelayMs = 0;

    bool operator==(const Profile&) const = default;
};

inline Profile DefaultProfile(Family family)
{
    Profile result;
    result.preferredDeviceId = DefaultDeviceId(family);
    return result;
}

struct Store {
    std::array<Profile, kFamilyCount> profiles;
    // Runtime only: identifies which family owns the legacy field projection.
    int projectedDeviceId = -1;

    Store()
    {
        for (std::size_t i = 0; i < profiles.size(); ++i)
            profiles[i] = DefaultProfile(static_cast<Family>(i));
    }
    bool operator==(const Store&) const = default;
};
inline Store current;

inline Profile& ForId(Store& store, int id) noexcept
{
    return store.profiles[static_cast<std::size_t>(FamilyForDeviceId(id))];
}
inline const Profile& ForId(const Store& store, int id) noexcept
{
    return store.profiles[static_cast<std::size_t>(FamilyForDeviceId(id))];
}

inline bool ValidText(const std::string& value, std::size_t maximum) noexcept
{
    if (value.size() > maximum) return false;
    for (std::size_t i = 0; i < value.size();) {
        const auto c = static_cast<unsigned char>(value[i++]);
        if (c < 0x20 || c == 0x7f) return false;
        if (c < 0x80) continue;
        unsigned int code = 0;
        std::size_t extra = 0;
        unsigned int minimum = 0;
        if (c >= 0xc2 && c <= 0xdf) { code = c & 0x1f; extra = 1; minimum = 0x80; }
        else if (c >= 0xe0 && c <= 0xef) { code = c & 0x0f; extra = 2; minimum = 0x800; }
        else if (c >= 0xf0 && c <= 0xf4) { code = c & 0x07; extra = 3; minimum = 0x10000; }
        else return false;
        if (i + extra > value.size()) return false;
        while (extra--) {
            const auto next = static_cast<unsigned char>(value[i++]);
            if ((next & 0xc0) != 0x80) return false;
            code = (code << 6) | (next & 0x3f);
        }
        if (code < minimum || code > 0x10ffff || (code >= 0xd800 && code <= 0xdfff)) return false;
    }
    return true;
}
inline bool IsValidProfile(Family family, const Profile& profile) noexcept
{
    const auto inRange = [](float value, float minimum, float maximum) {
        return std::isfinite(value) && value >= minimum && value <= maximum;
    };
    return IsDeviceId(profile.preferredDeviceId) &&
        FamilyForDeviceId(profile.preferredDeviceId) == family &&
        ValidText(profile.port, 15) && ValidText(profile.instanceId, 255) &&
        inRange(profile.countsPerRadian, 0.1f, 20000.0f) &&
        inRange(profile.calibratedYaw, 0.0f, 20000.0f) &&
        inRange(profile.calibratedPitch, 0.0f, 20000.0f) &&
        inRange(profile.referenceGameSensitivity, 0.01f, 100.0f) &&
        profile.inputDelayMs >= 0 && profile.inputDelayMs <= 20;
}

inline Profile NormalizeProfile(Family family, Profile profile)
{
    const auto finiteClamp = [](float value, float minimum, float maximum, float fallback) {
        return std::isfinite(value) ? std::clamp(value, minimum, maximum) : fallback;
    };
    if (!IsDeviceId(profile.preferredDeviceId) || FamilyForDeviceId(profile.preferredDeviceId) != family)
        profile.preferredDeviceId = DefaultDeviceId(family);
    if (!ValidText(profile.port, 15)) profile.port.clear();
    if (!ValidText(profile.instanceId, 255)) profile.instanceId.clear();
    profile.countsPerRadian = finiteClamp(profile.countsPerRadian, 0.1f, 20000.0f, 100.0f);
    profile.calibratedYaw = finiteClamp(profile.calibratedYaw, 0.0f, 20000.0f, 0.0f);
    profile.calibratedPitch = finiteClamp(profile.calibratedPitch, 0.0f, 20000.0f, 0.0f);
    profile.referenceGameSensitivity = finiteClamp(profile.referenceGameSensitivity, 0.01f, 100.0f, 15.0f);
    profile.inputDelayMs = std::clamp(profile.inputDelayMs, 0, 20);
    return profile;
}

inline void MigrateLegacy(Store& store, int selectedId, const Profile& legacy)
{
    store = {};
    selectedId = NormalizeDeviceId(selectedId);
    auto migrated = legacy;
    migrated.preferredDeviceId = selectedId;
    migrated.hasSavedParameters = true;
    if (!IsSerialFamily(FamilyForDeviceId(selectedId))) {
        migrated.port.clear();
        migrated.instanceId.clear();
    }
    ForId(store, selectedId) = NormalizeProfile(FamilyForDeviceId(selectedId), std::move(migrated));
    store.projectedDeviceId = selectedId;
}

// Capture the old projection before loading another family. The two MAKCU
// baud selections share parameters, but retain the latest explicit preference.
inline Profile Synchronize(Store& store, int selectedId, const Profile& projection)
{
    selectedId = NormalizeDeviceId(selectedId);
    const int previousId = store.projectedDeviceId < 0
        ? selectedId : NormalizeDeviceId(store.projectedDeviceId);
    auto saved = NormalizeProfile(FamilyForDeviceId(previousId), projection);
    saved.preferredDeviceId = previousId;
    const auto& previous = ForId(store, previousId);
    saved.hasSavedParameters = previous.hasSavedParameters;
    if (!IsSerialFamily(FamilyForDeviceId(previousId))) {
        saved.port.clear();
        saved.instanceId.clear();
    }
    if (saved != previous) saved.hasSavedParameters = true;
    ForId(store, previousId) = std::move(saved);
    auto& selected = ForId(store, selectedId);
    if (selected.preferredDeviceId != selectedId) {
        selected.preferredDeviceId = selectedId;
        selected.hasSavedParameters = true;
    }
    store.projectedDeviceId = selectedId;
    return selected;
}

} // namespace V2::Output::ProfileConfig
