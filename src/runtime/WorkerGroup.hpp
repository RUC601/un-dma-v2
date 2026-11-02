#pragma once
#include <atomic>
#include <functional>
#include <mutex>
#include <span>
#include <thread>
#include <vector>

namespace V2::Runtime {
struct Worker { const char* name; void (*entry)(); };
// Session owner starts/stops this group. Stop signal is separate from join:
// callers first close publication/output gates, then drain, then release DMA.
class WorkerGroup {
    mutable std::mutex mutex_;
    std::vector<std::thread> workers_;
    std::atomic<bool> running_{false}, failed_{false};
public:
    WorkerGroup()=default;
    WorkerGroup(const WorkerGroup&)=delete;
    ~WorkerGroup();
    bool Running() const noexcept {return running_.load(std::memory_order_acquire);}
    bool Failed() const noexcept {return failed_.load(std::memory_order_acquire);}
    void Start(std::span<const Worker> workers, void (*onFailure)());
    void Join();
};
}
