#pragma once
#include <algorithm>
#include <array>
#include <cstdint>
#include <string>
#include <vector>

namespace V2::Alerts {
inline constexpr uint64_t kAnaHeroId = 0x02E000000000013Bull;
inline constexpr const char* kAnaSleepId = "ana.sleep_dart";
inline constexpr const char* kAnaGrenadeId = "ana.biotic_grenade";
inline constexpr uint64_t kEchoHeroId = 0x02E0000000000206ull;
inline constexpr const char* kEchoFlightId = "echo.flight";
inline constexpr size_t kMaxMessageBytes = 240;
struct Definition {
    const char* id;
    uint64_t hero;
    uint8_t slot;
    const char* skillName;
    const char* skillNameZh;
    const char* defaultMessage;
    bool enabledByDefault = false;
};
inline constexpr std::array kDefinitions{
    Definition{kAnaSleepId, kAnaHeroId, 0, "Sleep Dart / Shift", "睡眠针 · Shift", "安娜没睡针", true},
    Definition{kEchoFlightId, kEchoHeroId, 0, "Flight / Shift", "飞行 · Shift", "echo没飞", true},
    Definition{kAnaGrenadeId, kAnaHeroId, 1, "Biotic Grenade / E", "生化手雷 · E", "安娜没瓶"},
    Definition{"echo.focusing_beam", kEchoHeroId, 1, "Focusing Beam / E", "聚焦光线 · E", "回声没光线"}
};
inline const Definition* SkillForHero(uint64_t hero, uint8_t slot = 0) {
    for (const auto& definition : kDefinitions)
        if (definition.hero == hero && definition.slot == slot) return &definition;
    return nullptr;
}
struct RuleSettings {
    std::string id = kAnaSleepId;
    bool enabled = false;
    std::string message = "安娜没睡针";
    bool operator==(const RuleSettings&) const = default;
};
inline constexpr const char* kDefaultUltimateMessage = "敌方大招就绪";
struct UltimateRule {
    uint64_t hero = 0;
    int mode = -1; // -1 follows the global setting, 0 off, 1 on.
    std::string message; // Empty follows the global announcement.
    bool operator==(const UltimateRule&) const = default;
};
struct Settings {
    bool skillEnabled = false;
    bool ultimateEnabled = false;
    bool ultimateAll = true;
    int voice = 0; // Kokoro v1.1-zh: 0 = zf_001 (sid 3), 1 = zm_009 (sid 58).
    int volume = 80;
    std::string outputDevice; // Empty selects the system playback default.
    std::vector<RuleSettings> rules = [] {
        std::vector<RuleSettings> result;
        for (const auto& item : kDefinitions)
            result.push_back({item.id, item.enabledByDefault, item.defaultMessage});
        return result;
    }();
    std::string ultimateMessage = kDefaultUltimateMessage;
    std::vector<UltimateRule> ultimates;
    bool operator==(const Settings&) const = default;
};
bool UltimateHeroSupported(uint64_t hero);
inline const UltimateRule* UltimateForHero(const Settings& settings, uint64_t hero) {
    for (const auto& rule : settings.ultimates)
        if (rule.hero == hero) return &rule;
    return nullptr;
}
inline bool UltimateHeroEnabled(const Settings& settings, uint64_t hero) {
    const auto* rule = UltimateForHero(settings, hero);
    return rule && rule->mode >= 0 ? rule->mode == 1 : settings.ultimateAll;
}
inline const std::string& UltimateMessage(const Settings& settings, uint64_t hero) {
    const auto* rule = UltimateForHero(settings, hero);
    return rule && !rule->message.empty() ? rule->message : settings.ultimateMessage;
}
Settings Normalize(Settings settings);
Settings GetSettings();
void SetSettings(Settings settings);
bool ObserveSkillHero(uint64_t hero);
bool ObserveUltimateHero(uint64_t hero);
}
