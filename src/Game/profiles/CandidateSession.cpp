#include "CandidateSession.hpp"

namespace V2::Profiles {
std::span<const Capability> Catalog() {
    static const std::array<Capability,4> catalog{{
        {profiles[0].id,true,true,true,true,true,true,false},
        {profiles[1].id,true,true,true,false,false,false,true},
        {profiles[2].id,true,true,true,true,false,false,true},
        {profiles[3].id,true,false,false,false,false,false,true}
    }};
    return catalog;
}
void CandidateSession::Reset() {
    profileIndex_=profiles.size();module_=0;io_={};keys_={};registry_={};snapshot_={};pending_.clear();
}
CandidateStatus CandidateSession::Fail(CandidateStatus status) {
    keys_={};registry_={};pending_.clear();snapshot_={};snapshot_.status=status;
    return status;
}
CandidateStatus CandidateSession::Begin(size_t index,uint64_t module,CandidateIo io) {
    Reset();
    if(index>=profiles.size() || !CandidateRegion(profiles[index]))return snapshot_.status;
    profileIndex_=index;module_=module;io_=std::move(io);
    if(!io_.read || !io_.current || !UserPointer(module_))return Fail(CandidateStatus::Invalid);
    const auto& profile=profiles[index];
    auto status=ReadCandidateKeys(profile,module_,io_.read,io_.current,keys_);
    if(status!=CandidateStatus::Ready)return Fail(status);
    status=ReadCandidateRegistry(profile,module_,io_.read,io_.current,registry_);
    if(status!=CandidateStatus::Ready)return Fail(status);
    snapshot_.status=CandidateStatus::Ready;snapshot_.capacity=registry_.capacity;
    pending_.reserve(registry_.count);
    return snapshot_.status;
}
CandidateStatus CandidateSession::Step() {
    if(snapshot_.status!=CandidateStatus::Ready)return snapshot_.status;
    if(!io_.current())return Fail(CandidateStatus::Stale);
    // 已完成结果也必须复核会话和表头；调用方不能无限持有过期拓扑。
    if(snapshot_.complete) {
        CandidateKeys afterKeys;
        auto keyStatus=ReadCandidateKeys(profiles[profileIndex_],module_,io_.read,io_.current,afterKeys);
        if(keyStatus!=CandidateStatus::Ready)return Fail(keyStatus);
        if(afterKeys.byte!=keys_.byte || afterKeys.owner!=keys_.owner || afterKeys.value!=keys_.value)
            return Fail(CandidateStatus::Changed);
        CandidateRegistry after;
        auto status=ReadCandidateRegistry(profiles[profileIndex_],module_,io_.read,io_.current,after);
        if(status!=CandidateStatus::Ready)return Fail(status);
        if(after.header!=registry_.header)return Fail(CandidateStatus::Changed);
        return snapshot_.status;
    }
    std::array<uint64_t,64> parents{};
    const auto count=(std::min)(uint32_t(parents.size()),registry_.capacity-snapshot_.scanned);
    auto status=ReadCandidateSlots(profiles[profileIndex_],module_,registry_,snapshot_.scanned,
        std::span(parents.data(),count),io_.read,io_.current);
    if(status!=CandidateStatus::Ready)return Fail(status);
    for(uint32_t i=0;i<count;++i)if(parents[i])pending_.push_back(parents[i]);
    snapshot_.scanned+=count;
    if(snapshot_.scanned==registry_.capacity) {
        CandidateKeys afterKeys;
        status=ReadCandidateKeys(profiles[profileIndex_],module_,io_.read,io_.current,afterKeys);
        if(status!=CandidateStatus::Ready)return Fail(status);
        if(afterKeys.byte!=keys_.byte || afterKeys.owner!=keys_.owner || afterKeys.value!=keys_.value)
            return Fail(CandidateStatus::Changed);
        CandidateRegistry finalRegistry;
        status=ReadCandidateRegistry(profiles[profileIndex_],module_,io_.read,io_.current,finalRegistry);
        if(status!=CandidateStatus::Ready)return Fail(status);
        if(finalRegistry.header!=registry_.header)return Fail(CandidateStatus::Changed);
        // root 没变也不等于每个 parent 有业务语义。输出明确只叫 candidates。
        snapshot_.parents=std::move(pending_);snapshot_.complete=true;
    }
    return snapshot_.status;
}
uint64_t CandidateSession::DecodeComponent(uint64_t encoded) const {
    return snapshot_.status==CandidateStatus::Ready && io_.current && io_.current()
        ? DecodeCandidateComponent(profiles[profileIndex_],encoded,keys_) : 0;
}
uint64_t CandidateSession::DecodeAdmin(uint64_t encoded) const {
    // KR admin 来源地址未建立，即使普通解码是恒等式也不承诺可读 admin。
    return snapshot_.status==CandidateStatus::Ready && profiles[profileIndex_].region==Region::BZ &&
        io_.current && io_.current()?DecodeCandidateAdmin(profiles[profileIndex_],encoded):0;
}
}
