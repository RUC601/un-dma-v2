#pragma once
#include <algorithm>
#include <array>
#include <cstdint>
#include <deque>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace OW::EntityDiscovery {
// Address additions get first service. A rolling fingerprint read also catches
// an allocation reused without a visible table-address transition.
struct PositiveCandidates {
    struct Fingerprint {
        std::array<uint64_t,4> types{};
        uint32_t match=0;
        bool operator==(const Fingerprint&) const = default;
        bool Has(uint32_t type) const { return type/64<types.size() && (types[type/64]&(uint64_t{1}<<(type%64))); }
    };
    struct Entry { Fingerprint value{}; uint32_t attempted=0; bool known=false,accepted=false; uint32_t observed=0; };
    std::vector<uint64_t> members;
    std::unordered_map<uint64_t,Entry> entries;
    std::deque<uint64_t> urgent, probes;
    std::unordered_set<uint64_t> urgentSet, probeSet;
    size_t cursor=0;
    void ObserveAdditionalMembers(std::vector<uint64_t> current) {
        // Exact blocks can contribute additions immediately. They are never
        // evidence of absence; only the completed table replaces the baseline.
        current.insert(current.end(),members.begin(),members.end());
        ObserveMembers(std::move(current));
    }
    void ObserveMembers(std::vector<uint64_t> current) {
        std::sort(current.begin(),current.end());
        current.erase(std::unique(current.begin(),current.end()),current.end());
        for(auto address:current)
            if(!std::binary_search(members.begin(),members.end(),address) && urgentSet.insert(address).second)
                urgent.push_back(address);
        members=std::move(current);
        std::erase_if(entries,[&](const auto& p){return !Contains(p.first);});
        if(cursor>=members.size())cursor=0;
    }
    bool Contains(uint64_t address) const { return std::binary_search(members.begin(),members.end(),address); }
    std::vector<uint64_t> NextHeaders(size_t limit) {
        std::vector<uint64_t> result;
        while(!urgent.empty() && result.size()<limit) {
            const auto address=urgent.front();urgent.pop_front();urgentSet.erase(address);
            if(Contains(address))result.push_back(address);
        }
        for(size_t visited=0;visited<members.size() && result.size()<limit;++visited) {
            const auto address=members[cursor++%members.size()];
            if(std::find(result.begin(),result.end(),address)==result.end())result.push_back(address);
        }
        return result;
    }
    void ObserveHeader(uint64_t address,const Fingerprint& value,bool exact,uint32_t now,uint32_t linkType,uint32_t heroType) {
        if(!Contains(address))return;
        if(!exact) {
            if(auto found=entries.find(address);found!=entries.end())found->second.observed=0;
            return;
        }
        auto& entry=entries[address];
        if(!entry.known || entry.value!=value) {
            entry={value,0,true,false};
        }
        entry.observed=now;
        if(value.match && (value.Has(linkType)||value.Has(heroType)) && !entry.accepted &&
            (!entry.attempted || uint32_t(now-entry.attempted)>=250) && probeSet.insert(address).second)
            probes.push_back(address);
    }
    bool FreshMemberIds(uint32_t now,std::vector<std::pair<uint64_t,uint32_t>>& ids) const {
        ids.clear();ids.reserve(members.size());
        for(auto address:members) {
            const auto entry=entries.find(address);
            if(entry==entries.end() || !entry->second.known || !entry->second.observed ||
                uint32_t(now-entry->second.observed)>500) {ids.clear();return false;}
            ids.emplace_back(address,entry->second.value.match);
        }
        return !members.empty();
    }
    uint64_t NextProbe() {
        while(!probes.empty()) {
            const auto address=probes.front();probes.pop_front();probeSet.erase(address);
            if(Contains(address))return address;
        }
        return 0;
    }
    void FinishProbe(uint64_t address,bool accepted,uint32_t now) {
        if(auto found=entries.find(address);found!=entries.end()) {
            found->second.attempted=now;found->second.accepted=accepted;
        }
    }
};
}
