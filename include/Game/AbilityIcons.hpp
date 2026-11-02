#pragma once

#include <cctype>
#include <cstddef>
#include <string>

namespace OW {

struct HeroAbilityIcons {
    const char* heroSlug;
    const char* ability1Icon;
    const char* ability2Icon;
    const char* ultimateIcon;
};

// Default Ability 1 / Ability 2; Bastion and Venture use their active secondary
// fire for the second display slot. Anran's icon represents the living ultimate.
inline constexpr HeroAbilityIcons kHeroAbilityIcons[] = {
    { "ana", "sleep-dart", "biotic-grenade", "nano-boost" },
    { "anran", "inferno-rush", "dancing-blaze", "vermillion-ascent" },
    { "ashe", "coach-gun", "dynamite", "bob" },
    { "baptiste", "regenerative-burst", "immortality-field", "amplification-matrix" },
    { "bastion", "reconfigure", "a-36-tactical-grenade", "configuration-artillery" },
    { "brigitte", "whip-shot", "repair-pack", "rally" },
    { "cassidy", "combat-roll", "flashbang", "deadeye" },
    { "domina", "sonic-repulsors", "crystal-charge", "panopticon" },
    { "doomfist", "seismic-slam", "power-block", "meteor-strike" },
    { "dva", "boosters", "micro-missiles", "self-destruct" },
    { "echo", "flight", "focusing-beam", "duplicate" },
    { "emre", "siphon-blaster", "cyber-frag", "override-protocol" },
    { "freja", "quick-dash", "updraft", "bola-shot" },
    { "genji", "swift-strike", "deflect", "dragonblade" },
    { "hanzo", "sonic-arrow", "storm-arrows", "dragonstrike" },
    { "hazard", "violent-leap", "jagged-wall", "downpour" },
    { "illari", "outburst", "healing-pylon", "captive-sun" },
    { "jetpack-cat", "lifeline", "purr", "catnapper" },
    { "junker-queen", "commanding-shout", "carnage", "rampage" },
    { "junkrat", "concussion-mine", "steel-trap", "rip-tire" },
    { "juno", "glide-boost", "hyper-ring", "orbital-ray" },
    { "kiriko", "swift-step", "protection-suzu", "kitsune-rush" },
    { "lifeweaver", "petal-platform", "life-grip", "tree-of-life" },
    { "lucio", "crossfade", "amp-it-up", "sound-barrier" },
    { "mauga", "overrun", "cardiac-overdrive", "cage-fight" },
    { "mei", "cryo-freeze", "ice-wall", "blizzard" },
    { "mercy", "guardian-angel", "resurrect", "valkyrie" },
    { "mizuki", "katashiro-return", "binding-chain", "kekkai-sanctuary" },
    { "moira", "fade", "biotic-orb", "coalescence" },
    { "orisa", "fortify", "javelin-spin", "terra-surge" },
    { "pharah", "jump-jet", "concussive-blast", "barrage" },
    { "ramattra", "nemesis-form", "ravenous-vortex", "annihilation" },
    { "reaper", "wraith-form", "shadow-step", "death-blossom" },
    { "reinhardt", "charge", "fire-strike", "earthshatter" },
    { "roadhog", "chain-hook", "take-a-breather", "whole-hog" },
    { "shion", "evade", "joyride", "satsuriku-spree" },
    { "sierra", "anchor-drone", "tremor-charge", "trailblazer" },
    { "sigma", "kinetic-grasp", "accretion", "gravitic-flux" },
    { "sojourn", "power-slide", "disruptor-shot", "overclock" },
    { "soldier-76", "sprint", "biotic-field", "tactical-visor" },
    { "sombra", "cyberspace", "translocator", "emp" },
    { "symmetra", "sentry-turret", "teleporter", "photon-barrier" },
    { "torbjorn", "deploy-turret", "overload", "molten-core" },
    { "tracer", "blink", "recall", "pulse-bomb" },
    { "vendetta", "whirlwind-dash", "soaring-slice", "sundering-blade" },
    { "venture", "burrow", "drill-dash", "tectonic-shock" },
    { "widowmaker", "grappling-hook", "venom-mine", "infra-sight" },
    { "winston", "jump-pack", "barrier-projector", "primal-rage" },
    { "wrecking-ball", "roll", "adaptive-shield", "minefield" },
    { "wuyang", "rushing-torrent", "guardian-wave", "tidal-blast" },
    { "zarya", "particle-barrier", "projected-barrier", "graviton-surge" },
    { "zenyatta", "orb-of-harmony", "orb-of-discord", "transcendence" },
};

inline constexpr const auto& AllHeroAbilityIcons() {
    return kHeroAbilityIcons;
}

inline constexpr size_t HeroAbilityIconCount() {
    return sizeof(kHeroAbilityIcons) / sizeof(kHeroAbilityIcons[0]);
}

std::string HeroDisplayNameToSlug(const std::string& displayName);

inline const HeroAbilityIcons* GetAbilityIcons(const std::string& slugOrDisplayName) {
    const std::string slug = HeroDisplayNameToSlug(slugOrDisplayName);
    if (slug.empty())
        return nullptr;

    for (const HeroAbilityIcons& icons : kHeroAbilityIcons) {
        if (slug == icons.heroSlug)
            return &icons;
    }
    return nullptr;
}

} // namespace OW
