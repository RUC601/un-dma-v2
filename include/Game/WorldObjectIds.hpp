#pragma once
#include <array>
#include <cstdint>

namespace V2::WorldObjects {
enum class Group { Health, Deployable, Projectile };
struct ObjectType {
    const char* name;
    uint64_t id;
    Group group;
    const char* iconHero;
    const char* iconName;
};
// Pure 1106 Item ESP table RVA 0x3CA160 (36 entries), not its mixed hero/marker registry.
// First 12 indices are a persisted configuration contract. CN/World/Nexon share pool IDs;
// component decoding remains owned by the active version profile. Live acceptance is pending.
inline constexpr std::array kObjects{
    ObjectType{"Health Packs",0x40000000000005Full,Group::Health,"",""},
    ObjectType{"Ashe Dynamite",0x400000000002533ull,Group::Projectile,"ashe","dynamite"},
    ObjectType{"Torb Turret",0x4000000000028B7ull,Group::Deployable,"torbjorn","deploy-turret"},
    ObjectType{"Sym Turret",0x400000000002658ull,Group::Deployable,"symmetra","sentry-turret"},
    ObjectType{"Junkrat Trap",0x40000000000103Bull,Group::Deployable,"junkrat","steel-trap"},
    ObjectType{"Junkrat Mine",0x400000000000F9Bull,Group::Deployable,"junkrat","concussion-mine"},
    ObjectType{"Widow Mine",0x4000000000000AEull,Group::Deployable,"widowmaker","venom-mine"},
    ObjectType{"Healing Pylon",0x400000000004BCAull,Group::Deployable,"illari","healing-pylon"},
    ObjectType{"Immortality Field",0x4000000000029A9ull,Group::Deployable,"baptiste","immortality-field"},
    ObjectType{"B.O.B.",0x4000000000025C3ull,Group::Deployable,"ashe","bob"},
    ObjectType{"Junkrat Tire",0,Group::Deployable,"junkrat","rip-tire"},
    ObjectType{"Bastion Ultimate",0x400000000003EFDull,Group::Projectile,"bastion","configuration-artillery"},
    ObjectType{"Petal Platform",0x400000000004504ull,Group::Deployable,"lifeweaver","petal-platform"},
    ObjectType{"Teleporter Entry",0x4000000000026F2ull,Group::Deployable,"symmetra","teleporter"},
    ObjectType{"Teleporter Exit",0x40000000000272Dull,Group::Deployable,"symmetra","teleporter"},
    ObjectType{"Ice Wall",0x400000000000932ull,Group::Deployable,"mei","ice-wall"},
    ObjectType{"Blizzard Drone",0x400000000000946ull,Group::Deployable,"mei","blizzard"},
    ObjectType{"Proximity Mine",0x400000000002578ull,Group::Deployable,"wrecking-ball","minefield"},
    ObjectType{"Biotic Field",0x40000000000068Aull,Group::Deployable,"soldier-76","biotic-field"},
    ObjectType{"Jagged Wall",0x400000000005920ull,Group::Deployable,"hazard","jagged-wall"},
    ObjectType{"Winston Barrier",0x4000000000001C3ull,Group::Deployable,"winston","barrier-projector"},
    ObjectType{"Orisa Barrier",0x4000000000014A1ull,Group::Deployable,"orisa","protective-barrier"},
    ObjectType{"Domina Barrier",0x40000000000679Full,Group::Deployable,"domina","barrier-array"},
    ObjectType{"Ramattra Barrier",0x40000000000343Full,Group::Deployable,"ramattra","void-barrier"},
    ObjectType{"Ravenous Vortex",0x400000000003523ull,Group::Deployable,"ramattra","ravenous-vortex"},
    ObjectType{"Reinhardt Barrier",0x40000000000011Aull,Group::Deployable,"reinhardt","barrier-field"},
    ObjectType{"Brigitte Barrier",0x400000000002284ull,Group::Deployable,"brigitte","barrier-shield"},
    ObjectType{"Sigma Barrier",0x400000000002D25ull,Group::Deployable,"sigma","experimental-barrier"},
    ObjectType{"Particle Barrier",0x4000000000013F4ull,Group::Deployable,"zarya","particle-barrier"},
    ObjectType{"Projected Barrier",0x4000000000003DAull,Group::Deployable,"zarya","projected-barrier"},
    ObjectType{"Anchor Drone",0x400000000007196ull,Group::Deployable,"",""},
    ObjectType{"Hyper Ring",0x400000000005530ull,Group::Deployable,"juno","hyper-ring"},
    ObjectType{"Trailblazer",0x400000000007255ull,Group::Deployable,"",""},
    ObjectType{"Kekkai Sanctuary",0x400000000006D24ull,Group::Deployable,"mizuki","kekkai-sanctuary"},
    ObjectType{"Biotic Grenade",0x400000000001426ull,Group::Projectile,"ana","biotic-grenade"},
    ObjectType{"Accretion Rock",0x4000000000018B7ull,Group::Projectile,"sigma","accretion"},
    ObjectType{"Bastion Tactical Grenade",0x400000000003F69ull,Group::Projectile,"bastion","a-36-tactical-grenade"}};
constexpr uint64_t NormalizeId(uint64_t id) {
    return id && id <= UINT32_MAX ? id | 0x400000000000000ull : id;
}
constexpr int ObjectIndex(uint64_t id) {
    id = NormalizeId(id);
    if(id==0x400000000000060ull||id==0x40000000000480Aull)return 0;
    for(int i=0;i<int(kObjects.size());++i)if(kObjects[i].id&&kObjects[i].id==id)return i;
    return -1;
}

template<class Read>
bool ReadId(uint64_t parent, Read read, uint64_t& id) {
    const auto pointer=[](uint64_t p){return p>=0x10000 && p<0x800000000000ULL;};
    id=0;
    uint64_t pool=0, poolAfter=0, candidate=0;
    if(!pointer(parent) || !read(parent+0x30,&pool,sizeof(pool)))return false;
    pool &= ~uint64_t(63);
    if(!pointer(pool) || !read(pool+0x10,&candidate,sizeof(candidate)) || ObjectIndex(candidate)<0 ||
        !read(parent+0x30,&poolAfter,sizeof(poolAfter)) || (poolAfter&~uint64_t(63))!=pool)return false;
    id=NormalizeId(candidate);
    return true;
}
}
