#include "SlotStorageInternal.hpp"
#include "Utils/InputLabels.hpp"
// 所有公开读写经 Config::mutex / ScopedConfigAuthority；序列化共享相同规范化规则。
// UI 命令不返回容器引用，worker 读取配置快照中的值，删除不会留下悬空 Slot。
namespace OW::Config {
using namespace Detail;
    bool Detail::TryGetHeroPresetFromStore(HeroPresetSlotKind kind, uint64_t heroId, HeroPreset& outPreset)
    {
        const auto& store = PresetStoreConst(kind);
        const auto item = store.find(heroId);
        if (item == store.end())
            return false;

        for (const HeroSlotPreset& slot : item->second) {
            if (slot.present && slot.enabled) {
                outPreset = ValidateHeroPresetValueForHero(heroId, slot.preset);
                return true;
            }
        }
        return false;
    }

    bool TryGetHeroPreset(uint64_t heroId, HeroPreset& outPreset)
    {
        std::lock_guard<std::mutex> lock(mutex);
        ScopedConfigAuthority authority(false);
        return TryGetHeroPresetFromStore(HeroPresetSlotKind::Aim, heroId, outPreset);
    }

    bool TryGetHeroAimPreset(uint64_t heroId, HeroPreset& outPreset)
    {
        std::lock_guard<std::mutex> lock(mutex);
        ScopedConfigAuthority authority(false);
        return TryGetHeroPresetFromStore(HeroPresetSlotKind::Aim, heroId, outPreset);
    }

    bool TryGetHeroTriggerPreset(uint64_t heroId, HeroPreset& outPreset)
    {
        std::lock_guard<std::mutex> lock(mutex);
        ScopedConfigAuthority authority(false);
        return TryGetHeroPresetFromStore(HeroPresetSlotKind::Trigger, heroId, outPreset);
    }

    static bool HasHeroPresetInStore(HeroPresetSlotKind kind, uint64_t heroId)
    {
        const auto& store = PresetStoreConst(kind);
        const auto item = store.find(heroId);
        return item != store.end() && CountHeroPresetSlots(item->second) > 0;
    }

    bool HasHeroPreset(uint64_t heroId)
    {
        std::lock_guard<std::mutex> lock(mutex);
        ScopedConfigAuthority authority(false);
        return HasHeroPresetInStore(HeroPresetSlotKind::Aim, heroId) ||
            HasHeroPresetInStore(HeroPresetSlotKind::Trigger, heroId);
    }

    bool HasHeroAimPreset(uint64_t heroId)
    {
        std::lock_guard<std::mutex> lock(mutex);
        ScopedConfigAuthority authority(false);
        return HasHeroPresetInStore(HeroPresetSlotKind::Aim, heroId);
    }

    bool HasHeroTriggerPreset(uint64_t heroId)
    {
        std::lock_guard<std::mutex> lock(mutex);
        ScopedConfigAuthority authority(false);
        return HasHeroPresetInStore(HeroPresetSlotKind::Trigger, heroId);
    }

    HeroPreset GetHeroPresetOrDefault(uint64_t heroId)
    {
        return GetHeroPresetOrDefault(heroId, 0);
    }

    static HeroPreset GetHeroPresetOrDefaultFromStore(HeroPresetSlotKind kind, uint64_t heroId, int slotIndex)
    {
        const auto& store = PresetStoreConst(kind);
        const auto item = store.find(heroId);
        const int clampedSlotIndex = ClampHeroPresetSlotIndex(slotIndex);
        if (item != store.end() &&
            item->second[static_cast<size_t>(clampedSlotIndex)].present)
            return ValidateHeroPresetValueForHero(heroId, item->second[static_cast<size_t>(clampedSlotIndex)].preset);
        return kind == HeroPresetSlotKind::Aim
            ? MakeHeroAimPresetFromCurrentUnlocked()
            : MakeHeroTriggerPresetFromCurrentUnlocked();
    }

    HeroPreset GetHeroPresetOrDefault(uint64_t heroId, int slotIndex)
    {
        std::lock_guard<std::mutex> lock(mutex);
        ScopedConfigAuthority authority(false);
        return GetHeroPresetOrDefaultFromStore(HeroPresetSlotKind::Aim, heroId, slotIndex);
    }

    HeroPreset GetHeroAimPresetOrDefault(uint64_t heroId, int slotIndex)
    {
        std::lock_guard<std::mutex> lock(mutex);
        ScopedConfigAuthority authority(false);
        return GetHeroPresetOrDefaultFromStore(HeroPresetSlotKind::Aim, heroId, slotIndex);
    }

    HeroPreset GetHeroTriggerPresetOrDefault(uint64_t heroId, int slotIndex)
    {
        std::lock_guard<std::mutex> lock(mutex);
        ScopedConfigAuthority authority(false);
        return GetHeroPresetOrDefaultFromStore(HeroPresetSlotKind::Trigger, heroId, slotIndex);
    }

    void SetHeroPreset(uint64_t heroId, const HeroPreset& preset)
    {
        SetHeroPreset(heroId, 0, preset);
    }

    static std::array<HeroSlotPreset, kHeroPresetSlotCount>& EnsureHeroPresetSlotsInStore(
        HeroPresetSlotKind kind,
        uint64_t heroId)
    {
        auto& store = PresetStore(kind);
        auto [item, inserted] = store.try_emplace(heroId);
        if (inserted)
            InitializeBasicHeroPresetSlots(item->second, heroId, kind);
        else
            NormalizeHeroPresetSlots(item->second, heroId, kind);
        return item->second;
    }

    static void EnsureHeroPresetSlotRange(std::array<HeroSlotPreset, kHeroPresetSlotCount>& slots,
                                          int slotIndex)
    {
        const int clampedSlotIndex = ClampHeroPresetSlotIndex(slotIndex);
        const int presentCount = CountHeroPresetSlots(slots);
        for (int index = presentCount; index <= clampedSlotIndex; ++index) {
            HeroSlotPreset& slot = slots[static_cast<size_t>(index)];
            ResetHeroPresetSlot(slot, index);
            slot.present = true;
            slot.enabled = true;
        }
    }

    static void SetHeroPresetInStore(HeroPresetSlotKind kind, uint64_t heroId, int slotIndex, const HeroPreset& preset)
    {
        if (heroId == 0)
            return;

        const int clampedSlotIndex = ClampHeroPresetSlotIndex(slotIndex);
        auto& slots = EnsureHeroPresetSlotsInStore(kind, heroId);
        EnsureHeroPresetSlotRange(slots, clampedSlotIndex);
        slots[static_cast<size_t>(clampedSlotIndex)].preset = ValidateHeroPresetValueForHero(heroId, preset);
        NormalizeHeroPresetSlots(slots, heroId, kind);
    }

    void SetHeroPreset(uint64_t heroId, int slotIndex, const HeroPreset& preset)
    {
        std::lock_guard<std::mutex> lock(mutex);
        ScopedConfigAuthority authority;
        SetHeroPresetInStore(HeroPresetSlotKind::Aim, heroId, slotIndex, preset);
    }

    void SetHeroAimPreset(uint64_t heroId, int slotIndex, const HeroPreset& preset)
    {
        std::lock_guard<std::mutex> lock(mutex);
        ScopedConfigAuthority authority;
        SetHeroPresetInStore(HeroPresetSlotKind::Aim, heroId, slotIndex, preset);
    }

    void SetHeroTriggerPreset(uint64_t heroId, int slotIndex, const HeroPreset& preset)
    {
        std::lock_guard<std::mutex> lock(mutex);
        ScopedConfigAuthority authority;
        SetHeroPresetInStore(HeroPresetSlotKind::Trigger, heroId, slotIndex, preset);
    }

    void SetHeroAimSlotScopeRequirement(uint64_t heroId,
                                        int slotIndex,
                                        AimScopeRequirement requirement)
    {
        if (heroId == 0)
            return;

        std::lock_guard<std::mutex> lock(mutex);
        ScopedConfigAuthority authority;
        const int clampedSlotIndex = ClampHeroPresetSlotIndex(slotIndex);
        auto& slots = EnsureHeroPresetSlotsInStore(HeroPresetSlotKind::Aim, heroId);
        EnsureHeroPresetSlotRange(slots, clampedSlotIndex);

        HeroSlotPreset& slot = slots[static_cast<size_t>(clampedSlotIndex)];
        requirement = NormalizeAimScopeRequirement(static_cast<int>(requirement));
        if (HeroUsesScopedWeaponActionSplit(heroId)) {
            slot.preset.trigger.action = requirement == AimScopeRequirement::ScopedOnly
                ? 2
                : 0;
        }
        slot.scopeRequirement = requirement;
        NormalizeHeroPresetSlots(slots, heroId, HeroPresetSlotKind::Aim);
    }

    static std::string GetHeroSlotNameFromStore(HeroPresetSlotKind kind, uint64_t heroId, int slotIndex)
    {
        const int clampedSlotIndex = ClampHeroPresetSlotIndex(slotIndex);
        const auto& store = PresetStoreConst(kind);
        const auto item = store.find(heroId);
        if (item == store.end() ||
            !item->second[static_cast<size_t>(clampedSlotIndex)].present)
            return DefaultHeroSlotName(clampedSlotIndex);

        return NormalizeHeroSlotName(item->second[static_cast<size_t>(clampedSlotIndex)].name, clampedSlotIndex);
    }

    static bool TryGetHeroSlotFromStore(HeroPresetSlotKind kind, uint64_t heroId, int slotIndex, HeroSlotPreset& outSlot)
    {
        if (heroId == 0)
            return false;

        const int clampedSlotIndex = ClampHeroPresetSlotIndex(slotIndex);
        const auto& store = PresetStoreConst(kind);
        const auto item = store.find(heroId);
        if (item == store.end())
            return false;

        const HeroSlotPreset& slot = item->second[static_cast<size_t>(clampedSlotIndex)];
        if (!slot.present)
            return false;

        outSlot = slot;
        outSlot.name = NormalizeHeroSlotName(outSlot.name, clampedSlotIndex);
        outSlot.preset = ValidateHeroPresetValueForHero(heroId, outSlot.preset);
        outSlot.scopeRequirement = kind == HeroPresetSlotKind::Aim
            ? DefaultAimScopeRequirementForHeroAction(
                heroId,
                outSlot.preset.trigger.action)
            : AimScopeRequirement::All;
        outSlot.present = true;
        return true;
    }

    std::string GetHeroSlotName(uint64_t heroId, int slotIndex)
    {
        std::lock_guard<std::mutex> lock(mutex);
        ScopedConfigAuthority authority(false);
        return GetHeroSlotNameFromStore(HeroPresetSlotKind::Aim, heroId, slotIndex);
    }

    std::string GetHeroAimSlotName(uint64_t heroId, int slotIndex)
    {
        std::lock_guard<std::mutex> lock(mutex);
        ScopedConfigAuthority authority(false);
        return GetHeroSlotNameFromStore(HeroPresetSlotKind::Aim, heroId, slotIndex);
    }

    std::string GetHeroTriggerSlotName(uint64_t heroId, int slotIndex)
    {
        std::lock_guard<std::mutex> lock(mutex);
        ScopedConfigAuthority authority(false);
        return GetHeroSlotNameFromStore(HeroPresetSlotKind::Trigger, heroId, slotIndex);
    }

    bool TryGetHeroAimSlot(uint64_t heroId, int slotIndex, HeroSlotPreset& outSlot)
    {
        if (configView && !configAuthority) {
            const auto& store = configView->heroAimPresets;
            auto item = store.find(heroId);
            if (item == store.end()) return false;
            outSlot = item->second[static_cast<size_t>(ClampHeroPresetSlotIndex(slotIndex))];
            return outSlot.present;
        }

        std::lock_guard<std::mutex> lock(mutex);
        ScopedConfigAuthority authority(false);
        return TryGetHeroSlotFromStore(HeroPresetSlotKind::Aim, heroId, slotIndex, outSlot);
    }

    bool TryGetHeroTriggerSlot(uint64_t heroId, int slotIndex, HeroSlotPreset& outSlot)
    {
        if (configView && !configAuthority) {
            const auto& store = configView->heroTriggerPresets;
            auto item = store.find(heroId);
            if (item == store.end()) return false;
            outSlot = item->second[static_cast<size_t>(ClampHeroPresetSlotIndex(slotIndex))];
            return outSlot.present;
        }

        std::lock_guard<std::mutex> lock(mutex);
        ScopedConfigAuthority authority(false);
        return TryGetHeroSlotFromStore(HeroPresetSlotKind::Trigger, heroId, slotIndex, outSlot);
    }

    static bool IsHeroSlotEnabledInStore(HeroPresetSlotKind kind, uint64_t heroId, int slotIndex)
    {
        const int clampedSlotIndex = ClampHeroPresetSlotIndex(slotIndex);
        const auto& store = PresetStoreConst(kind);
        const auto item = store.find(heroId);
        if (item == store.end())
            return clampedSlotIndex == 0;

        const HeroSlotPreset& slot = item->second[static_cast<size_t>(clampedSlotIndex)];
        return slot.present && slot.enabled;
    }

    bool IsHeroSlotEnabled(uint64_t heroId, int slotIndex)
    {
        std::lock_guard<std::mutex> lock(mutex);
        ScopedConfigAuthority authority(false);
        return IsHeroSlotEnabledInStore(HeroPresetSlotKind::Aim, heroId, slotIndex);
    }

    bool IsHeroAimSlotEnabled(uint64_t heroId, int slotIndex)
    {
        std::lock_guard<std::mutex> lock(mutex);
        ScopedConfigAuthority authority(false);
        return IsHeroSlotEnabledInStore(HeroPresetSlotKind::Aim, heroId, slotIndex);
    }

    bool IsHeroTriggerSlotEnabled(uint64_t heroId, int slotIndex)
    {
        std::lock_guard<std::mutex> lock(mutex);
        ScopedConfigAuthority authority(false);
        return IsHeroSlotEnabledInStore(HeroPresetSlotKind::Trigger, heroId, slotIndex);
    }

    static void SetHeroSlotEnabledInStore(HeroPresetSlotKind kind, uint64_t heroId, int slotIndex, bool enabled)
    {
        if (heroId == 0)
            return;

        const int clampedSlotIndex = ClampHeroPresetSlotIndex(slotIndex);
        auto& slots = EnsureHeroPresetSlotsInStore(kind, heroId);
        EnsureHeroPresetSlotRange(slots, clampedSlotIndex);
        slots[static_cast<size_t>(clampedSlotIndex)].enabled = enabled;
        NormalizeHeroPresetSlots(slots, heroId, kind);
    }

    void SetHeroSlotEnabled(uint64_t heroId, int slotIndex, bool enabled)
    {
        std::lock_guard<std::mutex> lock(mutex);
        ScopedConfigAuthority authority;
        SetHeroSlotEnabledInStore(HeroPresetSlotKind::Aim, heroId, slotIndex, enabled);
    }

    void SetHeroAimSlotEnabled(uint64_t heroId, int slotIndex, bool enabled)
    {
        std::lock_guard<std::mutex> lock(mutex);
        ScopedConfigAuthority authority;
        SetHeroSlotEnabledInStore(HeroPresetSlotKind::Aim, heroId, slotIndex, enabled);
    }

    void SetHeroTriggerSlotEnabled(uint64_t heroId, int slotIndex, bool enabled)
    {
        std::lock_guard<std::mutex> lock(mutex);
        ScopedConfigAuthority authority;
        SetHeroSlotEnabledInStore(HeroPresetSlotKind::Trigger, heroId, slotIndex, enabled);
    }

    static int GetHeroSlotCountInStore(HeroPresetSlotKind kind, uint64_t heroId)
    {
        if (heroId == 0)
            return 1;

        auto& store = PresetStore(kind);
        auto item = store.find(heroId);
        if (item == store.end())
            return 1;

        NormalizeHeroPresetSlots(item->second, heroId, kind);
        return (std::max)(1, CountHeroPresetSlots(item->second));
    }

    int GetHeroAimSlotCount(uint64_t heroId)
    {
        std::lock_guard<std::mutex> lock(mutex);
        ScopedConfigAuthority authority(false);
        return GetHeroSlotCountInStore(HeroPresetSlotKind::Aim, heroId);
    }

    int GetHeroTriggerSlotCount(uint64_t heroId)
    {
        std::lock_guard<std::mutex> lock(mutex);
        ScopedConfigAuthority authority(false);
        return GetHeroSlotCountInStore(HeroPresetSlotKind::Trigger, heroId);
    }

    static int AddHeroSlotInStore(HeroPresetSlotKind kind, uint64_t heroId, const HeroPreset& seedPreset)
    {
        if (heroId == 0)
            return -1;

        auto& slots = EnsureHeroPresetSlotsInStore(kind, heroId);
        const int presentCount = CountHeroPresetSlots(slots);
        if (presentCount >= kHeroPresetSlotCount)
            return -1;

        const int defaultMode = kind == HeroPresetSlotKind::Aim ?
            EnsureDefaultBehaviorModeUnlocked(kAimBehaviorTracking) : -1;
        if (kind == HeroPresetSlotKind::Aim && defaultMode < 0) return -1;

        HeroSlotPreset& slot = slots[static_cast<size_t>(presentCount)];
        ResetHeroPresetSlot(slot, presentCount);
        slot.present = true;
        slot.enabled = false;
        slot.stableId = NewSlotIdentity();
        slot.name = DefaultHeroSlotName(presentCount);
        slot.preset = ValidateHeroPresetValueForHero(heroId, seedPreset);
        if (kind == HeroPresetSlotKind::Aim) {
            StoreHeroBehaviorMode(slot.preset);
            slot.preset.aimBehaviorPresetId = defaultMode;
            ResolveHeroBehaviorMode(slot.preset);
        }
        slot.preset.key = OW::Labels::kAimActivationKeyNoneIndex;
        slot.preset.trigger.key = OW::Labels::kAimActivationKeyNoneIndex;
        NormalizeHeroPresetSlots(slots, heroId, kind);
        return presentCount;
    }

    int AddHeroAimSlot(uint64_t heroId, const HeroPreset& seedPreset)
    {
        std::lock_guard<std::mutex> lock(mutex);
        ScopedConfigAuthority authority;
        return AddHeroSlotInStore(HeroPresetSlotKind::Aim, heroId, seedPreset);
    }

    int AddHeroTriggerSlot(uint64_t heroId, const HeroPreset& seedPreset)
    {
        std::lock_guard<std::mutex> lock(mutex);
        ScopedConfigAuthority authority;
        return AddHeroSlotInStore(HeroPresetSlotKind::Trigger, heroId, seedPreset);
    }

    static bool DeleteHeroSlotInStore(HeroPresetSlotKind kind, uint64_t heroId, int slotIndex)
    {
        if (heroId == 0)
            return false;

        auto& store = PresetStore(kind);
        auto item = store.find(heroId);
        if (item == store.end())
            return false;

        NormalizeHeroPresetSlots(item->second, heroId, kind);
        const int presentCount = CountHeroPresetSlots(item->second);
        const int clampedSlotIndex = ClampHeroPresetSlotIndex(slotIndex);
        if (presentCount <= 1 || clampedSlotIndex >= presentCount)
            return false;

        for (int index = clampedSlotIndex; index < presentCount - 1; ++index) {
            HeroSlotPreset slot = item->second[static_cast<size_t>(index + 1)];
            slot.name = NormalizeHeroSlotName(slot.name, index);
            item->second[static_cast<size_t>(index)] = slot;
        }

        ResetHeroPresetSlot(item->second[static_cast<size_t>(presentCount - 1)], presentCount - 1);
        NormalizeHeroPresetSlots(item->second, heroId, kind);
        return true;
    }

    bool DeleteHeroAimSlot(uint64_t heroId, int slotIndex)
    {
        std::lock_guard<std::mutex> lock(mutex);
        ScopedConfigAuthority authority;
        return DeleteHeroSlotInStore(HeroPresetSlotKind::Aim, heroId, slotIndex);
    }

    bool DeleteHeroTriggerSlot(uint64_t heroId, int slotIndex)
    {
        std::lock_guard<std::mutex> lock(mutex);
        ScopedConfigAuthority authority;
        return DeleteHeroSlotInStore(HeroPresetSlotKind::Trigger, heroId, slotIndex);
    }


}
