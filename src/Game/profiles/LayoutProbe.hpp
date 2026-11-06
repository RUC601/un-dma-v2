#pragma once
#include <array>
#include <cmath>
#include <cstdint>
#include "Game/Offsets.hpp"

namespace V2::Profiles {
struct Profile {
    const char* region;
    const char* build;
    OW::offset::RuntimeProfile runtime;

    constexpr const auto& Offsets() const { return OW::offset::ForProfile(runtime); }
};
inline constexpr std::array<Profile,3> profiles{{
    {"CN", "2.25.1.0.155284N", OW::offset::RuntimeProfile::Cn},
    {"World", "2.25.1.0.155284", OW::offset::RuntimeProfile::World155284},
    {"Nexon", "2.25.1.0.155284KB", OW::offset::RuntimeProfile::Nexon155284},
}};
inline constexpr int FindImageProfile(uint32_t timestamp, uint32_t imageSize)
{
    for (size_t i = 0; i < profiles.size(); ++i) {
        const auto& offsets = profiles[i].Offsets();
        if (offsets.imageTimestamp == timestamp && offsets.imageSize == imageSize)
            return static_cast<int>(i);
    }
    return -1;
}
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
template<class Read,class Current>
Probe ProbeCamera(const Profile& profile,uint64_t module,Read&& read,Current&& current) {
    Probe result;
    auto exact=[&](uint64_t at,void* out,size_t n) {
        if(!current()) { result.status=ProbeStatus::Stale;return false; }
        if(!read(at,out,n)) {result.status=ProbeStatus::ReadFailed;return false;}
        return true;
    };
    uint64_t encoded=0,p2=0,p3=0;
    const auto& offsets = profile.Offsets();
    if(!exact(module+offsets.Address_viewmatrix_base,&encoded,8))return result;
    if(!encoded)return result;
    const auto p1=OW::offset::DecodeCameraRoot(offsets, encoded);
    if(!UserPointer(p1)) {result.status=ProbeStatus::InvalidPointer;return result;}
    if(!exact(p1+offsets.VM_P1,&p2,8))return result;
    if(!UserPointer(p2)) {result.status=ProbeStatus::InvalidPointer;return result;}
    if(!exact(p2+offsets.VM_P2,&p3,8))return result;
    if(!UserPointer(p3)) {result.status=ProbeStatus::InvalidPointer;return result;}
    std::array<float,16> a{},b{};
    if(!exact(p3+offsets.VM_ViewMatrix,a.data(),64) || !exact(p3+offsets.VM_ProjMatrix,b.data(),64))return result;
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
