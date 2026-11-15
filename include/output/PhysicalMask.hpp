#pragma once
#include "Actions.hpp"
#include <functional>
#include <map>
#include <mutex>
namespace OW {
// 每代次、每业务 owner 的物理屏蔽租约；协调器统一合并、释放与失败重试。
    struct PhysicalMouseMaskLeaseOwner {
        OutputOwnerSource source = OutputOwnerSource::GlobalAim;
        std::string key;

        bool operator<(const PhysicalMouseMaskLeaseOwner& other) const noexcept {
            if (source != other.source) {
                return static_cast<std::uint8_t>(source) <
                    static_cast<std::uint8_t>(other.source);
            }
            return key < other.key;
        }
    };

    class PhysicalMouseMaskLeaseCoordinator {
    public:
        using MaskDispatch =
            std::function<int(std::uint64_t generation, std::uint32_t mask)>;
        using UnmaskDispatch =
            std::function<int(std::uint64_t generation)>;
        using RuntimeSource = std::function<OutputRuntimeState()>;

        PhysicalMouseMaskLeaseCoordinator(
            MaskDispatch maskDispatch,
            UnmaskDispatch unmaskDispatch,
            RuntimeSource runtimeSource)
            : maskDispatch_(std::move(maskDispatch)),
              unmaskDispatch_(std::move(unmaskDispatch)),
              runtimeSource_(std::move(runtimeSource))
        {
        }

        bool Acquire(
            std::string ownerKey,
            OutputOwnerSource source,
            std::uint32_t mask)
        {
            if (!runtimeSource_)
                return false;
            const std::uint64_t expectedGeneration =
                runtimeSource_().backendGeneration;
            return Acquire(
                std::move(ownerKey),
                source,
                mask,
                expectedGeneration);
        }

        bool Acquire(
            std::string ownerKey,
            OutputOwnerSource source,
            std::uint32_t mask,
            std::uint64_t expectedGeneration)
        {
            const std::uint32_t effectiveMask = mask & 0x7Fu;
            if (ownerKey.empty() || effectiveMask == 0 ||
                expectedGeneration == 0 ||
                !maskDispatch_ || !unmaskDispatch_ || !runtimeSource_) {
                return false;
            }

            std::unique_lock<std::mutex> operationLock(operationMutex_);
            if (runtimeSource_().backendGeneration != expectedGeneration)
                return false;
            if (!RetryPendingWhileOperationLocked())
                return false;

            const OutputRuntimeState runtime = runtimeSource_();
            if (runtime.backendGeneration != expectedGeneration)
                return false;
            NormalizeGenerationWhileOperationLocked(runtime.backendGeneration);
            if (!runtime.outputGateOpen || runtime.backendGeneration == 0)
                return false;

            const PhysicalMouseMaskLeaseOwner owner{
                source, std::move(ownerKey) };
            std::optional<std::uint32_t> previousMask;
            {
                std::lock_guard<std::mutex> stateLock(stateMutex_);
                const auto existing = leases_.find(owner);
                if (existing != leases_.end()) {
                    if (existing->second == effectiveMask)
                        return true;
                    previousMask = existing->second;
                    existing->second = effectiveMask;
                } else {
                    leases_.emplace(owner, effectiveMask);
                }
            }

            if (ReconcileWhileOperationLocked(runtime.backendGeneration)) {
                return true;
            }

            {
                std::lock_guard<std::mutex> stateLock(stateMutex_);
                if (generation_ == runtime.backendGeneration) {
                    if (previousMask.has_value())
                        leases_[owner] = *previousMask;
                    else
                        leases_.erase(owner);
                    RefreshPendingLocked();
                }
            }
            return false;
        }

        bool Release(
            const std::string& ownerKey,
            OutputOwnerSource source)
        {
            if (!runtimeSource_)
                return false;
            const std::uint64_t expectedGeneration =
                runtimeSource_().backendGeneration;
            return Release(ownerKey, source, expectedGeneration);
        }

        bool Release(
            const std::string& ownerKey,
            OutputOwnerSource source,
            std::uint64_t expectedGeneration)
        {
            if (ownerKey.empty() || expectedGeneration == 0 || !runtimeSource_)
                return false;

            std::unique_lock<std::mutex> operationLock(operationMutex_);
            const OutputRuntimeState runtime = runtimeSource_();
            if (runtime.backendGeneration != expectedGeneration)
                return false;
            NormalizeGenerationWhileOperationLocked(runtime.backendGeneration);

            bool removed = false;
            {
                std::lock_guard<std::mutex> stateLock(stateMutex_);
                removed = leases_.erase({ source, ownerKey }) != 0;
                if (removed)
                    RefreshPendingLocked();
            }
            if (!removed)
                return RetryPendingWhileOperationLocked();
            return ReconcileWhileOperationLocked(runtime.backendGeneration);
        }

        bool RetryPending()
        {
            std::unique_lock<std::mutex> operationLock(operationMutex_);
            return RetryPendingWhileOperationLocked();
        }

        bool IsActive(
            const std::string& ownerKey,
            OutputOwnerSource source)
        {
            std::unique_lock<std::mutex> operationLock(operationMutex_);
            (void)RetryPendingWhileOperationLocked();
            const OutputRuntimeState runtime = runtimeSource_();
            NormalizeGenerationWhileOperationLocked(runtime.backendGeneration);
            std::lock_guard<std::mutex> stateLock(stateMutex_);
            return leases_.find({ source, ownerKey }) != leases_.end();
        }

        bool IsConfirmedActive(
            const std::string& ownerKey,
            OutputOwnerSource source)
        {
            std::unique_lock<std::mutex> operationLock(operationMutex_);
            (void)RetryPendingWhileOperationLocked();
            const OutputRuntimeState runtime = runtimeSource_();
            NormalizeGenerationWhileOperationLocked(runtime.backendGeneration);
            std::lock_guard<std::mutex> stateLock(stateMutex_);
            const auto lease = leases_.find({ source, ownerKey });
            return lease != leases_.end() && hardwareKnown_ &&
                (appliedMask_ & lease->second) == lease->second;
        }

        std::uint32_t AggregateMask()
        {
            std::unique_lock<std::mutex> operationLock(operationMutex_);
            (void)RetryPendingWhileOperationLocked();
            const OutputRuntimeState runtime = runtimeSource_();
            NormalizeGenerationWhileOperationLocked(runtime.backendGeneration);
            std::lock_guard<std::mutex> stateLock(stateMutex_);
            return DesiredAggregateLocked();
        }

        bool HasPending()
        {
            std::unique_lock<std::mutex> operationLock(operationMutex_);
            const OutputRuntimeState runtime = runtimeSource_();
            NormalizeGenerationWhileOperationLocked(runtime.backendGeneration);
            std::lock_guard<std::mutex> stateLock(stateMutex_);
            return pending_;
        }

    private:
        std::uint32_t DesiredAggregateLocked() const
        {
            std::uint32_t aggregate = 0;
            for (const auto& [owner, mask] : leases_) {
                (void)owner;
                aggregate |= mask;
            }
            return aggregate & 0x7Fu;
        }

        void RefreshPendingLocked()
        {
            pending_ = !hardwareKnown_ ||
                appliedMask_ != DesiredAggregateLocked();
        }

        void NormalizeGenerationWhileOperationLocked(
            std::uint64_t generation)
        {
            std::lock_guard<std::mutex> stateLock(stateMutex_);
            if (generation_ == generation)
                return;
            generation_ = generation;
            leases_.clear();
            appliedMask_ = 0;
            hardwareKnown_ = true;
            pending_ = false;
        }

        void MarkHardwareUnknown(std::uint64_t generation)
        {
            std::lock_guard<std::mutex> stateLock(stateMutex_);
            if (generation_ != generation)
                return;
            hardwareKnown_ = false;
            pending_ = true;
        }

        bool ReconcileWhileOperationLocked(std::uint64_t generation)
        {
            std::uint32_t desiredMask = 0;
            std::uint32_t appliedMask = 0;
            bool hardwareKnown = true;
            {
                std::lock_guard<std::mutex> stateLock(stateMutex_);
                if (generation_ != generation)
                    return false;
                desiredMask = DesiredAggregateLocked();
                appliedMask = appliedMask_;
                hardwareKnown = hardwareKnown_;
            }

            const bool needsBaseline = !hardwareKnown ||
                (appliedMask & ~desiredMask) != 0;
            if (needsBaseline) {
                if (unmaskDispatch_(generation) != success) {
                    MarkHardwareUnknown(generation);
                    return false;
                }
                {
                    std::lock_guard<std::mutex> stateLock(stateMutex_);
                    if (generation_ != generation)
                        return false;
                    appliedMask_ = 0;
                    hardwareKnown_ = true;
                    pending_ = desiredMask != 0;
                }
                appliedMask = 0;
            }

            if (desiredMask != appliedMask) {
                const OutputRuntimeState observed = runtimeSource_();
                if (observed != OutputRuntimeState{ generation, true }) {
                    std::lock_guard<std::mutex> stateLock(stateMutex_);
                    if (generation_ == generation)
                        pending_ = true;
                    return false;
                }
                if (maskDispatch_(generation, desiredMask) != success) {
                    MarkHardwareUnknown(generation);
                    return false;
                }
                {
                    std::lock_guard<std::mutex> stateLock(stateMutex_);
                    if (generation_ != generation)
                        return false;
                    appliedMask_ = desiredMask;
                    hardwareKnown_ = true;
                    pending_ = false;
                }
                return true;
            }

            {
                std::lock_guard<std::mutex> stateLock(stateMutex_);
                if (generation_ != generation)
                    return false;
                appliedMask_ = desiredMask;
                hardwareKnown_ = true;
                pending_ = false;
            }
            return true;
        }

        bool RetryPendingWhileOperationLocked()
        {
            const OutputRuntimeState runtime = runtimeSource_();
            NormalizeGenerationWhileOperationLocked(runtime.backendGeneration);
            bool pending = false;
            {
                std::lock_guard<std::mutex> stateLock(stateMutex_);
                pending = pending_;
            }
            if (!pending)
                return true;
            return ReconcileWhileOperationLocked(runtime.backendGeneration);
        }

        MaskDispatch maskDispatch_;
        UnmaskDispatch unmaskDispatch_;
        RuntimeSource runtimeSource_;
        std::mutex operationMutex_;
        std::mutex stateMutex_;
        std::map<PhysicalMouseMaskLeaseOwner, std::uint32_t> leases_;
        std::uint64_t generation_ = 0;
        std::uint32_t appliedMask_ = 0;
        bool hardwareKnown_ = true;
        bool pending_ = false;
    };

    PhysicalMouseMaskLeaseCoordinator& RuntimePhysicalMouseMaskLeases();

    bool AcquirePhysicalMouseMaskLease(
        std::string ownerKey,
        OutputOwnerSource source,
        std::uint32_t mask,
        std::uint64_t expectedGeneration);

    bool AcquirePhysicalMouseMaskLease(
        std::string ownerKey,
        OutputOwnerSource source,
        std::uint32_t mask);

    bool ReleasePhysicalMouseMaskLease(
        const std::string& ownerKey,
        OutputOwnerSource source,
        std::uint64_t expectedGeneration);

    bool ReleasePhysicalMouseMaskLease(
        const std::string& ownerKey,
        OutputOwnerSource source);

    bool RetryPendingPhysicalMouseMaskLeases();

    bool IsPhysicalMouseMaskLeaseActive(
        const std::string& ownerKey,
        OutputOwnerSource source);

    bool IsPhysicalMouseMaskLeaseConfirmed(
        const std::string& ownerKey,
        OutputOwnerSource source);

    std::uint32_t PhysicalMouseMaskAggregate();


}
