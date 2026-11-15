#pragma once

#include "output/settings/OutputProfiles.hpp"
#include <rapidjson/document.h>
#include <rapidjson/stringbuffer.h>
#include <rapidjson/writer.h>

namespace V2::Output::ProfileConfig {

inline bool HasUniqueMembers(const rapidjson::Value& value)
{
    if (!value.IsObject()) return false;
    for (auto it = value.MemberBegin(); it != value.MemberEnd(); ++it)
        for (auto next = it + 1; next != value.MemberEnd(); ++next)
            if (it->name == next->name) return false;
    return true;
}

// Decode transactionally: a malformed member must not partially overwrite any
// previously valid family. Unknown fields are ignored for forward extension.
inline bool Decode(const std::string& json, Store& destination)
{
    rapidjson::Document root;
    root.Parse<rapidjson::kParseValidateEncodingFlag>(json.data(), json.size());
    if (root.HasParseError() || !HasUniqueMembers(root) ||
        !root.HasMember("version") || !root["version"].IsInt() || root["version"].GetInt() != 1 ||
        !root.HasMember("profiles") || !HasUniqueMembers(root["profiles"])) return false;
    Store candidate;
    const auto& profiles = root["profiles"];
    std::array<bool, kFamilyCount> seen{};
    for (auto it = profiles.MemberBegin(); it != profiles.MemberEnd(); ++it) {
        std::size_t index = 0;
        while (index < kFamilyCount && it->name != kFamilyNames[index]) ++index;
        if (index == kFamilyCount) continue;
        if (seen[index] || !HasUniqueMembers(it->value)) return false;
        seen[index] = true;
        auto& profile = candidate.profiles[index];
        const auto& value = it->value;
        const auto text = [&](const char* key, std::string& target) {
            if (!value.HasMember(key)) return false;
            if (!value[key].IsString()) return false;
            target.assign(value[key].GetString(), value[key].GetStringLength());
            return true;
        };
        const auto integer = [&](const char* key, int& target) {
            if (!value.HasMember(key)) return false;
            if (!value[key].IsInt()) return false;
            target = value[key].GetInt(); return true;
        };
        const auto boolean = [&](const char* key, bool& target) {
            if (!value.HasMember(key)) return false;
            if (!value[key].IsBool()) return false;
            target = value[key].GetBool(); return true;
        };
        const auto number = [&](const char* key, float& target) {
            if (!value.HasMember(key)) return false;
            if (!value[key].IsNumber()) return false;
            const double number = value[key].GetDouble();
            if (!std::isfinite(number) || std::abs(number) > 20000.0) return false;
            target = static_cast<float>(number); return true;
        };
        if (!text("port", profile.port) || !text("instanceId", profile.instanceId) ||
            !integer("preferredDeviceId", profile.preferredDeviceId) ||
            !boolean("hasSavedParameters", profile.hasSavedParameters) ||
            !number("countsPerRadian", profile.countsPerRadian) ||
            !number("calibratedYaw", profile.calibratedYaw) ||
            !number("calibratedPitch", profile.calibratedPitch) ||
            !number("referenceGameSensitivity", profile.referenceGameSensitivity) ||
            !boolean("autoScaleByGameSensitivity", profile.autoScaleByGameSensitivity) ||
            !integer("inputDelayMs", profile.inputDelayMs) ||
            !IsValidProfile(static_cast<Family>(index), profile)) return false;
    }
    // An incomplete document cannot silently erase another family's settings.
    for (bool present : seen) if (!present) return false;
    destination = std::move(candidate);
    return true;
}

inline std::string Encode(const Store& store)
{
    rapidjson::StringBuffer buffer;
    rapidjson::Writer<rapidjson::StringBuffer, rapidjson::UTF8<>, rapidjson::ASCII<>> writer(buffer);
    writer.StartObject();
    writer.Key("version"); writer.Int(1);
    writer.Key("profiles"); writer.StartObject();
    for (std::size_t i = 0; i < kFamilyCount; ++i) {
        const auto& profile = store.profiles[i];
        writer.Key(kFamilyNames[i]); writer.StartObject();
        writer.Key("port"); writer.String(profile.port.c_str());
        writer.Key("instanceId"); writer.String(profile.instanceId.c_str());
        writer.Key("preferredDeviceId"); writer.Int(profile.preferredDeviceId);
        writer.Key("hasSavedParameters"); writer.Bool(profile.hasSavedParameters);
        writer.Key("countsPerRadian"); writer.Double(profile.countsPerRadian);
        writer.Key("calibratedYaw"); writer.Double(profile.calibratedYaw);
        writer.Key("calibratedPitch"); writer.Double(profile.calibratedPitch);
        writer.Key("referenceGameSensitivity"); writer.Double(profile.referenceGameSensitivity);
        writer.Key("autoScaleByGameSensitivity"); writer.Bool(profile.autoScaleByGameSensitivity);
        writer.Key("inputDelayMs"); writer.Int(profile.inputDelayMs);
        writer.EndObject();
    }
    writer.EndObject(); writer.EndObject();
    return {buffer.GetString(), buffer.GetSize()};
}

} // namespace V2::Output::ProfileConfig
