#pragma once

#include "Game/Entity.hpp"
#include "Game/Overwatch.hpp"
#include "Utils/Types.hpp"
#include <imgui.h>

namespace OverlayRenderDetail {

// Draws the projected union of native sphere/capsule collision shapes. The
// caller owns the frame snapshot; this module only performs screen projection
// and emits overlay primitives.
bool DrawNativeHitboxContour(const OW::c_entity& entity,
                             const OW::Matrix& view,
                             ImU32 outline,
                             const ImVec4& fill,
                             float thickness,
                             Vector2 offset,
                             bool drawContour,
                             bool drawFill);

}
