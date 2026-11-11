#pragma once

#include <array>
#include <string_view>

namespace OW {

struct ProjectilePhysicsProfile {
    std::string_view id;
    float referenceSpeed; // Historical source speed; zero means unknown. Does not override newer weapon speeds.
    float gravityAcceleration; // Positive downward, m/s^2.
    float launchDirectionUpBias; // normalize(view + bias * worldUp).
    float castDelayMs; // Button press to launch-direction selection; not cooldown/recovery.
};

// Pluz manual data, 2023-12-27: https://workshop.codes/wiki/articles/projectile-data-table
// Complete raw records/unknowns/world radii stay in research/pluz-ballistics-20261104.
// Historical skill IDs are explicit; no new actions are created by this catalog.
inline constexpr std::array<ProjectilePhysicsProfile, 51> kProjectilePhysicsProfiles = {{
    { "biotic-grenade", 30.0f, 9.84f, 0.0f, 0.0f },
    { "jagged-blade", 35.0f, 8.5f, 0.02f, 0.0f },
    { "junkrat_frag_launcher", 25.0f, 12.35f, 0.075f, 0.0f },
    { "concussion-mine", 25.0f, 20.3f, 0.09f, 136.0f },
    { "steel-trap", 10.0f, 20.2f, 0.095f, 0.0f },
    { "baptiste_biotic_launcher_alt", 60.0f, 20.1f, 0.007f, 0.0f },
    { "immortality-field", 60.0f, 20.1f, 0.007f, 0.0f },
    { "a-36-tactical-grenade", 40.0f, 9.75f, 0.0f, 0.0f },
    { "orisa_energy_javelin", 70.0f, 7.5f, 0.0f, 304.0f },
    { "orisa_protective_barrier_old", 25.0f, 9.84f, 0.0f, 128.0f },
    { "zarya_particle_cannon_alt", 25.0f, 9.84f, 0.1f, 0.0f },
    { "graviton-surge", 25.0f, 9.84f, 0.1f, 0.0f },
    { "sombra_translocator_old", 25.0f, 20.2f, 0.09f, 96.0f },
    { "dynamite", 25.0f, 8.03f, 0.07f, 140.0f },
    { "pulse-bomb", 15.0f, 30.8f, 0.19f, 144.0f },
    { "flashbang", 25.0f, 12.0f, 0.025f, 0.0f },
    { "cassidy_flashbang_old", 30.0f, 9.84f, 0.0f, 0.0f },
    { "cassidy_tumbleweed", 4.2f, 10.0f, 0.577f, 0.0f },
    { "blizzard", 20.0f, 9.9f, 0.09f, 484.0f },
    { "torbjorn_rivet_gun", 70.0f, 9.81f, 0.015f, 0.0f },
    { "molten-core", 40.0f, 30.05f, 0.0f, 0.0f },
    { "deploy-turret", 17.0f, 20.0f, 0.0f, 48.0f },
    { "hanzo_storm_bow", 110.0f, 9.82f, 0.0f, 0.0f },
    { "storm-arrows", 110.0f, 9.82f, 0.0f, 0.0f },
    { "sigma_hyperspheres", 49.7f, 5.95f, 0.0f, 0.0f },
    { "accretion", 37.4f, 18.35f, 0.1f, 640.0f },
    { "ravenous-vortex", 30.0f, 9.75f, 0.0f, 496.0f },
    { "ramattra_void_accelerator", 80.0f, 0.0f, 0.0f, 0.0f },
    { "venom-mine", 20.0f, 9.84f, 0.0f, 192.0f },
    { "ana_biotic_rifle_hip", 0.0f, 0.0f, 0.0f, 0.0f },
    { "sleep-dart", 0.0f, 0.0f, 0.0f, 0.0f },
    { "whip-shot", 0.0f, 0.0f, 0.0f, 0.0f },
    { "dva_light_gun", 0.0f, 0.0f, 0.0f, 0.0f },
    { "genji_shuriken", 0.0f, 0.0f, 0.0f, 0.0f },
    { "genji_shuriken_alt", 0.0f, 0.0f, 0.0f, 0.0f },
    { "doomfist_hand_cannon", 0.0f, 0.0f, 0.0f, 0.0f },
    { "echo_tri_shot", 0.0f, 0.0f, 0.0f, 0.0f },
    { "roadhog_scrap_gun", 0.0f, 0.0f, 0.0f, 0.0f },
    { "roadhog_scrap_gun_alt", 0.0f, 0.0f, 0.0f, 0.0f },
    { "lucio_sonic_amplifier", 0.0f, 0.0f, 0.0f, 0.0f },
    { "mei_endothermic_blaster", 0.0f, 0.0f, 0.0f, 0.0f },
    { "mei_icicle", 0.0f, 0.0f, 0.0f, 0.0f },
    { "mercy_caduceus_blaster", 0.0f, 0.0f, 0.0f, 0.0f },
    { "pharah_rocket_launcher", 0.0f, 0.0f, 0.0f, 0.0f },
    { "concussive-blast", 0.0f, 0.0f, 0.0f, 0.0f },
    { "barrage", 0.0f, 0.0f, 0.0f, 0.0f },
    { "fire-strike", 0.0f, 0.0f, 0.0f, 0.0f },
    { "helix-rockets", 0.0f, 0.0f, 0.0f, 0.0f },
    { "symmetra_photon_projector_alt", 0.0f, 0.0f, 0.0f, 0.0f },
    { "zenyatta_orb_of_destruction", 0.0f, 0.0f, 0.0f, 0.0f },
    { "zenyatta_orb_of_destruction_alt", 0.0f, 0.0f, 0.0f, 0.0f },
}};

constexpr const ProjectilePhysicsProfile* FindProjectilePhysicsProfile(std::string_view id)
{
    for (const auto& profile : kProjectilePhysicsProfiles)
        if (profile.id == id)
            return &profile;
    return nullptr;
}

static_assert([] {
    for (std::size_t i = 0; i < kProjectilePhysicsProfiles.size(); ++i) {
        const auto& p = kProjectilePhysicsProfiles[i];
        if (p.id.empty() || p.referenceSpeed < 0 || p.gravityAcceleration < 0 ||
            p.launchDirectionUpBias < 0 || p.launchDirectionUpBias >= 1 || p.castDelayMs < 0)
            return false;
        for (std::size_t j = 0; j < i; ++j)
            if (p.id == kProjectilePhysicsProfiles[j].id)
                return false;
    }
    return true;
}(), "Invalid or duplicate projectile physics profile");

} // namespace OW
