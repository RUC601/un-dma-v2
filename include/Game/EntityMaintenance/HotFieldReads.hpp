#pragma once
#include <Windows.h>
#include "Game/Structs.hpp"

namespace OW {
    struct EntityHotFieldReads {
        OW::velocity_fields_t velocity{};
        OW::health_fields_t health{};
        struct { uint64_t heroid=0; } hero;
        uint64_t visibilityRaw=0;
        std::array<DWORD,3> velocityBytes{};
        std::array<DWORD,4> healthBytes{};
        DWORD heroBytes=0, visibilityBytes=0;
        DWORD deathBytes=0;
        bool deathRequested=false;
        bool velocityRequested=false, healthRequested=false, heroRequested=false, visibilityRequested=false;
        bool scatterExecuted=false, scatterOk=false;
        bool VelocityRead() const { return scatterOk && velocityRequested && velocityBytes[0]==12 && velocityBytes[1]==12; }
        bool DeathRead() const { return scatterOk && deathRequested && deathBytes==1 && health.death<=1; }
        bool HealthRead() const {
            const bool pooled = offset::Active().pooledHealth;
            const std::array<DWORD,4> expected = pooled
                ? std::array<DWORD,4>{kCnHealthPoolBytes,kCnHealthPoolBytes,kCnHealthPoolBytes,2}
                : std::array<DWORD,4>{32,8,8,2};
            return DeathRead() && healthRequested && healthBytes == expected;
        }
        bool HeroRead() const { return scatterOk && heroRequested && heroBytes==8; }
        bool VisibilityRead() const { return scatterOk && visibilityRequested && visibilityBytes==8; }
    };
}
