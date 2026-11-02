#pragma once
#include "CandidateAdapter.hpp"
#include <functional>
#include <vector>

namespace V2::Profiles {
struct CandidateIo {
    // read 必须按实际字节数返回；current 必须核对调用方持有的 pid/module/epoch。
    std::function<bool(uint64_t, void*, size_t)> read;
    std::function<bool()> current;
};
struct CandidateSnapshot {
    CandidateStatus status = CandidateStatus::Unsupported;
    uint32_t scanned = 0, capacity = 0;
    bool complete = false;
    std::vector<uint64_t> parents;
};
// 单个采集 owner 持有，不共享可变 cursor。仅候选拓扑，不能冒充 UN 实体或输出授权。
class CandidateSession {
public:
    CandidateStatus Begin(size_t profileIndex, uint64_t module, CandidateIo io);
    CandidateStatus Step(); // 每步最多 64 槽，header 前后核验
    void Reset();
    const CandidateSnapshot& Snapshot() const { return snapshot_; }
    uint64_t DecodeComponent(uint64_t encoded) const;
    uint64_t DecodeAdmin(uint64_t encoded) const;
private:
    CandidateStatus Fail(CandidateStatus status);
    size_t profileIndex_ = profiles.size();
    uint64_t module_ = 0;
    CandidateIo io_;
    CandidateKeys keys_{};
    CandidateRegistry registry_{};
    CandidateSnapshot snapshot_{};
    std::vector<uint64_t> pending_;
};
struct Capability {
    const char* id;
    bool camera, registry, componentDecode, adminDecode, unFields, output;
    bool candidate;
};
std::span<const Capability> Catalog();
}
