#include "Game/WeaponCadence.hpp"

#include <algorithm>
#include <array>

namespace OW {
namespace {

// Reviewed snapshot: weapon-data-sources-20261104/legacy-v2-data, 2026-07-14.
constexpr std::array<WeaponCadenceEntry, 75> kCadence = {{
    { "ana_biotic_rifle_ads", 800.0f, "scoped_single", "existing_weapon_spec" },
    { "ana_biotic_rifle_hip", 800.0f, "single", "existing_weapon_spec" },
    { "ashe_take_aim_ads", 656.0f, "scoped_single", "existing_weapon_spec" },
    { "ashe_the_viper_hip", 256.0f, "semi_automatic_single", "existing_weapon_spec" },
    { "baptiste_biotic_launcher", 588.0f, "sequential_burst", "existing_weapon_spec" },
    { "baptiste_biotic_launcher_alt", 900.0f, "single", "existing_weapon_spec" },
    { "bastion_assault", 33.333f, "automatic", "existing_weapon_spec" },
    { "bastion_recon", 166.667f, "automatic", "existing_weapon_spec" },
    { "brigitte_rocket_flail", 600.0f, "melee_swing", "existing_weapon_spec" },
    { "cassidy_ads_perk", 500.0f, "code_variant_single", "existing_code_variant" },
    { "cassidy_fan_the_hammer", 120.0f, "automatic", "existing_weapon_spec" },
    { "cassidy_peacekeeper", 500.0f, "single", "existing_weapon_spec" },
    { "doomfist_hand_cannon", 336.0f, "simultaneous_projectile_volley", "existing_weapon_spec" },
    { "dva_fusion_cannons", 150.0f, "simultaneous_projectile_volley", "existing_weapon_spec" },
    { "dva_light_gun", 143.0f, "automatic", "existing_weapon_spec" },
    { "echo_tri_shot", 336.0f, "simultaneous_projectile_volley", "existing_weapon_spec" },
    { "emre_synthetic_burst_rifle", 480.0f, "sequential_burst", "existing_weapon_spec" },
    { "emre_synthetic_burst_rifle_ads", 480.0f, "sequential_burst", "existing_weapon_spec" },
    { "freja_revdraw_crossbow", 182.0f, "automatic", "existing_weapon_spec" },
    { "freja_take_aim", 688.0f, "scoped_single", "existing_weapon_spec" },
    { "genji_shuriken", 880.0f, "sequential_burst", "existing_weapon_spec" },
    { "genji_shuriken_alt", 680.0f, "simultaneous_projectile_volley", "existing_weapon_spec" },
    { "hanzo_storm_bow", 1220.0f, "charge_release", "existing_weapon_spec" },
    { "hazard_bonespur", 570.0f, "simultaneous_projectile_volley", "existing_weapon_spec" },
    { "illari_solar_rifle", 900.0f, "auto_charge_single", "existing_weapon_spec" },
    { "illari_solar_rifle_alt", std::nullopt, "continuous_beam", "existing_weapon_spec" },
    { "jetpackcat_biotic_pawjectiles", 224.0f, "simultaneous_projectile_volley", "existing_weapon_spec" },
    { "junkerqueen_scattergun", 750.0f, "simultaneous_projectile_volley", "existing_weapon_spec" },
    { "junkrat_frag_launcher", 666.667f, "single", "existing_weapon_spec" },
    { "juno_mediblaster", 774.0f, "sequential_burst", "existing_weapon_spec" },
    { "kiriko_healing_ofuda", 150.0f, "toggled_simultaneous_volley_stream", "existing_weapon_spec" },
    { "kiriko_kunai", 500.0f, "single", "existing_weapon_spec" },
    { "lifeweaver_healing_blossom", 1500.0f, "charge_release", "existing_weapon_spec" },
    { "lifeweaver_thorn_volley", 91.0f, "simultaneous_projectile_volley", "existing_weapon_spec" },
    { "lucio_sonic_amplifier", 935.0f, "sequential_burst", "existing_weapon_spec" },
    { "mauga_incendiary_chaingun", 57.6f, "automatic", "existing_weapon_spec" },
    { "mauga_volatile_chaingun", 57.6f, "automatic", "existing_weapon_spec" },
    { "mei_endothermic_blaster", 50.0f, "continuous_beam", "existing_weapon_spec" },
    { "mei_icicle", 800.0f, "windup_single", "existing_weapon_spec" },
    { "mercy_caduceus_blaster", 200.0f, "automatic", "existing_weapon_spec" },
    { "mercy_caduceus_staff_damage", std::nullopt, "continuous_beam", "existing_weapon_spec" },
    { "mercy_caduceus_staff_heal", std::nullopt, "continuous_beam", "existing_weapon_spec" },
    { "moira_biotic_grasp_damage", std::nullopt, "continuous_beam", "existing_weapon_spec" },
    { "moira_biotic_grasp_heal", std::nullopt, "continuous_beam", "existing_weapon_spec" },
    { "orisa_augmented_fusion_driver", 100.0f, "automatic", "existing_weapon_spec" },
    { "pharah_rocket_launcher", 800.0f, "single", "existing_weapon_spec" },
    { "ramattra_pummel", 600.0f, "melee_swing", "existing_weapon_spec" },
    { "ramattra_void_accelerator", 40.0f, "automatic", "existing_weapon_spec" },
    { "reaper_hellfire_shotguns", 500.0f, "simultaneous_projectile_volley", "existing_weapon_spec" },
    { "reinhardt_rocket_hammer", 960.0f, "melee_swing", "existing_weapon_spec" },
    { "roadhog_scrap_gun", 800.0f, "simultaneous_projectile_volley", "existing_weapon_spec" },
    { "roadhog_scrap_gun_alt", 800.0f, "simultaneous_projectile_volley", "existing_weapon_spec" },
    { "sierra_helix_rifle", 91.84f, "simultaneous_projectile_volley", "existing_weapon_spec" },
    { "sigma_hyperspheres", 1500.0f, "sequential_burst", "existing_weapon_spec" },
    { "sojourn_charged_shot", 640.0f, "resource_gated_single", "existing_weapon_spec" },
    { "sojourn_railgun", 62.5f, "automatic", "existing_weapon_spec" },
    { "soldier76_heavy_pulse_rifle", 96.0f, "automatic", "existing_weapon_spec" },
    { "sombra_machine_pistol", 48.0f, "automatic", "existing_weapon_spec" },
    { "symmetra_photon_projector", std::nullopt, "continuous_beam", "existing_weapon_spec" },
    { "symmetra_photon_projector_alt", 1250.0f, "charge_release", "existing_weapon_spec" },
    { "torbjorn_forge_hammer", 700.0f, "melee_swing", "existing_weapon_spec" },
    { "torbjorn_rivet_gun", 510.0f, "single", "existing_weapon_spec" },
    { "torbjorn_rivet_gun_alt", 700.0f, "simultaneous_projectile_volley", "existing_weapon_spec" },
    { "tracer_pulse_pistols", 50.0f, "simultaneous_projectile_volley", "existing_weapon_spec" },
    { "venture_smart_excavator", 600.0f, "single", "existing_weapon_spec" },
    { "widowmaker_widows_kiss_ads", 1400.0f, "passive_charge_single", "existing_weapon_spec" },
    { "widowmaker_widows_kiss_hip", 100.0f, "automatic", "existing_weapon_spec" },
    { "winston_tesla_cannon", std::nullopt, "continuous_beam", "existing_weapon_spec" },
    { "winston_tesla_cannon_alt", 1600.0f, "charge_release", "existing_weapon_spec" },
    { "wreckingball_quad_cannons", 32.0f, "automatic", "existing_weapon_spec" },
    { "wuyang_xuanwu_staff", 630.0f, "controllable_charge_single", "existing_weapon_spec" },
    { "zarya_particle_cannon", std::nullopt, "continuous_beam", "existing_weapon_spec" },
    { "zarya_particle_cannon_alt", 992.0f, "single", "existing_weapon_spec" },
    { "zenyatta_orb_of_destruction", 400.0f, "single", "existing_weapon_spec" },
    { "zenyatta_orb_of_destruction_alt", 3220.0f, "charged_sequential_volley", "existing_weapon_spec" },
}};

static_assert([] {
    for (std::size_t i = 0; i < kCadence.size(); ++i) {
        const auto& entry = kCadence[i];
        if (entry.weaponId.empty() || (entry.triggerCycleIntervalMs && *entry.triggerCycleIntervalMs <= 0.0f) ||
            (i > 0 && kCadence[i - 1].weaponId >= entry.weaponId))
            return false;
    }
    return true;
}(), "Cadence keys must be unique/sorted and known intervals positive");

} // namespace

const WeaponCadenceEntry* WeaponCadenceTable::Find(std::string_view weaponId) const
{
    const auto found = std::lower_bound(kCadence.begin(), kCadence.end(), weaponId,
        [](const WeaponCadenceEntry& entry, std::string_view key) { return entry.weaponId < key; });
    return found != kCadence.end() && found->weaponId == weaponId ? &*found : nullptr;
}

std::span<const WeaponCadenceEntry> WeaponCadenceTable::Entries() const noexcept { return kCadence; }
std::size_t WeaponCadenceTable::Size() const noexcept { return kCadence.size(); }
std::size_t WeaponCadenceTable::DiscreteIntervalCount() const noexcept
{
    return static_cast<std::size_t>(std::count_if(kCadence.begin(), kCadence.end(),
        [](const WeaponCadenceEntry& entry) { return entry.triggerCycleIntervalMs.has_value(); }));
}

const WeaponCadenceTable& RuntimeWeaponCadenceTable()
{
    static const WeaponCadenceTable table;
    return table;
}

const WeaponCadenceEntry* FindRuntimeWeaponCadence(std::string_view weaponId)
{
    return RuntimeWeaponCadenceTable().Find(weaponId);
}

std::optional<float> ResolveWeaponTriggerCycleIntervalMs(std::string_view weaponId)
{
    const auto* entry = FindRuntimeWeaponCadence(weaponId);
    return entry ? entry->triggerCycleIntervalMs : std::nullopt;
}
} // namespace OW
