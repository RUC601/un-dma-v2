#pragma once
#include <array>
#include <cmath>
#include <cstdint>

namespace V2::Profiles {
enum class Region { CN, KR, BZ, Unclassified };
struct Profile {
    const char* id;
    Region region;
    uint64_t cameraRva, entityRva, adminRva;
    uint64_t addBefore, xorValue, addAfter;
    bool encoded, candidate;
};
// 来源：Pure/research/20261028-offset-profiles/profiles.json；
// 探测控制流：evidence/12aca0.c。仅证明布局兼容，不推断渠道身份。
inline constexpr std::array<Profile,4> profiles{{
    {"cn-154504n",Region::CN,0x4BCBCE8,0x4C33BF8,0x4D98760,
     0,0xD9B0DF0D8532B5A0ULL,0x488A1F0DC13B6040ULL,true,false},
    {"kr-nexon-152688",Region::KR,0x3CD33B0,0x35E8568,0,0,0,0,false,true},
    {"bz-battlenet-152690",Region::BZ,0x3930050,0x398F358,0x3AF6A70,
     0x7821C8A55B94CFA0ULL,0xB5B70DFEBEFA4BB5ULL,0x6E6C0E878F0FE669ULL,true,true},
    {"normal-154504-unclassified",Region::Unclassified,0x39AEF28,0x3A16E28,0x3B7F410,
     0xB2485AD1CF871D2CULL,0x89D3E0B8C0994ABFULL,0x820B88E340AB6EB0ULL,true,true}
}};
enum class ProbeStatus { Match, NotReady, ReadFailed, InvalidPointer, InvalidMatrix, Stale };
enum class DetectionStatus { Matched, NotReady, NoMatch, Ambiguous, Stale };
struct Probe {
    ProbeStatus status=ProbeStatus::NotReady;
    uint64_t camera=0;
    std::array<float,16> combined{};
};
struct Detection {
    DetectionStatus status=DetectionStatus::NotReady;
    int selected=-1, matches=0;
    std::array<Probe,profiles.size()> evidence{};
};
inline bool UserPointer(uint64_t p) { return p>0x10000 && p<0x800000000000ULL; }
inline uint64_t DecodeCamera(const Profile& p,uint64_t value) {
    return p.encoded?((value+p.addBefore)^p.xorValue)+p.addAfter:value;
}
template<class Read,class Current>
Probe ProbeCamera(const Profile& profile,uint64_t module,Read&& read,Current&& current) {
    Probe result;
    auto exact=[&](uint64_t at,void* out,size_t n) {
        if(!current()) { result.status=ProbeStatus::Stale;return false; }
        if(!read(at,out,n)) {result.status=ProbeStatus::ReadFailed;return false;}
        return true;
    };
    uint64_t encoded=0,p2=0,p3=0;
    if(!exact(module+profile.cameraRva,&encoded,8))return result;
    if(!encoded)return result;
    const auto p1=DecodeCamera(profile,encoded);
    if(!UserPointer(p1)) {result.status=ProbeStatus::InvalidPointer;return result;}
    if(!exact(p1+0x20,&p2,8))return result;
    if(!UserPointer(p2)) {result.status=ProbeStatus::InvalidPointer;return result;}
    if(!exact(p2+0x48,&p3,8))return result;
    if(!UserPointer(p3)) {result.status=ProbeStatus::InvalidPointer;return result;}
    std::array<float,16> a{},b{};
    if(!exact(p3+0x140,a.data(),64) || !exact(p3+0xb0,b.data(),64))return result;
    b[3]=0;
    for(size_t row=0;row<4;++row)for(size_t col=0;col<4;++col)
        for(size_t k=0;k<4;++k)result.combined[row*4+col]+=a[row*4+k]*b[k*4+col];
    for(float value:result.combined)if(!std::isfinite(value)) {
        result.status=ProbeStatus::InvalidMatrix;return result;
    }
    if(std::abs(result.combined[0])<=1e-4f || std::abs(result.combined[5])<=1e-4f ||
       std::abs(result.combined[3])+std::abs(result.combined[7])+std::abs(result.combined[11])<=1e-4f) {
        result.status=ProbeStatus::InvalidMatrix;return result;
    }
    if(!current()) {result.status=ProbeStatus::Stale;return result;}
    result.camera=p1;result.status=ProbeStatus::Match;return result;
}
template<class Read,class Current>
Detection Detect(uint64_t module,Read&& read,Current&& current,int manual=-1) {
    Detection result;
    bool pending=false;
    for(size_t i=0;i<profiles.size();++i) {
        if(manual>=0 && int(i)!=manual)continue;
        auto& probe=result.evidence[i];probe=ProbeCamera(profiles[i],module,read,current);
        if(probe.status==ProbeStatus::Stale) {result.status=DetectionStatus::Stale;return result;}
        if(probe.status==ProbeStatus::Match) {++result.matches;result.selected=int(i);}
        pending|=probe.status==ProbeStatus::NotReady || probe.status==ProbeStatus::ReadFailed;
    }
    result.status=result.matches>1?DetectionStatus::Ambiguous:result.matches==1?DetectionStatus::Matched:
        pending?DetectionStatus::NotReady:DetectionStatus::NoMatch;
    if(result.matches!=1)result.selected=-1;
    return result;
}
}
