#pragma once

namespace V2::Output
{
    bool EnsureTimerResolution();
    void ReleaseTimerResolution();
    bool IsTimerResolutionRaised();
}
