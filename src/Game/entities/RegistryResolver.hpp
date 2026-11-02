#pragma once
#include "Game/EntityRegistry.hpp"
#include <vector>

namespace OW::EntityRegistry {
enum class ResolveStatus { Known, Cached, Searching, Validating, Recovered, Ambiguous, NoMatch, Incomplete, EmptyUnconfirmed };
struct SearchRange { uint64_t begin=0, end=0; }; // 模块内 RVA，半开区间。
struct Candidate { uint64_t location=0, checkedBytes=0; Snapshot table{}; bool entity=false, unreadable=false; };

// 单个会话的状态，只能由注册表 reader lane 操作。调用者在发布前另检查会话。
// 每步最多一个 4KiB 页或一段表、8 次实体语义检查；不在 UI 线程搜索。
class RegistryResolver {
    uint64_t epoch_=0, module_=0, cached_=0, cursor_=0;
    size_t range_=0, candidate_=0;
    bool scanned_=false, missing_=false, overflow_=false;
    std::vector<SearchRange> ranges_;
    std::vector<Candidate> candidates_;
public:
    static constexpr size_t maxEntityChecksPerStep=8;
    ResolveStatus status=ResolveStatus::Searching;
    uint64_t pages=0, bytes=0;
    void Reset(uint64_t epoch, uint64_t module, std::vector<SearchRange> ranges) {
        *this=RegistryResolver{}; epoch_=epoch; module_=module; ranges_=std::move(ranges);
        if (!ranges_.empty()) cursor_=ranges_[0].begin;
    }
    bool Matches(uint64_t epoch,uint64_t module) const { return epoch_==epoch && module_==module; }
    uint64_t Location() const { return cached_; }
    size_t CandidateCount() const { return candidates_.size(); }

    template<class Read> Error Header(uint64_t rva, Read&& read, Snapshot& out) const {
        // 沿用当前 CN 构建的锁协议；不猜测未知构建的锁布局。
        return ReadSnapshotAt(epoch_, module_, rva, lockRva, read, out);
    }
    template<class Read, class Current, class Entity>
    Error Step(Read&& read, Current&& current, Entity&& entity, Snapshot& out) {
        if (!current()) return Error::Disconnected;
        auto exact=[&](uint64_t at,void* dst,size_t n) {
            if (!current()) return false;
            bytes+=n; return read(at,dst,n);
        };
        if(cached_) {
            const auto e=Header(cached_,exact,out);
            if(e==Error::None) { status=ResolveStatus::Cached;return e; }
            if(e==Error::Busy || e==Error::Unstable) return e;
            cached_=0; scanned_=false; range_=candidate_=0; candidates_.clear();
            missing_=overflow_=false; cursor_=ranges_.empty()?0:ranges_[0].begin;
        }
        auto known=Header(stateRva,exact,out);
        if(known==Error::None) { status=ResolveStatus::Known; return known; }
        if(known==Error::Busy || known==Error::Unstable) return known;
        if (!current()) return Error::Disconnected;
        if(!scanned_) {
            status=ResolveStatus::Searching;
            if(range_>=ranges_.size()) { scanned_=true; return Error::Changed; }
            const auto end=ranges_[range_].end;
            std::array<uint8_t,4096> page{};
            const auto n=static_cast<size_t>((std::min)(uint64_t(page.size()),end-cursor_));
            ++pages;
            if(exact(module_+cursor_,page.data(),n)) {
                for(size_t i=0;i+32<=n;i+=8) {
                    Snapshot s{}; s.epoch=epoch_;s.module=module_;
                    std::memcpy(&s.state,page.data()+i,4);
                    std::memcpy(&s.capacity,page.data()+i+4,4);
                    std::memcpy(&s.root,page.data()+i+24,8);
                    if(!Valid(s))continue;
                    const auto location=cursor_+i;
                    if(candidates_.size()==64) { overflow_=true;break; }
                    candidates_.push_back({location,0,s});
                }
            } else missing_=true;
            // 24-byte overlap includes headers crossing a page boundary, aligned to 8.
            cursor_+=n>24?n-24:n;
            if(cursor_+32>end) {
                ++range_; if(range_<ranges_.size())cursor_=ranges_[range_].begin;
            }
            return current()?Error::Changed:Error::Disconnected;
        }
        status=ResolveStatus::Validating;
        if(candidate_<candidates_.size()) {
            auto& c=candidates_[candidate_];
            Snapshot fresh{};
            const auto e=Header(c.location,exact,fresh);
            if(e==Error::Busy || e==Error::Unstable)return e;
            if(e!=Error::None || !fresh.SameTable(c.table)) { c.unreadable=true;++candidate_;return Error::Changed; }
            std::array<uint8_t,4096> chunk{};
            const size_t n=ChunkBytes(c.table,c.checkedBytes,chunk.size());
            if(!n) { ++candidate_;return Error::Changed; }
            size_t consumed=n;
            if(!exact(c.table.root+c.checkedBytes,chunk.data(),n))c.unreadable=true;
            else {
                size_t checks=0;
                for(size_t i=0;i+slotBytes<=n;i+=slotBytes) {
                    if(!current())return Error::Disconnected;
                    uint64_t p=0;std::memcpy(&p,chunk.data()+i,8);
                    if(p>=0x10000 && p<0x800000000000ULL && !(p&7)) {
                        ++checks;
                        if(entity(p,c.table)) { c.entity=true;break; }
                        if(checks==maxEntityChecksPerStep) { consumed=i+slotBytes;break; }
                    }
                }
            }
            c.checkedBytes+=consumed;
            // 非空表结构通过后不用读完；空／稀疏表继续到末尾。
            if(c.entity || c.checkedBytes>=c.table.Bytes())++candidate_;
            return Error::Changed;
        }
        const Candidate* selected=nullptr;
        bool empty=false;
        for(const auto& c:candidates_) {
            if(c.unreadable) { missing_=true;continue; }
            if(!c.entity) { empty=true;continue; }
            if(selected && selected->table.root!=c.table.root) {
                status=ResolveStatus::Ambiguous;return Error::Invalid;
            }
            selected=&c;
        }
        // 搜索不完整、截断或只有空候选时不能宣称唯一根。
        if(missing_ || overflow_) { status=ResolveStatus::Incomplete;return Error::ReadFailed; }
        if(!selected) {status=empty?ResolveStatus::EmptyUnconfirmed:ResolveStatus::NoMatch;return Error::Invalid;}
        Snapshot final{};
        const auto e=Header(selected->location,exact,final);
        if(e!=Error::None) {
            if(e!=Error::Busy && e!=Error::Unstable)status=ResolveStatus::Incomplete;
            return e;
        }
        if(!current())return Error::Disconnected;
        // 末次复核迁表后必须进入可重试终态，否则会永久比较同一旧候选。
        if(!final.SameTable(selected->table)) { status=ResolveStatus::Incomplete;return Error::Changed; }
        cached_=selected->location;out=final;status=ResolveStatus::Recovered;return Error::None;
    }
};
}
