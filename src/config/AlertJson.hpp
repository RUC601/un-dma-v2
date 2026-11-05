#pragma once
#include "Alerts/Settings.hpp"
#include <rapidjson/document.h>
#include <rapidjson/writer.h>
#include <rapidjson/stringbuffer.h>

namespace V2::Alerts {
inline Settings DecodeSettings(const std::string& json) {
    Settings result;
    rapidjson::Document root;
    root.Parse<rapidjson::kParseValidateEncodingFlag>(json.c_str());
    if (root.HasParseError() || !root.IsObject()) return result;
    if (root.HasMember("enabled") && root["enabled"].IsBool()) result.enabled = root["enabled"].GetBool();
    if (root.HasMember("voice") && root["voice"].IsInt()) result.voice = root["voice"].GetInt();
    if (root.HasMember("volume") && root["volume"].IsInt()) result.volume = root["volume"].GetInt();
    if (root.HasMember("outputDevice") && root["outputDevice"].IsString())
        result.outputDevice.assign(root["outputDevice"].GetString(), root["outputDevice"].GetStringLength());
    if (root.HasMember("rules") && root["rules"].IsArray()) {
        result.rules.clear();
        for (const auto& entry : root["rules"].GetArray()) {
            if (!entry.IsObject() || !entry.HasMember("id") || !entry["id"].IsString()) continue;
            RuleSettings rule;
            rule.message.clear(); // Missing text inherits this rule's catalog default.
            rule.id.assign(entry["id"].GetString(), entry["id"].GetStringLength());
            if (entry.HasMember("enabled") && entry["enabled"].IsBool()) rule.enabled = entry["enabled"].GetBool();
            if (entry.HasMember("message") && entry["message"].IsString())
                rule.message.assign(entry["message"].GetString(), entry["message"].GetStringLength());
            result.rules.push_back(std::move(rule));
        }
    }
    return Normalize(std::move(result));
}
inline std::string EncodeSettings(const Settings& raw) {
    const auto settings = Normalize(raw);
    rapidjson::StringBuffer buffer;
    // ASCII escaping preserves Chinese text through the existing ANSI INI writer.
    rapidjson::Writer<rapidjson::StringBuffer, rapidjson::UTF8<>, rapidjson::ASCII<>> writer(buffer);
    writer.StartObject();
    writer.Key("version"); writer.Int(1);
    writer.Key("enabled"); writer.Bool(settings.enabled);
    writer.Key("voice"); writer.Int(settings.voice);
    writer.Key("volume"); writer.Int(settings.volume);
    writer.Key("outputDevice"); writer.String(settings.outputDevice.c_str());
    writer.Key("rules"); writer.StartArray();
    for (const auto& rule : settings.rules) {
        writer.StartObject();
        writer.Key("id"); writer.String(rule.id.c_str());
        writer.Key("enabled"); writer.Bool(rule.enabled);
        writer.Key("message"); writer.String(rule.message.c_str());
        writer.EndObject();
    }
    writer.EndArray(); writer.EndObject();
    return {buffer.GetString(), buffer.GetSize()};
}
}
