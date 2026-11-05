#include "Alerts/Settings.hpp"
#include <Windows.h>
#include <mutex>
#include <atomic>

namespace V2::Alerts {
namespace {
std::mutex settingsMutex;
Settings currentSettings;
std::atomic<uint64_t> observedRules{0};
static_assert(kDefinitions.size() <= 64);
}
Settings Normalize(Settings settings) {
    settings.voice = std::clamp(settings.voice, 0, 1);
    settings.volume = std::clamp(settings.volume, 0, 100);
    auto validText = [](const std::string& text, size_t limit) {
        return text.size() <= limit && text.find_first_of("\r\n\t") == std::string::npos &&
            text.find('\0') == std::string::npos && (text.empty() ||
            MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, text.data(), int(text.size()), nullptr, 0) > 0);
    };
    if (!validText(settings.outputDevice, 512)) {
        settings.outputDevice.clear();
        settings.enabled = false; // A corrupt explicit route must not redirect automatic speech to speakers.
    }
    std::vector<RuleSettings> rules;
    for (const auto& definition : kDefinitions) {
        RuleSettings rule{definition.id, true, definition.defaultMessage};
        const auto found = std::find_if(settings.rules.begin(), settings.rules.end(),
            [&](const auto& candidate) { return candidate.id == definition.id; });
        if (found != settings.rules.end()) rule = *found;
        if (rule.message.empty() || !validText(rule.message, kMaxMessageBytes))
            rule.message = definition.defaultMessage;
        rules.push_back(std::move(rule));
    }
    settings.rules = std::move(rules);
    return settings;
}
Settings GetSettings() { std::lock_guard lock(settingsMutex); return currentSettings; }
void SetSettings(Settings settings) {
    settings = Normalize(std::move(settings));
    std::lock_guard lock(settingsMutex);
    currentSettings = std::move(settings);
    uint64_t mask = 0;
    if (currentSettings.enabled)
        for (size_t index = 0; index < currentSettings.rules.size(); ++index)
            if (currentSettings.rules[index].enabled) mask |= uint64_t{1} << index;
    observedRules.store(mask, std::memory_order_release);
}
bool ObserveSkillHero(uint64_t hero) {
    const auto mask = observedRules.load(std::memory_order_acquire);
    for (size_t index = 0; index < kDefinitions.size(); ++index)
        if (kDefinitions[index].hero == hero && (mask & (uint64_t{1} << index))) return true;
    return false;
}
}
