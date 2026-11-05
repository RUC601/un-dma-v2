#pragma once
#include "Alerts/Settings.hpp"
#include <filesystem>
#include <functional>

namespace V2::Alerts {
using Cancel = std::function<bool()>;
struct AudioClip { uint32_t sampleRate = 0; std::vector<int16_t> samples; };
struct AudioDevice { std::string id, name; uint32_t index = UINT32_MAX; };
std::vector<AudioDevice> PlaybackDevices();
AudioClip ReadClip(const std::filesystem::path& path);
AudioClip PrepareSpeech(const Settings& settings, const RuleSettings& rule, const Cancel& cancelled);
bool PlaySpeech(const AudioClip& clip, const Settings& settings, const Cancel& cancelled);
}
