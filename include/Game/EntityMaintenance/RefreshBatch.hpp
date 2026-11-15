#pragma once
#include "ComponentRefresh.hpp"
#include <memory>
#include <span>

namespace OW::EntityMaintenance {
// Each actor keeps its own transaction, failures and identity. Only the I/O
// execution is shared; a short optional field cannot invalidate another actor.
template<class Identity,class Clock,class Batch,class Decode>
size_t AdvanceRefreshBatch(std::span<ComponentRefresh*> owners,Identity&& identity,Clock&& clock,
                          Batch&& batch,Decode&& decode,std::span<const RefreshRequest> extra={}) {
    std::vector<std::unique_ptr<ComponentRefresh::StepPlan>> plans(owners.size());
    std::vector<RefreshRequest> reads;
    reads.reserve(owners.size()*32);
    reads.insert(reads.end(),extra.begin(),extra.end());
    for(size_t i=0;i<owners.size();++i) {
        if(!owners[i] || !owners[i]->Active())continue;
        auto plan=std::make_unique<ComponentRefresh::StepPlan>();
        if(!owners[i]->PrepareStep(identity(i),clock(),*plan))continue;
        reads.insert(reads.end(),plan->requests.begin(),plan->requests.end());
        plans[i]=std::move(plan);
    }
    const bool executed=reads.empty() || batch(reads);
    size_t advanced=0;
    for(size_t i=0;i<owners.size();++i)if(plans[i]) {
        owners[i]->FinishStep(identity(i),clock(),*plans[i],executed,decode);
        ++advanced;
    }
    return advanced;
}

// Exact byte counts are per request even if PrepareEx queued a payload before
// returning failure. Close before destroying the destination/count storage.
template<class MemoryBackend>
bool ReadRefreshBatch(MemoryBackend& memory,const std::vector<RefreshRequest>& requests) {
    auto handle=memory.CreateScatterHandle();
    if(!handle)return false;
    struct Owner {
        MemoryBackend& memory;decltype(handle)& handleRef;
        ~Owner(){if(handleRef)memory.CloseScatterHandle(handleRef);}
    } owner{memory,handle};
    std::vector<unsigned long> counts(requests.size(),0);
    std::vector<bool> prepared(requests.size(),false);
    for(size_t i=0;i<requests.size();++i) {
        const auto& request=requests[i];
        prepared[i]=memory.AddScatterReadRequest(handle,request.address,request.output,request.size,&counts[i]);
    }
    const bool executed=memory.ExecuteReadScatter(handle);
    for(size_t i=0;i<requests.size();++i)*requests[i].actual=prepared[i]?counts[i]:0;
    memory.CloseScatterHandle(handle);handle=nullptr;
    return executed;
}
}
