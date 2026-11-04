#pragma once
#include "Utils/Config.hpp"
namespace OW::Config::Detail {
        inline constexpr int kHeroPresetSlotCount = kMaxHeroPresetSlots;

        enum class HeroPresetSlotKind {
            Aim,
            Trigger
        };

        using HeroPresetStore = std::unordered_map<uint64_t, std::array<HeroSlotPreset, kMaxHeroPresetSlots>>;


bool TryGetHeroPresetFromStore(HeroPresetSlotKind kind, uint64_t heroId, HeroPreset& outPreset);
HeroPresetStore& PresetStore(HeroPresetSlotKind kind);
const HeroPresetStore& PresetStoreConst(HeroPresetSlotKind kind);
int ClampHeroPresetSlotIndex(int slotIndex);
std::string DefaultHeroSlotName(int slotIndex);
std::string NormalizeHeroSlotName(const std::string& name, int slotIndex);
uint64_t NewSlotIdentity();
int EnsureDefaultBehaviorModeUnlocked(int behavior, int method = -1);
void ResetHeroPresetSlot(HeroSlotPreset& slot, int slotIndex);
void InitializeBasicHeroPresetSlots(std::array<HeroSlotPreset, kHeroPresetSlotCount>& slots,
                                            uint64_t heroId,
                                            HeroPresetSlotKind kind);
int CountHeroPresetSlots(const std::array<HeroSlotPreset, kHeroPresetSlotCount>& slots);
void NormalizeHeroPresetSlots(std::array<HeroSlotPreset, kHeroPresetSlotCount>& slots,
                                      uint64_t heroId,
                                      HeroPresetSlotKind kind);
HeroPreset MakeHeroAimPresetFromCurrentUnlocked();
HeroPreset MakeHeroTriggerPresetFromCurrentUnlocked();
HeroPreset ValidateHeroPresetValueForHero(uint64_t heroId, HeroPreset preset);
}
