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
    size_t matchOffset = 0;
    uint8_t keyA = 0, keyB = 0;
    bool Active() const { return stage == Stage::Slots || stage == Stage::Types || stage == Stage::Verify; }
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
    template<class Batch, class Decode>
    Stage Step(const RefreshIdentity& current, uint32_t tick, Batch&& batch, Decode&& decode) {
        // A paused transaction must never survive a changed lifetime or become a
        // permanently queued owner. A failed transaction is retried from headers.
        if (!(current == identity) || uint32_t(tick - startedTick) > 5000) return stage = Stage::Failed;
        if (!Active()) return stage;
        std::vector<RefreshRequest> requests;
        requests.reserve(48);
        std::array<uint32_t, 48> actual{};
        auto request = [&](uint64_t address, void* output, size_t size) {
            const size_t i = requests.size();
            requests.push_back({address, output, size, &actual[i]});
        };
        auto execute = [&] {
            if (!requests.empty() && !batch(requests)) return false;
            for (const auto& read : requests) if (*read.actual != read.size) return false;
            return true;
        };
        if (stage == Stage::Slots) {
            request(keyAddressA, &keyA, sizeof(keyA)); request(keyAddressB, &keyB, sizeof(keyB));
            std::array<size_t,21> indexes{};
            for (size_t i=0;i<slots.size();++i) if (slots[i].address) {
                indexes[i]=requests.size();request(slots[i].address,&slots[i].encoded,sizeof(slots[i].encoded));
            }
            if (!batch(requests) || actual[0]!=1 || actual[1]!=1) return stage = Stage::Failed;
            for (size_t i=0;i<slots.size();++i)
                if (slots[i].address && actual[indexes[i]]!=8) slots[i].encoded=0;
            for (auto& slot : slots) if (slot.address && slot.encoded) {
                const auto value = decode(slot.encoded, keyA, keyB);
                if (value >= 0x10000 && value < 0x0000800000000000ULL) slot.decoded = value;
            }
            return stage = Stage::Types;
        }
        if (stage == Stage::Types) {
            std::array<size_t,21> indexes{};
            for (size_t i=0;i<slots.size();++i) if (slots[i].decoded) {
                indexes[i]=requests.size();request(slots[i].decoded+0x10,&slots[i].observedType,1);
            }
            if (!requests.empty() && !batch(requests)) return stage = Stage::Failed;
            for (size_t i=0;i<slots.size();++i)
                if (slots[i].decoded && (actual[indexes[i]]!=1 || slots[i].observedType!=slots[i].type)) slots[i].decoded=0;
            return stage = Stage::Verify;
        }
        constexpr std::array<size_t,8> fallback{0,2,3,4,6,7,8,11};
        std::array<size_t,13> chosen{};
        for(size_t i=0;i<chosen.size();++i)chosen[i]=i;
        for(size_t i=0;i<fallback.size();++i)if(!slots[fallback[i]].decoded && slots[13+i].decoded)chosen[fallback[i]]=13+i;
        // A transient failure of a declared life/body field cannot replace a
        // healthy committed cache with zero. Optional fields remain independent.
        for(const size_t field:std::array<size_t,5>{0,1,2,4,5}) {
            bool declared=slots[field].address!=0;
            for(size_t i=0;i<fallback.size();++i)if(fallback[i]==field)declared=declared || slots[13+i].address!=0;
            if(declared && !slots[chosen[field]].decoded)return stage=Stage::Failed;
        }
        std::array<bool,21> selected{};
        for(const auto index:chosen)if(slots[index].decoded)selected[index]=true;
        RefreshHeader currentBody{}, currentLink{};
        std::array<uint64_t, 21> encoded{};
        std::array<uint8_t, 21> types{};
        uint8_t a = 0, b = 0;
        request(identity.values[0] + RefreshHeader::offset, currentBody.bytes.data(), currentBody.bytes.size());
        if (identity.values[0] != identity.values[1])
            request(identity.values[1] + RefreshHeader::offset, currentLink.bytes.data(), currentLink.bytes.size());
        request(keyAddressA, &a, 1); request(keyAddressB, &b, 1);
        for (size_t i = 0; i < slots.size(); ++i) {
            if (selected[i]) {
                request(slots[i].address,&encoded[i],sizeof(encoded[i]));
                request(slots[i].decoded+0x10,&types[i],1);
            }
        }
        if (!execute()) return stage = Stage::Failed;
        if (identity.values[0] == identity.values[1]) currentLink = currentBody;
        if (!component.SameAllocation(currentBody, matchOffset) || !link.SameAllocation(currentLink, matchOffset) ||
            a != keyA || b != keyB) return stage = Stage::Failed;
        for (size_t i = 0; i < slots.size(); ++i)
            if (selected[i] && (encoded[i] != slots[i].encoded || types[i] != slots[i].type))
                return stage = Stage::Failed;
        for(size_t i=0;i<values.size();++i)values[i]=slots[chosen[i]].decoded;
        return stage = Stage::Ready;
    }
};
}
