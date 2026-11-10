#pragma once
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <string>

namespace OW::MapData {
// 155284 World/CN/Nexon dumps and forum 516727 p343/p365.
// DisplayName covers the 37 entries in p343, not a complete game-resource catalogue.
// Arcade, Assault, Stadium and sub-map variants may legitimately return Unknown + raw ID.
inline constexpr uint64_t singletonIndex = 0x30;
inline constexpr uint64_t idOffset = 0x18;
inline constexpr uint64_t resourceType = 0x0800000000000000ULL;

constexpr bool ValidId(uint64_t id) {
    return (id & ~0xFFFFFFULL) == resourceType && (id & 0xFFFFFFULL) != 0;
}

inline std::string DisplayName(uint64_t id) {
    if (!ValidId(id)) return "--";
    switch (id & 0xFFFFFFULL) {
    case 0x688: return "Practice Range";
    case 0x1157: return "Hanaoka";
    case 0x1160: return "Throne of Anubis";
    case 0xCF2: return "Antarctic Peninsula";
    case 0x7E2: return "Busan";
    case 0x66D: return "Ilios";
    case 0x662: return "Lijiang Tower";
    case 0x6B7: return "Lijiang Tower (Lunar)";
    case 0x4B7: return "Nepal";
    case 0x69E: return "Oasis";
    case 0xEC0: return "Samoa";
    case 0x827: return "Circuit Royal";
    case 0x2C3: return "Dorado";
    case 0xA44: return "Havana";
    case 0x756: return "Junkertown";
    case 0x871: return "Rialto";
    case 0x5BB: return "Route 66";
    case 0xC85: return "Shambali Monastery";
    case 0x184: return "Watchpoint Gibraltar";
    case 0xF35: return "Aatlis";
    case 0xE13: return "New Junk City";
    case 0xD3E: return "Suravasa";
    case 0x75E: return "Blizzard World";
    case 0xA5B: return "Blizzard World (Winter)";
    case 0x68D: return "Eichenwalde";
    case 0x7F4: return "Eichenwalde (Halloween)";
    case 0x2AF: return "Hollywood";
    case 0x6AB: return "Hollywood (Halloween)";
    case 0xD4: return "King's Row";
    case 0x6B1: return "King's Row (Winter)";
    case 0xB4C: return "Midtown";
    case 0x1D4: return "Numbani";
    case 0x938: return "Paraiso";
    case 0xD53: return "Esperanca";
    case 0xAEB: return "New Queen Street";
    case 0xB34: return "Colosseo";
    case 0xEB2: return "Runasapi";
    }
    char text[32]{};
    std::snprintf(text, sizeof(text), "Unknown (0x%06X)", unsigned(id & 0xFFFFFFULL));
    return text;
}

struct Snapshot {
    uint64_t id = 0;
    uint64_t connection = 0;
    uint64_t scene = 0;
    uint64_t sampledAtMs = 0;

    bool Fresh(uint64_t now, uint64_t expectedConnection, uint64_t expectedScene) const {
        return ValidId(id) && sampledAtMs != 0 && now >= sampledAtMs &&
            now - sampledAtMs <= 1500 && connection == expectedConnection && scene == expectedScene;
    }
};

constexpr bool UserPointer(uint64_t value) {
    return value >= 0x10000 && value < 0x800000000000ULL && (value & 7) == 0;
}

// Re-read the owner, encrypted table, slot and ID so a map load cannot mix generations.
template <typename Read, typename Decode>
bool TryRead(Read&& read, Decode&& decode, uint64_t module, uint64_t adminRva, uint64_t& id) {
    id = 0;
    if (!UserPointer(module) || !adminRva) return false;
    uint64_t admin = 0, encrypted = 0, record = 0, candidate = 0;
    if (!read(module + adminRva, &admin, sizeof(admin)) || !UserPointer(admin) ||
        !read(admin + 0x160, &encrypted, sizeof(encrypted)) || !encrypted) return false;
    const uint64_t table = decode(encrypted);
    if (!UserPointer(table) || !read(table + singletonIndex * 8, &record, sizeof(record)) ||
        !UserPointer(record) || !read(record + idOffset, &candidate, sizeof(candidate)) ||
        !ValidId(candidate)) return false;
    uint64_t adminAgain = 0, encryptedAgain = 0, recordAgain = 0, idAgain = 0;
    if (!read(module + adminRva, &adminAgain, sizeof(adminAgain)) || adminAgain != admin ||
        !read(admin + 0x160, &encryptedAgain, sizeof(encryptedAgain)) || encryptedAgain != encrypted ||
        !read(table + singletonIndex * 8, &recordAgain, sizeof(recordAgain)) || recordAgain != record ||
        !read(record + idOffset, &idAgain, sizeof(idAgain)) || idAgain != candidate) return false;
    id = candidate;
    return true;
}
}
