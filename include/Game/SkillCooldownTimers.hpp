#pragma once
#include "Game/OverlayStatus.hpp"

namespace OW::CooldownTimers {
    struct Rule {
        float seconds = 0;
        bool afterEnd = false;
        uint8_t activeSlot = 0;
    };
    using Rules = std::array<Rule,2>;
    // Wiki normal 5v5 PvP baseline, retrieved 2026-11-05; NOT Stadium or 6v6 values.
    // https://wiki.counterpick.app/zh-cn/abilities/ and /zh-cn/heroes/<hero-slug>/
    // Current snapshots and per-ability source URLs: Scenario reports/timer-smooth-20261105/wiki-baselines.json.
    // Fandom's old index is stale; do not restore its older cooldown numbers.
    // HUD icon order. Zero means resource/charges/conditional reset or unverified mapping:
    // keep unknown rather than infer availability from a single recharge timer.
    // Custom room multipliers, role passives, perks and hit/kill resets can differ.
    inline Rules ForHero(uint64_t hero) {
        if ((hero & 0xffffffffffff0000ull) != 0x02e0000000000000ull) return {};
        auto pair=[](float a,float b,bool endA=false,bool endB=false)->Rules {
            return {{{a,endA,0},{b,endB,1}}};
        };
        switch(hero & 0xffff) {
        case 0x002: return pair(8,10,true,true); // Wraith / Shadow Step
        case 0x003: return pair(0,12);          // Tracer: Blink charges unknown.
        case 0x004: return pair(1.5f,0,true);   // Mercy: jump extension/perks can prolong GA; Rez success unknown.
        case 0x005: return pair(12,8);          // Hanzo
        case 0x006: return {{{0,false,1},{10,false,0}}}; // Turret combat/destroy state is not observed.
        case 0x007: return pair(7,0,true);      // Reinhardt / Fire Strike charges
        case 0x008: return pair(14,8);          // Pharah
        case 0x009: return pair(5,10);          // Winston
        case 0x00a: return pair(10,15);         // Widowmaker
        case 0x015: return pair(12,8,true);     // Bastion
        case 0x016: return pair(0,12);          // Symmetra / turret charges unknown
        case 0x020: return {};                 // Zenyatta orb assignments are not ordinary CD timers.
        case 0x029: return pair(0,8,false,true);// Genji dash resets are not observable from active alone.
        case 0x040: return pair(7,0,true);      // Hook / Breather resource
        case 0x042: return pair(5,12);         // Cassidy
        case 0x065: return pair(0,10);         // Junkrat / mine charges unknown
        case 0x068: return {};                 // Shared charged bubbles.
        case 0x06e: return pair(0,15);         // Sprint / Biotic Field
        case 0x079: return pair(0,12);         // Crossfade / Amp
        case 0x07a: return pair(4,7,true);     // D.Va
        case 0x0dd: return pair(12,12,true);   // Mei: Ice Wall starts on placement.
        case 0x12e: return pair(0,7.5f);       // Cyberspace return/reset semantics unverified.
        case 0x12f: return pair(6.5f,7);       // Doomfist
        case 0x13b: return pair(14,12);        // Ana
        case 0x13e: return pair(15.5f,8,false,true); // Orisa
        case 0x195: return pair(4,0);          // Whip Shot / charged Repair Pack
        case 0x1a2: return pair(6,8,true);      // Moira
        case 0x1ca: return pair(0,15);         // Roll / Adaptive Shield
        case 0x1ec: return pair(7,15);         // Sojourn
        case 0x200: return pair(10,12);        // Coach Gun / Dynamite
        case 0x206: return pair(6,8,true,true);// Echo Flight / Focusing Beam
        case 0x221: return pair(12,20);        // Baptiste
        case 0x231: return pair(7,14);         // Kiriko
        case 0x236: return pair(10,0,true);    // Junker Queen: Carnage hit reduction unknown.
        case 0x23b: return pair(12,10,true);   // Sigma
        case 0x28d: return pair(0,11);         // Nemesis ultimate reset unknown.
        case 0x291: return pair(12,18);        // Lifeweaver
        case 0x30a: return pair(6,12,true);    // Mauga
        case 0x31c: return pair(8,0);          // Pylon destruction/recall is not its placement-active flag.
        case 0x32a: return pair(0,10);         // Freja dash has charges.
        case 0x32b: return pair(8,8,true);     // Venture
        case 0x362: return pair(5.5f,12);     // Hazard
        case 0x365: return pair(6,14,true);   // Juno
        case 0x3c3: return pair(8,14,true);   // Wuyang
        case 0x472: return pair(10,7);       // Vendetta
        case 0x4c4: return pair(7,7);        // Domina
        case 0x4d2: return pair(11,7);       // Sierra
        case 0x4d8: return pair(11,0,true);  // Emre / Cyber Frag charges unknown
        case 0x4dd: return pair(0,8);        // Anran / Inferno Rush charges and hit reduction unknown
        case 0x4e3: return pair(0,12);       // Mizuki: return/recast edge unverified.
        case 0x516: return pair(2,12);       // Jetpack Cat
        case 0x52c: return pair(6,15,false,true); // Shion
        case 0x570: return pair(10,14);      // Doctrine; infused variants may alter behavior.
        default: return {};                  // Never invent times for unmapped/new heroes.
        }
    }

    struct State {
        bool initialized = false;
        bool active = false;
        bool known = false;
        double deadline = 0;
        uint32_t observedTick = 0;
        void Observe(bool next, bool readOk, Rule rule, double now, uint32_t tick) {
            if (!readOk) return;
            if (observedTick && uint32_t(tick-observedTick)>1000) {
                initialized = false;
                known = false; // A long observation gap may conceal casts or cooldown resets.
            }
            observedTick = tick;
            if (initialized && rule.seconds > 0 && next != active &&
                (rule.afterEnd ? active && !next : !active && next)) {
                deadline = now + rule.seconds;
                known = true;
            }
            active = next;
            initialized = true;
        }
        OverlayStatus::CooldownSample Sample(double now, uint32_t tick, Rule rule) const {
            OverlayStatus::CooldownSample result;
            if (!known || !rule.seconds || !observedTick || uint32_t(tick-observedTick)>1000 ||
                (rule.afterEnd && active)) return result;
            result.seconds = float((std::max)(0.0,deadline-now));
            result.tick = observedTick;
            result.valid = true;
            result.estimated = true;
            result.deadline = deadline;
            return result;
        }
    };
}
