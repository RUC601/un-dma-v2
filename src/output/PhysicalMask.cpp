#include "PhysicalMask.hpp"
namespace OW {
    PhysicalMouseMaskLeaseCoordinator& RuntimePhysicalMouseMaskLeases() {
        static PhysicalMouseMaskLeaseCoordinator coordinator(
            [](std::uint64_t generation, std::uint32_t mask) {
                return V2::Output::DispatchMouseMaskAndWaitForGeneration(
                    generation,
                    mask);
            },
            [](std::uint64_t generation) {
                return V2::Output::DispatchMouseUnmaskAndWaitForGeneration(
                    generation);
            },
            []() { return CurrentOutputRuntimeState(); });
        return coordinator;
    }

    bool AcquirePhysicalMouseMaskLease(
        std::string ownerKey,
        OutputOwnerSource source,
        std::uint32_t mask,
        std::uint64_t expectedGeneration) {
        const V2::Output::OutputRuntimeAppliedState applied =
            V2::Output::ActiveRuntimeSnapshot();
        if (expectedGeneration == 0 ||
            applied.generation != expectedGeneration ||
            !V2::Output::SupportsMouseMask(applied.descriptor)) {
            return false;
        }
        return RuntimePhysicalMouseMaskLeases().Acquire(
            std::move(ownerKey),
            source,
            mask,
            expectedGeneration);
    }

    bool AcquirePhysicalMouseMaskLease(
        std::string ownerKey,
        OutputOwnerSource source,
        std::uint32_t mask) {
        const std::uint64_t expectedGeneration =
            V2::Output::ActiveRuntimeSnapshot().generation;
        return AcquirePhysicalMouseMaskLease(
            std::move(ownerKey),
            source,
            mask,
            expectedGeneration);
    }

    bool ReleasePhysicalMouseMaskLease(
        const std::string& ownerKey,
        OutputOwnerSource source,
        std::uint64_t expectedGeneration) {
        return RuntimePhysicalMouseMaskLeases().Release(
            ownerKey,
            source,
            expectedGeneration);
    }

    bool ReleasePhysicalMouseMaskLease(
        const std::string& ownerKey,
        OutputOwnerSource source) {
        const std::uint64_t expectedGeneration =
            V2::Output::ActiveRuntimeSnapshot().generation;
        return ReleasePhysicalMouseMaskLease(
            ownerKey,
            source,
            expectedGeneration);
    }

    bool RetryPendingPhysicalMouseMaskLeases() {
        return RuntimePhysicalMouseMaskLeases().RetryPending();
    }

    bool IsPhysicalMouseMaskLeaseActive(
        const std::string& ownerKey,
        OutputOwnerSource source) {
        return RuntimePhysicalMouseMaskLeases().IsActive(ownerKey, source);
    }

    bool IsPhysicalMouseMaskLeaseConfirmed(
        const std::string& ownerKey,
        OutputOwnerSource source) {
        return RuntimePhysicalMouseMaskLeases().IsConfirmedActive(
            ownerKey,
            source);
    }

    std::uint32_t PhysicalMouseMaskAggregate() {
        return RuntimePhysicalMouseMaskLeases().AggregateMask();
    }
}
