#pragma once
#include "LayoutProbe.hpp"
#include <algorithm>
#include <bit>
#include <cstring>
#include <span>

namespace V2::Profiles {
// KR/BZ 的候选读取器。不会改变 CN 的运行 Profile，也不授权设备输出。
// 来源: profiles.json；12d830.c 的 registry 读取及复核、12a460.c 的 key 读取、
// 12a0e0.c 的 component decode、dc3e0.c 的 admin decode。均未作本轮实服验证。
enum class CandidateStatus { Ready, Unsupported, ReadFailed, Invalid, Changed, Stale };
struct CandidateKeys { uint8_t byte=0; uint64_t owner=0,value=0; };
struct CandidateRegistry {
    uint32_t capacity=0,count=0;
    uint64_t root=0;
    std::array<uint8_t,28> header{};
};
inline bool CandidateRegion(const Profile& profile) {
    return profile.region==Region::KR || profile.region==Region::BZ;
}
template<class Read,class Current>
CandidateStatus ReadCandidateKeys(const Profile& profile,uint64_t module,Read&& read,
                                  Current&& current,CandidateKeys& output) {
    output={};
    if(!current())return CandidateStatus::Stale;
    if(!CandidateRegion(profile))return CandidateStatus::Unsupported;
    // Nexon 原件不读全局 key，组件值为普通指针。
    if(profile.region==Region::KR)return CandidateStatus::Ready;
    CandidateKeys keys;
    if(!read(module+0x37CB373,&keys.byte,1) || !read(module+0x3AF0BF8,&keys.owner,8))
        return CandidateStatus::ReadFailed;
    if(!UserPointer(keys.owner))return CandidateStatus::Invalid;
    if(!current())return CandidateStatus::Stale;
    if(!read(keys.owner+0x1D4,&keys.value,8))return CandidateStatus::ReadFailed;
    if(!current())return CandidateStatus::Stale;
    if(!keys.value)return CandidateStatus::Invalid;
    output=keys;return CandidateStatus::Ready;
}
inline uint64_t DecodeCandidateComponent(const Profile& profile,uint64_t encoded,const CandidateKeys& keys) {
    if(profile.region==Region::KR)return encoded;
    if(profile.region!=Region::BZ)return 0;
    return std::rotr((uint64_t(keys.byte) ^
        (std::rotr(encoded ^ keys.value ^ 0xDC01B58B9BDFFB4BULL,32)+0x024620C984E36588ULL))
        -0x7D957CD64821F39BULL,53);
}
inline uint64_t DecodeCandidateAdmin(const Profile& profile,uint64_t encoded) {
    if(profile.region==Region::KR)return encoded;
    if(profile.region!=Region::BZ)return 0;
    return std::rotr((encoded^0x41D346232DA856D8ULL)-0x53B5A9FA87525DFCULL,59)
        +0x57374166C207BDC2ULL;
}
template<class Read,class Current>
CandidateStatus ReadCandidateRegistry(const Profile& profile,uint64_t module,Read&& read,
                                      Current&& current,CandidateRegistry& output) {
    output={};
    if(!current())return CandidateStatus::Stale;
    if(!CandidateRegion(profile))return CandidateStatus::Unsupported;
    CandidateRegistry candidate;
    if(!read(module+profile.entityRva-20,candidate.header.data(),candidate.header.size()))
        return CandidateStatus::ReadFailed;
    std::memcpy(&candidate.capacity,candidate.header.data(),4);
    std::memcpy(&candidate.count,candidate.header.data()+4,4);
    std::memcpy(&candidate.root,candidate.header.data()+20,8);
    // 同一容量约束用于候选和分步扫描；空表保留结构，不能据此宣称语义有效。
    if(candidate.capacity<64 || candidate.capacity>32768 || candidate.count>candidate.capacity ||
       !UserPointer(candidate.root) || (candidate.root&7))return CandidateStatus::Invalid;
    std::array<uint8_t,28> after{};
    if(!current())return CandidateStatus::Stale;
    if(!read(module+profile.entityRva-20,after.data(),after.size()))return CandidateStatus::ReadFailed;
    if(!current())return CandidateStatus::Stale;
    if(after!=candidate.header)return CandidateStatus::Changed;
    output=candidate;return CandidateStatus::Ready;
}
template<class Read,class Current>
CandidateStatus ReadCandidateSlots(const Profile& profile,uint64_t module,const CandidateRegistry& table,
                                   uint32_t first,std::span<uint64_t> parents,Read&& read,Current&& current) {
    std::fill(parents.begin(),parents.end(),0);
    if(parents.size()>64 || first>table.capacity || parents.size()>table.capacity-first)
        return CandidateStatus::Invalid;
    CandidateRegistry before;
    auto status=ReadCandidateRegistry(profile,module,read,current,before);
    if(status!=CandidateStatus::Ready)return status;
    if(before.header!=table.header)return CandidateStatus::Changed;
    std::array<uint8_t,64*16> bytes{};
    if(!parents.empty() && !read(table.root+uint64_t(first)*16,bytes.data(),parents.size()*16))
        return CandidateStatus::ReadFailed;
    CandidateRegistry after;
    status=ReadCandidateRegistry(profile,module,read,current,after);
    if(status!=CandidateStatus::Ready)return status;
    if(after.header!=table.header)return CandidateStatus::Changed;
    for(size_t i=0;i<parents.size();++i) {
        uint64_t value=0;std::memcpy(&value,bytes.data()+i*16,8);
        if(UserPointer(value) && !(value&7))parents[i]=value;
    }
    return CandidateStatus::Ready;
}
}
