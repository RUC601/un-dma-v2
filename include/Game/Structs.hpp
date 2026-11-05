#pragma once
#include <cstdint>
#include <cmath>
#include <array>
#include <string>
#include <vector>
#include <bitset>
#include "Game/GameData.hpp"
#include "Game/Offsets.hpp"
#include "Utils/Types.hpp"

using DirectX::XMFLOAT3;

#define OFF_MEMB(type, name, offset)\
struct {\
    char zpad##name[offset];\
    type name;\
}

namespace OW {

    // =========================================================================
    // Types (Vector2, Vector3, Matrix, Color, Rect) are in Utils/Types.hpp
    // =========================================================================

    // =========================================================================
    // Enums
    // =========================================================================

    enum eTeam {
        TEAM_RED,
        TEAM_BLUE,
        TEAM_UNKNOWN1,
        TEAM_UNKNOWN2,
        TEAM_DEATHMATCH,
        TEAM_UNCONFIRMED = -1,
    };

    enum BONE : int {
        BONE_HEAD        = 17,
        BONE_NECK        = 16,
        BONE_CHEST       = 2,
        BONE_R_KNEE      = 99,
        BONE_R_SHANK     = 97,
        BONE_R_ANKLE     = 96,
        BONE_R_SHOULDER  = 54,
        BONE_R_ELBOW     = 51,
        BONE_R_HAND      = 71,
        BONE_L_KNEE      = 89,
        BONE_L_SHANK     = 87,
        BONE_L_ANKLE     = 86,
        BONE_L_SHOULDER  = 49,
        BONE_L_ELBOW     = 14,
        BONE_L_HAND      = 41,
        BONE_PELVIS      = 3,
        BONE_BODY        = 81,
        BONE_BODY_BOT    = 82,
    };

    enum eHero : uint64_t {
        HERO_REAPER        = GameData::MakeHeroId(0x002),
        HERO_TRACER        = GameData::MakeHeroId(0x003),
        HERO_MERCY         = GameData::MakeHeroId(0x004),
        HERO_HANJO         = GameData::MakeHeroId(0x005),
        HERO_HANZO         = HERO_HANJO,
        HERO_TORBJORN      = GameData::MakeHeroId(0x006),
        HERO_REINHARDT     = GameData::MakeHeroId(0x007),
        HERO_PHARAH        = GameData::MakeHeroId(0x008),
        HERO_WINSTON       = GameData::MakeHeroId(0x009),
        HERO_WIDOWMAKER    = GameData::MakeHeroId(0x00A),
        HERO_BASTION       = GameData::MakeHeroId(0x015),
        HERO_SYMMETRA      = GameData::MakeHeroId(0x016),
        HERO_ZENYATTA      = GameData::MakeHeroId(0x020),
        HERO_GENJI         = GameData::MakeHeroId(0x029),
        HERO_ROADHOG       = GameData::MakeHeroId(0x040),
        HERO_CASSIDY       = GameData::MakeHeroId(0x042),
        HERO_MCCREE        = HERO_CASSIDY,
        HERO_JUNKRAT       = GameData::MakeHeroId(0x065),
        HERO_ZARYA         = GameData::MakeHeroId(0x068),
        HERO_SOLDIER76     = GameData::MakeHeroId(0x06E),
        HERO_LUCIO         = GameData::MakeHeroId(0x079),
        HERO_DVA           = GameData::MakeHeroId(0x07A),
        HERO_MEI           = GameData::MakeHeroId(0x0DD),
        HERO_SOMBRA        = GameData::MakeHeroId(0x12E),
        HERO_DOOMFIST      = GameData::MakeHeroId(0x12F),
        HERO_ANA           = GameData::MakeHeroId(0x13B),
        HERO_ORISA         = GameData::MakeHeroId(0x13E),
        HERO_BRIGITTE      = GameData::MakeHeroId(0x195),
        HERO_MOIRA         = GameData::MakeHeroId(0x1A2),
        HERO_WRECKINGBALL  = GameData::MakeHeroId(0x1CA),
        HERO_SOJOURN       = GameData::MakeHeroId(0x1EC),
        HERO_ASHE          = GameData::MakeHeroId(0x200),
        HERO_ECHO          = GameData::MakeHeroId(0x206),
        HERO_BAPTISTE      = GameData::MakeHeroId(0x221),
        HERO_KIRIKO        = GameData::MakeHeroId(0x231),
        HERO_JUNKERQUEEN   = GameData::MakeHeroId(0x236),
        HERO_SIGMA         = GameData::MakeHeroId(0x23B),
        HERO_RAMATTRA      = GameData::MakeHeroId(0x28D),
        HERO_LIFEWEAVER    = GameData::MakeHeroId(0x291),
        HERO_MAUGA         = GameData::MakeHeroId(0x30A),
        HERO_ILLARI        = GameData::MakeHeroId(0x31C),
        HERO_FREJA         = GameData::MakeHeroId(0x32A),
        HERO_VENTURE       = GameData::MakeHeroId(0x32B),
        HERO_HAZARD        = GameData::MakeHeroId(0x362),
        HERO_JUNO          = GameData::MakeHeroId(0x365),
        HERO_WUYANG        = GameData::MakeHeroId(0x3C3),
        HERO_VENDETTA      = GameData::MakeHeroId(0x472),
        HERO_DOMINA        = GameData::MakeHeroId(0x4C4),
        HERO_SIERRA        = GameData::MakeHeroId(0x4D2),
        HERO_EMRE          = GameData::MakeHeroId(0x4D8),
        HERO_ANRAN         = GameData::MakeHeroId(0x4DD),
        HERO_MIZUKI        = GameData::MakeHeroId(0x4E3),
        HERO_JETPACKCAT    = GameData::MakeHeroId(0x516),
        HERO_SHION         = GameData::MakeHeroId(0x52C),
        HERO_DMON          = GameData::MakeHeroId(0x542),
        HERO_DOCTRINE      = GameData::MakeHeroId(0x570),
        HERO_TRAININGBOT1  = GameData::MakeHeroId(0x33C),
        HERO_TRAININGBOT2  = GameData::MakeHeroId(0x337),
        HERO_TRAININGBOT3  = GameData::MakeHeroId(0x35A),
        HERO_TRAININGBOT4  = GameData::MakeHeroId(0x4E7),
        HERO_TRAININGBOT5  = GameData::MakeHeroId(0x363),
        HERO_TRAININGBOT6  = GameData::MakeHeroId(0x349),
        HERO_TRAININGBOT7  = GameData::MakeHeroId(0x339),
        TOBTERT            = 0x16DD,
        SYMTERT            = 0x16EE,
        Bob                = 0x16BB,
    };

    enum eComponentType {
        TYPE_ERROR            = -1,
        TYPE_TRANSFORM        = 0x01,
        TYPE_VELOCITY         = 0x4,
        // Legacy 151177 values kept for source compatibility. Runtime code
        // must use offset::Active().type* so BZ and CN can diverge.
        TYPE_TEAM             = 0x21,
        TYPE_BONE             = 0x24,  // updated per UC p330 (was 0x27)
        TYPE_TRANSFORM_ALT    = 0x25,
        TYPE_ROTATION         = 0x2F,
        TYPE_LINK             = 0x34,  // legacy 151177 id; active BZ 151722 / CN 153480 profiles use 0x33
        TYPE_P_VISIBILITY     = 0x34,  // active BZ 151722 / CN 153480 id; +0x98 semantics are profile-specific
        TYPE_SKILL            = 0x37,
        TYPE_ANGLE            = 0x39,
        TYPE_HEALTH           = 0x3B,
        TYPE_PLAYERCONTROLLER = 0x43,  // corrected per plexies p332 (was 0x44)
        TYPE_P_HEROID         = 0x54,
        TYPE_OUTLINE          = 0x5A,
        TYPE_REPLICATED_POS   = 0x60,
        TYPE_STAT             = 0x24,
    };

    // =========================================================================
    // Component structs
    // =========================================================================

    // Shared across profiles by the current product contract; non-CN live validation is pending.
    inline constexpr uint64_t kHealthDeathOffset = 0x5C0;
    struct health_compo_t {
        union {
            OFF_MEMB(float,  health,          0xE0);
            OFF_MEMB(float,  health_max,      0xDC);
            OFF_MEMB(Vector2, health_ext,     0xF0);
            OFF_MEMB(float,  armor,           0x220);
            OFF_MEMB(float,  armor_max,       0x21C);
            OFF_MEMB(float,  barrier,         0x360);
            OFF_MEMB(float,  barrier_max,     0x35C);
            OFF_MEMB(bool,   isImmortal,      0x4A9);
            OFF_MEMB(bool,   isBarrierProjected, 0x4A8);
            OFF_MEMB(Vector2, cn154461_health_ext, 0xF4);
            OFF_MEMB(float, cn154461_armor_max, 0x25C);
            OFF_MEMB(float, cn154461_armor, 0x260);
            OFF_MEMB(float, cn154461_barrier_max, 0x3DC);
            OFF_MEMB(float, cn154461_barrier, 0x3E0);
            OFF_MEMB(bool, cn154461_isImmortal, 0x569);
            OFF_MEMB(bool, cn154461_isBarrierProjected, 0x568);
            OFF_MEMB(uint8_t, death, kHealthDeathOffset);
        };
    };

    struct HealthPayloadSummary {
        float health = 0, health_max = 0;
        float armor = 0, armor_max = 0;
        float barrier = 0, barrier_max = 0;
        Vector2 health_ext{};
        bool isImmortal = false, isBarrierProjected = false;
        float total = 0, total_max = 0;
        bool isDead = false;
    };

    inline constexpr size_t kCnHealthPoolBytes = 0x180;
    inline constexpr size_t kCnHealthExtraStride = 0x18;
    inline constexpr size_t kCnHealthMaxExtraPools = (kCnHealthPoolBytes - 0x1C) / kCnHealthExtraStride;

    inline bool TryParseCnHealthPool(const uint8_t* bytes, float& current, float& maximum) {
        uint32_t count = 0;
        std::memcpy(&maximum, bytes + 8, 4);
        std::memcpy(&current, bytes + 12, 4);
        std::memcpy(&count, bytes + 0x18, 4);
        if (count > kCnHealthMaxExtraPools)
            return false;
        const auto valid = [](float value) { return std::isfinite(value) && value >= 0.0f; };
        if (!valid(current) || !valid(maximum))
            return false;
        for (size_t i = 0; i < count; ++i) {
            float extraMax = 0, extraCurrent = 0;
            const uint8_t* extra = bytes + 0x1C + i * kCnHealthExtraStride;
            std::memcpy(&extraMax, extra + 4, 4);
            std::memcpy(&extraCurrent, extra + 8, 4);
            if (!valid(extraMax) || !valid(extraCurrent))
                return false;
            maximum += extraMax;
            current += extraCurrent;
        }
        return valid(current) && valid(maximum);
    }

    inline bool IsHealthSummaryValid(const HealthPayloadSummary& result, uint8_t death) {
        return death <= 1 && std::isfinite(result.total) && std::isfinite(result.total_max) &&
            result.total_max > 0 && result.total_max <= 10000 &&
            result.total >= 0 && result.total <= result.total_max + 2000;
    }

    inline bool TryParseCnHealthPools(const uint8_t* health, const uint8_t* armor,
                                     const uint8_t* barrier, const uint8_t* flags,
                                     uint8_t death, HealthPayloadSummary& result) {
        result = {};
        // Each resource includes every active extra pool; the third scalar is
        // current HP. The first two scalars stay at their configured maxima.
        if (!TryParseCnHealthPool(health, result.health, result.health_max) ||
            !TryParseCnHealthPool(armor, result.armor, result.armor_max) ||
            !TryParseCnHealthPool(barrier, result.barrier, result.barrier_max))
            return false;
        result.isBarrierProjected = flags[0] != 0;
        result.isImmortal = flags[1] != 0;
        result.isDead = death == 1;
        result.total = result.health + result.armor + result.barrier;
        result.total_max = result.health_max + result.armor_max + result.barrier_max;
        return IsHealthSummaryValid(result, death);
    }

    // Call only after a complete read. Invalid totals remain available for
    // diagnostics, but callers must check the result before updating state.
    inline bool TryParseHealthPayload(const health_compo_t& raw, HealthPayloadSummary& result) {
        if (offset::Active().pooledHealth) {
            const auto* bytes = reinterpret_cast<const uint8_t*>(&raw);
            return TryParseCnHealthPools(bytes + 0xD8, bytes + 0x258, bytes + 0x3D8,
                bytes + 0x568, raw.death, result);
        }
        const auto& health = raw;
        result = { health.health, health.health_max, health.armor, health.armor_max,
            health.barrier, health.barrier_max, health.health_ext,
            health.isImmortal, health.isBarrierProjected,
            health.health + health.armor + health.barrier + health.health_ext.Y,
            health.health_max + health.armor_max + health.barrier_max + health.health_ext.X };
        result.isDead = health.death == 1;
        return IsHealthSummaryValid(result, health.death);
    }

    struct velocity_fields_t {
        XMFLOAT3 velocity{}, location{};
        uint64_t bonedata = 0;
    };

    struct health_fields_t {
        std::array<uint8_t, 32> vital{};
        Vector2 armor{}, barrier{}; // legacy maximum, current
        std::array<std::array<uint8_t, kCnHealthPoolBytes>, 3> cnPools{};
        std::array<uint8_t, 2> flags{};
        uint8_t death = 0;
    };

    inline bool TryParseHealthPayload(const health_fields_t& raw, HealthPayloadSummary& result) {
        if (offset::Active().pooledHealth)
            return TryParseCnHealthPools(raw.cnPools[0].data(), raw.cnPools[1].data(),
                raw.cnPools[2].data(), raw.flags.data(), raw.death, result);
        result = {};
        std::memcpy(&result.health_max, raw.vital.data(), 4);
        std::memcpy(&result.health, raw.vital.data() + 4, 4);
        std::memcpy(&result.health_ext, raw.vital.data() + 20, 8);
        result.armor_max = raw.armor.X; result.armor = raw.armor.Y;
        result.barrier_max = raw.barrier.X; result.barrier = raw.barrier.Y;
        result.isBarrierProjected = raw.flags[0] != 0; result.isImmortal = raw.flags[1] != 0;
        result.total = result.health + result.armor + result.barrier + result.health_ext.Y;
        result.total_max = result.health_max + result.armor_max + result.barrier_max + result.health_ext.X;
        result.isDead = raw.death == 1;
        return IsHealthSummaryValid(result, raw.death);
    }

    struct obj_compo_t {
        union {
            OFF_MEMB(XMFLOAT3, obj_pos, 0xE0);
        };
    };

    struct velocity_compo_t {
        union {
            OFF_MEMB(XMFLOAT3,  velocity, 0x50);
            OFF_MEMB(XMFLOAT3,  location, 0x1B0 + 0x50);
            OFF_MEMB(uint64_t,  bonedata, 0x8B0);
        };
    };

    struct hero_compo_t {
        union {
            OFF_MEMB(uint64_t, heroid, 0xD0);
        };
    };

    struct vis_compo_t {
        union {
            OFF_MEMB(uint64_t, key1, 0xA0);
            OFF_MEMB(uint64_t, key2, 0x98);
        };
    };

    // =========================================================================
    // ESP helper types
    // =========================================================================

    struct espBone {
        bool boneerror = false;
        Vector2 upL, upR, downL, downR;
        Vector2 head, neck, body_1, body_2,
                l_1, l_2, r_1, r_2,
                l_d_1, l_d_2, r_d_1, r_d_2,
                l_a_1, l_a_2, r_a_1, r_a_2,
                sex, sex1, sex2, sex3;
    };

    class hpanddy {
    public:
        uint64_t scene_generation = 0;
        uint64_t  entityid = 0;
        uintptr_t MeshBase = 0;
        XMFLOAT3  POS = {};
    };

} // namespace OW
