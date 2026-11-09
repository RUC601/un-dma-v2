#include "Game/HeroSettings.hpp"
#include "Alerts/Settings.hpp"
#include "Game/GameData.hpp"
#include <Windows.h>
#include <mutex>
#include <atomic>

namespace V2::Alerts {
namespace {
std::mutex settingsMutex;
Settings currentSettings;
std::atomic<uint64_t> runtimeHero{0};
std::atomic<uint64_t> observedRules{0};
std::atomic<uint64_t> observedUltimates{0};
static_assert(kDefinitions.size() <= 64);
// Playable heroes are listed before the training bot entries.
constexpr size_t kPlayableHeroCount = [] {
    size_t i = 0;
    while (i < std::size(OW::GameData::kHeroIds) && OW::GameData::kHeroIds[i].value != OW::GameData::MakeHeroId(0x33C)) ++i;
    return i;
}();
static_assert(kPlayableHeroCount <= 64);
uint64_t UltimateHeroBit(uint64_t hero) {
    for (size_t i = 0; i < kPlayableHeroCount; ++i)
        if (OW::GameData::kHeroIds[i].value == hero) return uint64_t{1} << i;
    return 0;
}
}
bool UltimateHeroSupported(uint64_t hero) { return UltimateHeroBit(hero) != 0; }
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
        settings.skillEnabled = settings.ultimateEnabled = false;
    }
    std::vector<RuleSettings> rules;
    for (const auto& definition : kDefinitions) {
        RuleSettings rule{definition.id, definition.enabledByDefault, definition.defaultMessage};
        const auto found = std::find_if(settings.rules.begin(), settings.rules.end(),
            [&](const auto& candidate) { return candidate.id == definition.id; });
        if (found != settings.rules.end()) rule = *found;
        if (rule.message.empty() || !validText(rule.message, kMaxMessageBytes))
            rule.message = definition.defaultMessage;
        rules.push_back(std::move(rule));
    }
    settings.rules = std::move(rules);
    if (settings.ultimateMessage.empty() || !validText(settings.ultimateMessage, kMaxMessageBytes))
        settings.ultimateMessage = kDefaultUltimateMessage;
    std::vector<UltimateRule> ultimates;
    for (const auto& item : OW::GameData::kHeroIds) {
        if (!UltimateHeroSupported(item.value)) continue;
        const auto* rule = UltimateForHero(settings, item.value);
        if (!rule) continue;
        auto normalized = *rule;
        normalized.mode = std::clamp(normalized.mode, -1, 1);
        if (!validText(normalized.message, kMaxMessageBytes)) normalized.message.clear();
        if (normalized.mode >= 0 || !normalized.message.empty()) ultimates.push_back(std::move(normalized));
    }
    settings.ultimates = std::move(ultimates);
    return settings;
}
Settings GetSettings() { std::lock_guard lock(settingsMutex); return currentSettings; }
Settings GetRuntimeSettings() {
    const auto hero=V2::HeroSettings::Get(runtimeHero.load(std::memory_order_acquire));
    auto settings=hero.alerts;
    const auto shared=GetSettings();
    settings.voice=shared.voice;settings.volume=shared.volume;settings.outputDevice=shared.outputDevice;
    if(!hero.alertsEnabled)settings.skillEnabled=settings.ultimateEnabled=false;
    return settings;
}
void SetRuntimeHero(uint64_t hero) {
    runtimeHero.store(hero,std::memory_order_release);
    const auto settings=GetRuntimeSettings();
    uint64_t mask=0;
    if(settings.skillEnabled)for(size_t i=0;i<settings.rules.size();++i)
        if(settings.rules[i].enabled)mask|=uint64_t{1}<<i;
    observedRules.store(mask,std::memory_order_release);mask=0;
    if(settings.ultimateEnabled)for(size_t i=0;i<kPlayableHeroCount;++i)
        if(UltimateHeroEnabled(settings,OW::GameData::kHeroIds[i].value))mask|=uint64_t{1}<<i;
    observedUltimates.store(mask,std::memory_order_release);
}
void SetSettings(Settings settings) {
    settings=Normalize(std::move(settings));
    {std::lock_guard lock(settingsMutex);currentSettings=std::move(settings);}
    SetRuntimeHero(runtimeHero.load(std::memory_order_acquire));
}
bool ObserveSkillHero(uint64_t hero) {
    const auto mask = observedRules.load(std::memory_order_acquire);
    for (size_t index = 0; index < kDefinitions.size(); ++index)
        if (kDefinitions[index].hero == hero && (mask & (uint64_t{1} << index))) return true;
    return false;
}
bool ObserveUltimateHero(uint64_t hero) {
    const auto mask = observedUltimates.load(std::memory_order_acquire);
    return mask && (mask & UltimateHeroBit(hero));
}
}
