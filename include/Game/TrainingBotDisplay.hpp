#pragma once

#include "Game/GameData.hpp"

namespace OW {

struct TrainingBotDisplay {
    const char* name = nullptr;
    const char* avatarSlug = nullptr;
};

constexpr TrainingBotDisplay ResolveTrainingBotDisplay(uint64_t heroId, float maxHealth) noexcept {
    using GameData::MakeHeroId;
    switch (heroId) {
    case MakeHeroId(0x16C):
    case MakeHeroId(0x4E7):
        // Live CN reports the same ID for ordinary and heavy friendly bots.
        if (maxHealth >= 490.0f && maxHealth <= 510.0f)
            return {"Friendly Heavy Bot", "tank-bot"};
        return {"Friendly Bot", "friendly-bot"};
    case MakeHeroId(0x363):
        return {"Friendly Heavy Bot", "tank-bot"};
    case MakeHeroId(0x337):
        return {"Tank Bot", "tank-bot"};
    case MakeHeroId(0x33C):
    case MakeHeroId(0x356):
        return {"Standard Bot", "standard-bot"};
    case MakeHeroId(0x339):
    case MakeHeroId(0x39E):
        return {"Training Bot", "standard-bot"};
    case MakeHeroId(0x349):
        return {"Rocket Bot", "rocket-bot"};
    case MakeHeroId(0x35A):
        return {"Sniper Bot", "sniper-bot"};
    case MakeHeroId(0x338):
        return {"Attack Bot", "attack-bot"};
    case MakeHeroId(0x34F):
        return {"Healing Bot", "healing-bot"};
    case MakeHeroId(0x33B):
        return {"Explosive Bot", "explosive-bot"};
    case MakeHeroId(0x16D):
    case MakeHeroId(0x36C):
        return {"Friendly Bot", "training-bot-legacy"};
    case MakeHeroId(0x16B):
    case MakeHeroId(0x16E):
    case MakeHeroId(0x16F):
    case MakeHeroId(0x170):
    case MakeHeroId(0x171):
    case MakeHeroId(0x172):
        return {"Training Bot", "training-bot-legacy"};
    default:
        return {};
    }
}

} // namespace OW
