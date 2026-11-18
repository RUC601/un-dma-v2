#pragma once

#include <algorithm>

namespace OW {

enum class TargetVisibilityMode : int {
    VisibleOnly = 0,
    All = 1,
};

inline TargetVisibilityMode ClampTargetVisibilityMode(int value)
{
    return static_cast<TargetVisibilityMode>(std::clamp(value, 0, 1));
}

inline TargetVisibilityMode TargetVisibilityFromLegacyIgnoreInvisible(bool ignoreInvisible)
{
    return ignoreInvisible ? TargetVisibilityMode::VisibleOnly : TargetVisibilityMode::All;
}

inline bool LegacyIgnoreInvisible(TargetVisibilityMode mode)
{
    return ClampTargetVisibilityMode(static_cast<int>(mode)) == TargetVisibilityMode::VisibleOnly;
}

} // namespace OW
