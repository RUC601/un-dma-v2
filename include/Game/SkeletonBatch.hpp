#pragma once
#include "Game/SkeletonRead.hpp"
#include <algorithm>
#include <span>
#include <vector>

namespace OW::Skeleton {
enum class GuardResult { Same, Changed, Unavailable };

struct PointerGuard {
    uint64_t before=0,after=0;
    unsigned long beforeBytes=0,afterBytes=0;
    bool beforePrepared=false,afterPrepared=false,executed=false;
    bool Stable() const {
        return executed && beforePrepared && afterPrepared &&
            beforeBytes==sizeof(before) && afterBytes==sizeof(after) && before && before==after;
    }
    bool Matches(uint64_t expected) const { return expected && Stable() && before==expected; }
};

// A missing frame is not evidence of a changed layout. Retry cached addresses
// in the next hot batch; ask the independently scheduled cold owner to repair
// only after distinct failed samples span the existing freshness budget.
struct GuardFailureSeries {
    uint32_t first = 0, last = 0;
    bool active = false, repeated = false;
    void Reset() { *this = {}; }
    bool Expired(uint32_t now,uint32_t freshnessMs) {
        if(!active){active=true;first=last=now;return false;}
        if(now!=last){repeated=true;last=now;}
        return repeated && uint32_t(now-first)>=freshnessMs;
    }
};
// Stable mapping stays in the owner cache. A frame validates only metadata and
// the ID slots whose poses it will actually consume, never another full ID map.
// All requests use cached addresses and can be prepared for every actor before
// one scatter execution. A second guard batch fences the pose batch at commit.
struct LayoutGuard {
    uint64_t root = 0;
    std::array<uint8_t, 0x28> header{};
    std::array<uint8_t, 0x30> descriptor{}; // descriptor + 0x38 .. +0x67
    std::array<uint8_t, 8> form{};
    std::array<uint32_t, MaxBones> ids{};
    std::array<unsigned long, MaxBones> idBytes{};
    std::vector<uint16_t> indices;
    unsigned long rootBytes = 0, headerBytes = 0, descriptorBytes = 0, formBytes = 0;
    bool prepared = false, executed = false, dva = false;

    template<class Prepare>
    bool Queue(const Layout& expected, const BodyIdentity& body, bool isDva,
               std::span<const int> usedIndices, Prepare&& prepare) {
        *this = {};
        dva = isDva;
        if (!body.alive || !expected.sourceAddress || !expected.boneData || !expected.bonesBase ||
            !expected.bonePtr || !expected.idTable || !expected.count || expected.count > MaxBones)
            return false;
        for (const auto index : usedIndices) {
            if (index < 0) continue;
            if (index >= expected.count) return false;
            indices.push_back(static_cast<uint16_t>(index));
        }
        std::sort(indices.begin(), indices.end());
        indices.erase(std::unique(indices.begin(), indices.end()), indices.end());
        prepared = true;
        const auto request = [&](uint64_t at, void* output, size_t size, unsigned long& count) {
            const bool ok = at && prepare(at, output, size, &count);
            prepared = prepared && ok;
        };
        request(expected.sourceAddress, &root, sizeof(root), rootBytes);
        request(expected.boneData, header.data(), header.size(), headerBytes);
        request(AddressOffset(expected.bonePtr, 0x38), descriptor.data(), descriptor.size(), descriptorBytes);
        if (dva) request(AddressOffset(body.link, 0xD4), form.data(), form.size(), formBytes);
        for (const auto index : indices)
            request(AddressOffset(expected.idTable, uint64_t(index) * sizeof(uint32_t)),
                &ids[index], sizeof(uint32_t), idBytes[index]);
        return prepared;
    }

    GuardResult Compare(const Layout& expected) const {
        if (!prepared || !executed) return GuardResult::Unavailable;
        bool complete=rootBytes==sizeof(root) && headerBytes==header.size() && descriptorBytes==descriptor.size();
        if ((rootBytes==sizeof(root) && root!=expected.boneData) ||
            (headerBytes==header.size() && (Field<uint64_t>(header,0)!=expected.bonePtr ||
                Field<uint64_t>(header,0x20)!=expected.bonesBase)) ||
            (descriptorBytes==descriptor.size() && (Field<uint64_t>(descriptor,0)!=expected.idTable ||
                Field<uint16_t>(descriptor,BoneCountOffset()-0x38)!=expected.count)))
            return GuardResult::Changed;
        if (dva) {
            complete=complete && formBytes==form.size() && expected.formKnown;
            if(formBytes==form.size() && expected.formKnown &&
                (Field<uint16_t>(form,0)!=expected.linkD4 || Field<uint16_t>(form,4)!=expected.linkD8))
                return GuardResult::Changed;
        }
        for (const auto index : indices) {
            if(index>=expected.count || index>=MaxBones)return GuardResult::Changed;
            complete=complete && idBytes[index]==sizeof(uint32_t);
            if(idBytes[index]==sizeof(uint32_t) && ids[index]!=expected.ids[index])return GuardResult::Changed;
        }
        return complete?GuardResult::Same:GuardResult::Unavailable;
    }

    bool Matches(const Layout& expected) const { return Compare(expected)==GuardResult::Same; }

    bool Covers(int index) const {
        return index >= 0 && index < MaxBones &&
            std::binary_search(indices.begin(), indices.end(), static_cast<uint16_t>(index));
    }
};
}
