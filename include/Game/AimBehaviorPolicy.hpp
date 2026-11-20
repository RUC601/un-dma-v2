#pragma once

#include <algorithm>

namespace OW {

enum class AimBehaviorType : int {
    Tracking = 0,
    Flick = 1,
    Flick2nd = 2,
    Reacquire = 3,
    MagneticTrigger = 4,
    FlickClamp = Flick,
    FlickDelay = Flick,
    ReacquireAtApex = Reacquire,
};

enum class FirePolicyType : int {
    ManualOnly = 0,
    TrackingWithHold = 1,
    SingleShot = 2,
    ReleaseAfterDelay = 3,
    TimedBurst = 4,
    ChargeRelease = 5,
    KeepFiring = 6,
    HoldWhileTracking = TrackingWithHold,
    TapOnHitWindow = SingleShot,
};

inline AimBehaviorType ClampAimBehavior(int value) {
    return static_cast<AimBehaviorType>(std::clamp(value, 0, 4));
}

inline FirePolicyType DefaultFirePolicyForBehavior(AimBehaviorType behavior) {
    return behavior == AimBehaviorType::Tracking
        ? FirePolicyType::TrackingWithHold
        : FirePolicyType::SingleShot;
}

} // namespace OW
