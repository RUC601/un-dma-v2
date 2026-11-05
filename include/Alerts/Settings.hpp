#pragma once
#include <algorithm>
#include <array>
#include <cstdint>
#include <string>
#include <vector>

namespace V2::Alerts {
inline constexpr uint64_t kAnaHeroId = 0x02E000000000013Bull;
inline constexpr const char* kAnaSleepId = "ana.sleep_dart";
inline constexpr uint64_t kEchoHeroId = 0x02E0000000000206ull;
inline constexpr const char* kEchoFlightId = "echo.flight";
inline constexpr size_t kMaxMessageBytes = 240;
struct Definition {
    const char* id;
    uint64_t hero;
    const char* heroName;
    const char* heroNameZh;
    const char* skillName;
    const char* skillNameZh;
    const char* defaultMessage;
};
inline constexpr std::array kDefinitions{
    Definition{kAnaSleepId, kAnaHeroId, "Ana", "安娜", "Sleep Dart / Shift", "睡眠针 · Shift", "安娜没睡针"},
    Definition{kEchoFlightId, kEchoHeroId, "Echo", "回声", "Flight / Shift", "飞行 · Shift", "echo没飞"}
};
inline const Definition* SkillForHero(uint64_t hero) {
    for (const auto& definition : kDefinitions)
        if (definition.hero == hero) return &definition;
    return nullptr;
}
struct RuleSettings {
    std::string id = kAnaSleepId;
    bool enabled = true;
    std::string message = "安娜没睡针";
    bool operator==(const RuleSettings&) const = default;
};
struct Settings {
    bool enabled = false;
    int voice = 0; // Kokoro v1.1-zh: 0 = zf_001 (sid 3), 1 = zm_009 (sid 58).
    int volume = 80;
    std::string outputDevice; // Empty selects the system playback default.
    std::vector<RuleSettings> rules{RuleSettings{}, RuleSettings{kEchoFlightId, true, "echo没飞"}};
    bool operator==(const Settings&) const = default;
};
Settings Normalize(Settings settings);
Settings GetSettings();
void SetSettings(Settings settings);
bool ObserveSkillHero(uint64_t hero);
}
