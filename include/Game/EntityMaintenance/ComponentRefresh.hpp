#pragma once
#include <array>
#include <bit>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <vector>

namespace OW::EntityMaintenance {
// Runtime-independent transaction core. Production supplies the existing CN
// decoder and exact scatter adapter; tests supply a deterministic memory image.
struct RefreshHeader {
    static constexpr size_t offset = 0x30, size = 0x120;
    std::array<uint8_t, size> bytes{};
    template<class T> T At(size_t parentOffset) const {
        T value{};
        if (parentOffset >= offset && parentOffset - offset + sizeof(T) <= bytes.size())
            std::memcpy(&value, bytes.data() + parentOffset - offset, sizeof(T));
        return value;
    }
    std::array<uint64_t, 6> Layout() const {
        return {At<uint64_t>(0x80), At<uint64_t>(0x110), At<uint64_t>(0x118),
            At<uint64_t>(0x120), At<uint64_t>(0x128), At<uint32_t>(0x130)};
    }
    bool SameAllocation(const RefreshHeader& other, size_t matchOffset) const {
        return Layout() == other.Layout() && At<uint32_t>(matchOffset) == other.At<uint32_t>(matchOffset);
    }
};
struct RefreshRequest { uint64_t address; void* output; size_t size; uint32_t* actual; };
struct RefreshIdentity {
    // component/link, connection, scene, registry, profile, module, body
    // generation, per-body authority revision and absence revision. Unrelated
    // actors advancing the global observation fence must not restart this owner.
    std::array<uint64_t, 10> values{};
    bool operator==(const RefreshIdentity&) const = default;
};
struct ComponentRefresh {
    enum class Stage : uint8_t { Empty, Slots, Types, Verify, Ready, Failed };
    struct Slot {
        uint64_t address = 0, encoded = 0, decoded = 0;
        uint32_t type = 0;
        uint8_t observedType = 0;
    };
    Stage stage = Stage::Empty;
    RefreshIdentity identity{};
    RefreshHeader component{}, link{};
    std::array<Slot, 21> slots{};
    std::array<uint64_t, 13> values{};
    uint64_t keyAddressA = 0, keyAddressB = 0;
    uint32_t startedTick = 0;
    uint32_t verifiedTick = 0;
    size_t matchOffset = 0;
    uint8_t keyA = 0, keyB = 0;
    bool Active() const { return stage == Stage::Slots || stage == Stage::Types || stage == Stage::Verify; }
    bool ReadyFresh(uint32_t tick,uint32_t maxAge) {
        if(stage!=Stage::Ready)return false;
        if(uint32_t(tick-verifiedTick)<maxAge)return true;
        // Queue the existing final validation again; waiting never renews it.
        stage=Stage::Verify;verifiedTick=0;values={};return false;
    }
    bool Begin(const RefreshIdentity& owner, const RefreshHeader& body, const RefreshHeader& controller,
               const std::array<uint32_t, 13>& types, uint64_t keyAtA, uint64_t keyAtB,
               size_t matchAt, uint32_t tick) {
        *this = {};
        identity = owner; component = body; link = controller;
        keyAddressA = keyAtA; keyAddressB = keyAtB; matchOffset = matchAt; startedTick = tick;
        if (!component.At<uint32_t>(matchAt) || !link.At<uint32_t>(matchAt)) {
            stage = Stage::Failed; return false;
        }
        constexpr std::array<bool, 13> linked{false,true,false,false,false,true,false,false,false,true,true,false,true};
        constexpr std::array<size_t, 8> fallback{0,2,3,4,6,7,8,11};
        auto plan = [&](Slot& slot, uint32_t type, const RefreshHeader& header) {
            slot.type = type;
            const auto layout = header.Layout();
            const auto bucket = type >> 6, bit = type & 63;
            if (!layout[0] || bucket >= 4 || !(layout[1 + bucket] & (uint64_t{1} << bit))) return;
            const auto lower = layout[1 + bucket] & ((uint64_t{1} << bit) - 1);
            const auto index = ((layout[5] >> (bucket * 8)) & 255) + std::popcount(lower);
            slot.address = layout[0] + index * 8;
        };
        for (size_t i = 0; i < types.size(); ++i) plan(slots[i], types[i], linked[i] ? link : component);
        if (owner.values[0] != owner.values[1])
            for (size_t i = 0; i < fallback.size(); ++i) plan(slots[13 + i], types[fallback[i]], link);
        stage = Stage::Slots;
        return true;
    }
    // A plan owns the byte counts and verification payload until the shared
    // scatter completes. Do not move a prepared plan or its ComponentRefresh.
    struct StepPlan {
        Stage stage=Stage::Empty;
        RefreshIdentity identity{};
        std::vector<RefreshRequest> requests;
        std::array<uint32_t,48> actual{};
        std::array<size_t,21> indexes{};
        std::array<size_t,13> chosen{};
        std::array<bool,21> selected{};
        RefreshHeader currentBody{},currentLink{};
        std::array<uint64_t,21> encoded{};
        std::array<uint8_t,21> types{};
        uint8_t a=0,b=0;
        void Read(uint64_t address,void* output,size_t size) {
            const auto index=requests.size();
            requests.push_back({address,output,size,&actual[index]});
        }
    };
    bool PrepareStep(const RefreshIdentity& current,uint32_t tick,StepPlan& plan) {
        plan=StepPlan{};
        if(!(current==identity) || uint32_t(tick-startedTick)>5000) {
            stage=Stage::Failed;return false;
        }
        if(!Active())return false;
        plan.stage=stage;plan.identity=identity;plan.requests.reserve(48);
        if(stage==Stage::Slots) {
            plan.Read(keyAddressA,&keyA,1);plan.Read(keyAddressB,&keyB,1);
            for(size_t i=0;i<slots.size();++i)if(slots[i].address) {
                plan.indexes[i]=plan.requests.size();
                plan.Read(slots[i].address,&slots[i].encoded,sizeof(slots[i].encoded));
            }
            return true;
        }
        if(stage==Stage::Types) {
            for(size_t i=0;i<slots.size();++i)if(slots[i].decoded) {
                plan.indexes[i]=plan.requests.size();
                plan.Read(slots[i].decoded+0x10,&slots[i].observedType,1);
            }
            return true;
        }
        constexpr std::array<size_t,8> fallback{0,2,3,4,6,7,8,11};
        for(size_t i=0;i<plan.chosen.size();++i)plan.chosen[i]=i;
        for(size_t i=0;i<fallback.size();++i)
            if(!slots[fallback[i]].decoded && slots[13+i].decoded)plan.chosen[fallback[i]]=13+i;
        for(const size_t field:std::array<size_t,5>{0,1,2,4,5}) {
            bool declared=slots[field].address!=0;
            for(size_t i=0;i<fallback.size();++i)if(fallback[i]==field)declared=declared || slots[13+i].address!=0;
            if(declared && !slots[plan.chosen[field]].decoded) {stage=Stage::Failed;return false;}
        }
        for(const auto index:plan.chosen)if(slots[index].decoded)plan.selected[index]=true;
        plan.Read(identity.values[0]+RefreshHeader::offset,plan.currentBody.bytes.data(),RefreshHeader::size);
        if(identity.values[0]!=identity.values[1])
            plan.Read(identity.values[1]+RefreshHeader::offset,plan.currentLink.bytes.data(),RefreshHeader::size);
        plan.Read(keyAddressA,&plan.a,1);plan.Read(keyAddressB,&plan.b,1);
        for(size_t i=0;i<slots.size();++i)if(plan.selected[i]) {
            plan.Read(slots[i].address,&plan.encoded[i],8);
            plan.Read(slots[i].decoded+0x10,&plan.types[i],1);
        }
        return true;
    }
    template<class Decode>
    Stage FinishStep(const RefreshIdentity& current,uint32_t tick,const StepPlan& plan,bool executed,Decode&& decode) {
        if(!(current==identity) || !(plan.identity==identity) || stage!=plan.stage ||
           uint32_t(tick-startedTick)>5000 || !executed)return stage=Stage::Failed;
        if(stage==Stage::Slots) {
            if(plan.actual[0]!=1 || plan.actual[1]!=1)return stage=Stage::Failed;
            for(size_t i=0;i<slots.size();++i)
                if(slots[i].address && plan.actual[plan.indexes[i]]!=8)slots[i].encoded=0;
            for(auto& slot:slots)if(slot.address && slot.encoded) {
                const auto value=decode(slot.encoded,keyA,keyB);
                if(value>=0x10000 && value<0x0000800000000000ULL)slot.decoded=value;
            }
            return stage=Stage::Types;
        }
        if(stage==Stage::Types) {
            for(size_t i=0;i<slots.size();++i)
                if(slots[i].decoded && (plan.actual[plan.indexes[i]]!=1 || slots[i].observedType!=slots[i].type))slots[i].decoded=0;
            return stage=Stage::Verify;
        }
        if(stage!=Stage::Verify)return stage=Stage::Failed;
        for(const auto& read:plan.requests)if(*read.actual!=read.size)return stage=Stage::Failed;
        const auto& finalLink=identity.values[0]==identity.values[1]?plan.currentBody:plan.currentLink;
        if(!component.SameAllocation(plan.currentBody,matchOffset) || !link.SameAllocation(finalLink,matchOffset) ||
           plan.a!=keyA || plan.b!=keyB)return stage=Stage::Failed;
        for(size_t i=0;i<slots.size();++i)
            if(plan.selected[i] && (plan.encoded[i]!=slots[i].encoded || plan.types[i]!=slots[i].type))return stage=Stage::Failed;
        for(size_t i=0;i<values.size();++i)values[i]=slots[plan.chosen[i]].decoded;
        verifiedTick=tick;
        return stage=Stage::Ready;
    }
    template<class Batch,class Decode>
    Stage Step(const RefreshIdentity& current,uint32_t tick,Batch&& batch,Decode&& decode) {
        StepPlan plan;
        if(!PrepareStep(current,tick,plan))return stage;
        const bool executed=plan.requests.empty() || batch(plan.requests);
        return FinishStep(current,tick,plan,executed,decode);
    }
};
}
