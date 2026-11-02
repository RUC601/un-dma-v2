#include "Game/HeroPerkRuntime.hpp"

#include <Windows.h>

#include <mutex>

#include "Game/Structs.hpp"
#include "Game/RuntimeState.hpp"
#include "Memory/KeyState.hpp"
#include "Utils/Config.hpp"
#include "Utils/Diagnostics.hpp"

namespace OW::HeroPerkRuntime {
namespace {

struct RuntimeState {
    uint64_t heroId = 0;
    bool teamKnown = false;
    bool teamFlag = false;
    ManualOverrideMode manualOverride = ManualOverrideMode::None;
};

std::mutex g_mutex;
RuntimeState g_state;
bool g_lastF8Down = false;

const char* VariantIdForHero(uint64_t heroId)
{
    switch (heroId) {
    case static_cast<uint64_t>(OW::eHero::HERO_MCCREE):
        return "cassidy_ads_perk";
    default:
        return "";
    }
}

void ResetUnlocked(const char* reason)
{
    if (g_state.heroId != 0 || g_state.manualOverride != ManualOverrideMode::None) {
        Diagnostics::Aim("perk.runtime reset reason=%s hero=0x%llX manual=%s",
            reason ? reason : "unknown",
            static_cast<unsigned long long>(g_state.heroId),
            ManualOverrideModeName(g_state.manualOverride));
    }

    g_state = RuntimeState{};
}

void ProcessHotkeysUnlocked()
{
    const KeyState::KeyStateVkSample f8Sample = KeyState::SampleVk(VK_F8);
    if (!f8Sample.available)
        return;
    const bool f8Down = f8Sample.down;
    const bool f8Pressed = f8Down && !g_lastF8Down;
    g_lastF8Down = f8Down;
    if (!f8Pressed || g_state.heroId == 0 ||
        OW::IsPerkHotkeyReservedForMenu(OW::Config::MenuToggleKey, VK_F8))
        return;

    const bool shiftDown =
        KeyState::IsKeyDown(VK_SHIFT) ||
        KeyState::IsKeyDown(VK_LSHIFT) ||
        KeyState::IsKeyDown(VK_RSHIFT);

    if (shiftDown) {
        g_state.manualOverride = ManualOverrideMode::None;
        Diagnostics::Aim("perk.runtime manual_clear hero=0x%llX",
            static_cast<unsigned long long>(g_state.heroId));
        return;
    }

    if (!HeroSupportsManualPerk(g_state.heroId)) {
        Diagnostics::Aim("perk.runtime hotkey_noop reason=unsupported_hero hero=0x%llX",
            static_cast<unsigned long long>(g_state.heroId));
        return;
    }

    g_state.manualOverride = g_state.manualOverride == ManualOverrideMode::ForceOn
        ? ManualOverrideMode::ForceOff
        : ManualOverrideMode::ForceOn;
    Diagnostics::Aim("perk.runtime manual_toggle hero=0x%llX manual=%s variant=%s",
        static_cast<unsigned long long>(g_state.heroId),
        ManualOverrideModeName(g_state.manualOverride),
        VariantIdForHero(g_state.heroId));
}

} // namespace

void UpdateContext(uint64_t heroId, bool connected, bool teamFlag)
{
    std::lock_guard<std::mutex> lock(g_mutex);

    if (!connected) {
        ResetUnlocked("disconnect");
        g_lastF8Down = false;
        return;
    }

    if (heroId != 0) {
        if (g_state.heroId != 0 && g_state.heroId != heroId) {
            ResetUnlocked("hero_change");
        }

        if (g_state.heroId == 0) {
            g_state.heroId = heroId;
            g_state.teamKnown = true;
            g_state.teamFlag = teamFlag;
            Diagnostics::Aim("perk.runtime context hero=0x%llX supported=%d",
                static_cast<unsigned long long>(g_state.heroId),
                HeroSupportsManualPerk(g_state.heroId) ? 1 : 0);
        } else if (g_state.teamKnown && g_state.teamFlag != teamFlag) {
            ResetUnlocked("team_change");
            g_state.heroId = heroId;
            g_state.teamKnown = true;
            g_state.teamFlag = teamFlag;
        } else {
            g_state.teamKnown = true;
            g_state.teamFlag = teamFlag;
        }
    }

    ProcessHotkeysUnlocked();
}

void ResetForDisconnect()
{
    std::lock_guard<std::mutex> lock(g_mutex);
    ResetUnlocked("disconnect");
    g_lastF8Down = false;
}

bool HeroSupportsManualPerk(uint64_t heroId)
{
    return VariantIdForHero(heroId)[0] != '\0';
}

bool IsEffectivePerkOn(uint64_t heroId)
{
    std::lock_guard<std::mutex> lock(g_mutex);
    return g_state.heroId == heroId &&
        g_state.manualOverride == ManualOverrideMode::ForceOn &&
        HeroSupportsManualPerk(heroId);
}

void ToggleCurrentHeroManualPerk()
{
    std::lock_guard<std::mutex> lock(g_mutex);
    if (g_state.heroId == 0 || !HeroSupportsManualPerk(g_state.heroId))
        return;
    g_state.manualOverride = g_state.manualOverride == ManualOverrideMode::ForceOn
        ? ManualOverrideMode::ForceOff
        : ManualOverrideMode::ForceOn;
}

void ClearCurrentHeroManualPerk()
{
    std::lock_guard<std::mutex> lock(g_mutex);
    g_state.manualOverride = ManualOverrideMode::None;
}

void SetManualPerkOverride(uint64_t heroId, ManualOverrideMode mode)
{
    std::lock_guard<std::mutex> lock(g_mutex);
    g_state.heroId = heroId;
    g_state.teamKnown = false;
    g_state.manualOverride = HeroSupportsManualPerk(heroId)
        ? mode
        : ManualOverrideMode::None;
}

Snapshot CurrentSnapshot()
{
    std::lock_guard<std::mutex> lock(g_mutex);

    Snapshot snapshot{};
    snapshot.heroId = g_state.heroId;
    snapshot.supported = HeroSupportsManualPerk(g_state.heroId);
    snapshot.manualOverride = g_state.manualOverride;
    snapshot.effectivePerkOn = snapshot.supported &&
        g_state.manualOverride == ManualOverrideMode::ForceOn;
    snapshot.source = g_state.manualOverride == ManualOverrideMode::None
        ? EffectiveSource::Default
        : EffectiveSource::Manual;
    snapshot.variantId = snapshot.effectivePerkOn ? VariantIdForHero(g_state.heroId) : "";
    return snapshot;
}

const char* ManualOverrideModeName(ManualOverrideMode mode)
{
    switch (mode) {
    case ManualOverrideMode::ForceOff:
        return "ForceOff";
    case ManualOverrideMode::ForceOn:
        return "ForceOn";
    case ManualOverrideMode::None:
    default:
        return "None";
    }
}

const char* EffectiveSourceName(EffectiveSource source)
{
    switch (source) {
    case EffectiveSource::Manual:
        return "Manual";
    case EffectiveSource::Default:
    default:
        return "Default";
    }
}

} // namespace OW::HeroPerkRuntime
