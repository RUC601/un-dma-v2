#pragma once
#include "Alerts/Settings.hpp"
#include "Renderer/VisualSettings.hpp"
#include <array>
#include <cstdint>
#include <memory>
#include <string>
#include <unordered_map>

namespace V2::HeroSettings {
enum class Action : int { Primary, Secondary, Middle, Ability1, Ability2, Ability3, Ultimate, Jump, Crouch, Melee, Reload, Forward, Count };
inline constexpr std::array<int,int(Action::Count)> kDefaultKeys{1,2,4,0xA0,'E','F','Q',0x20,0xA2,'V','R','W'};
inline constexpr std::array<const char*,int(Action::Count)> kActionNames{
    "Primary fire","Secondary fire","Middle mouse","Ability 1","Ability 2","Ability 3","Ultimate","Jump","Crouch","Quick melee","Reload","Move forward"};
struct AutomaticActions {
    bool altFire=false, defensive=false, repeatFire=false;
    float altFireHealth=100,altFireDistance=30,defensiveHealth=100;
    int repeatIntervalMs=200;
};
struct Hero {
    std::array<int,int(Action::Count)> keys=kDefaultKeys;
    AutomaticActions automatic;
    bool alertsEnabled=false;
    Alerts::Settings alerts;
};
struct Store {
    int version=1;
    Hero defaults;
    VisualSettings::Settings display;
    std::unordered_map<uint64_t,Hero> heroes;
};
std::shared_ptr<const Store> Snapshot();
Hero Get(uint64_t hero);
AutomaticActions Automatic(uint64_t hero);
void Set(uint64_t hero,Hero value);
void SetDisplay(VisualSettings::Settings value);
int Key(uint64_t hero,Action action);
unsigned char HidUsage(int vk);
bool SupportedKey(int vk);
bool Decode(const std::string& json,bool commit=true);
std::string Encode();
void Migrate(AutomaticActions legacy,Alerts::Settings alerts);
}
