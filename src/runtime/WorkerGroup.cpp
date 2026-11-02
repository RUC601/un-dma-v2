#include "WorkerGroup.hpp"
#include "Utils/Diagnostics.hpp"
#include <stdexcept>

namespace V2::Runtime {
WorkerGroup::~WorkerGroup() {
    // 正常停止必须由会话完成；不能静默 detach 后让线程访问已关闭的资源。
    for(const auto& thread:workers_)if(thread.joinable())std::terminate();
}
void WorkerGroup::Start(std::span<const Worker> entries, void (*onFailure)()) {
    std::lock_guard lock(mutex_);
    if(running_.load())return;
    if(!workers_.empty())throw std::logic_error("Drain old workers before restarting");
    workers_.reserve(entries.size());
    failed_.store(false,std::memory_order_release);
    // 先标记，确保创建中途失败时连接层仍执行完整 drain。
    running_.store(true,std::memory_order_release);
    for(const auto entry:entries) {
        workers_.emplace_back([this,entry,onFailure] {
            Diagnostics::Info("Worker entering: %s.",entry.name);
            try { entry.entry(); }
            catch(const std::exception& e) {
                Diagnostics::Error("Worker failed: %s: %s",entry.name,e.what());
                failed_.store(true,std::memory_order_release);if(onFailure)onFailure();
            } catch(...) {
                Diagnostics::Error("Worker failed: %s",entry.name);
                failed_.store(true,std::memory_order_release);if(onFailure)onFailure();
            }
            Diagnostics::Info("Worker returned: %s.",entry.name);
        });
    }
}
void WorkerGroup::Join() {
    std::lock_guard lock(mutex_);
    for(auto& thread:workers_)if(thread.joinable())thread.join();
    workers_.clear();running_.store(false,std::memory_order_release);
}
}
