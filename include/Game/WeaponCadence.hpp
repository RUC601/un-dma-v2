#pragma once

#include <cstddef>
#include <optional>
#include <span>
#include <string_view>

namespace OW {

struct WeaponCadenceEntry {
    std::string_view weaponId;
    std::optional<float> triggerCycleIntervalMs;
    std::string_view firePattern;
    std::string_view integrationStatus;
};

// Immutable compiled catalog; source evidence is kept outside the product.
class WeaponCadenceTable {
public:
    const WeaponCadenceEntry* Find(std::string_view weaponId) const;
    std::span<const WeaponCadenceEntry> Entries() const noexcept;
    std::size_t DiscreteIntervalCount() const noexcept;
    std::size_t Size() const noexcept;
};

const WeaponCadenceTable& RuntimeWeaponCadenceTable();
const WeaponCadenceEntry* FindRuntimeWeaponCadence(std::string_view weaponId);
std::optional<float> ResolveWeaponTriggerCycleIntervalMs(std::string_view weaponId);

} // namespace OW
