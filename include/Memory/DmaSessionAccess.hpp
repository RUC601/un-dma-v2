#pragma once

#include <condition_variable>
#include <cstddef>
#include <mutex>
#include <utility>

// Readers may run concurrently. Closing admission first prevents a stream of
// new calls from starving shutdown; destruction waits for existing calls only.
// Nested leases are allowed, so resolver helpers can call Memory safely.
class DmaSessionAccess {
public:
    struct Activity { std::size_t activeLeases; bool admissionOpen; };
    Activity Inspect() const {
        std::lock_guard<std::mutex> lock(mutex_);
        return {active_,open_};
    }
    class Lease {
        friend class DmaSessionAccess;
        DmaSessionAccess* owner_ = nullptr;
        explicit Lease(DmaSessionAccess* owner) : owner_(owner) {}
    public:
        Lease(const Lease&) = delete;
        Lease& operator=(const Lease&) = delete;
        Lease(Lease&& other) noexcept : owner_(std::exchange(other.owner_, nullptr)) {}
        ~Lease() { if (owner_) owner_->Release(); }
        explicit operator bool() const noexcept { return owner_ != nullptr; }
    };

    Lease Acquire() {
        std::lock_guard<std::mutex> lock(mutex_);
        if (!open_)
            return Lease(nullptr);
        ++active_;
        return Lease(this);
    }

    void CloseAndDrain() {
        std::unique_lock<std::mutex> lock(mutex_);
        open_ = false;
        drained_.wait(lock, [this] { return active_ == 0; });
    }

    // Only the serialized lifecycle owner may reopen, after a new SDK handle
    // is fully initialized. Failed initialization leaves admission closed.
    void Open() {
        std::lock_guard<std::mutex> lock(mutex_);
        open_ = true;
    }

private:
    void Release() {
        std::lock_guard<std::mutex> lock(mutex_);
        if (--active_ == 0)
            drained_.notify_all();
    }
    mutable std::mutex mutex_;
    std::condition_variable drained_;
    std::size_t active_ = 0;
    bool open_ = false;
};
