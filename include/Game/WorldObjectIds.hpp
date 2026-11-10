#pragma once
#include <array>
#include <cstdint>

namespace V2::WorldObjects {
// Forum 516727 p328/p332/p344: candidate pool identities, not actor bindings.
struct ObjectType { const char* name; uint64_t id; };
inline constexpr std::array kObjects{
    ObjectType{"Health Packs",0x40000000000005Full},ObjectType{"Ashe Dynamite",0x400000000002533ull},
    ObjectType{"Torb Turret",0x4000000000028B7ull},ObjectType{"Sym Turret",0x400000000002658ull},
    ObjectType{"Junkrat Trap",0x40000000000103Bull},ObjectType{"Junkrat Mine",0x400000000000F9Bull},
    ObjectType{"Widow Mine",0x4000000000000AEull},ObjectType{"Healing Pylon",0x400000000004BCAull},
    ObjectType{"Immortality Field",0x4000000000029A9ull},ObjectType{"B.O.B.",0x4000000000025C3ull},
    ObjectType{"Junkrat Tire",0},ObjectType{"Bastion Ultimate",0x400000000003EFDull}};
constexpr int ObjectIndex(uint64_t id) {
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
    id=candidate;
    return true;
}
}
