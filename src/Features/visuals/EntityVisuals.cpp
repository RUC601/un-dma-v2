#include "EntityVisuals.hpp"
#include "Game/Overwatch.hpp"
#include "Game/HeroSettings.hpp"
#include "Game/TrainingBotDisplay.hpp"
#include "Game/ModelWireMesh.hpp"
#include "ShapeWire.hpp"
#include "Features/visuals/HitboxContour.hpp"
#include "Game/snapshots/Interpolation.hpp"
#include <cfloat>
namespace OverlayRenderDetail {

    inline ImU32 ToImU32(const ImVec4& color) {
        return ImGui::ColorConvertFloat4ToU32(color);
    }

    struct RenderPredictionInfo {
        bool trainingBot = false;
        bool shortExtrapolationCandidate = false;
        bool velocityReasonable = false;
        bool applied = false;
        bool usedWorldDeltaFallback = false;
        int leadMs = 0;
        int offsetCm = 0;
    };

    inline bool ApplyShortRenderExtrapolation(
        OW::c_entity& entity,
        DWORD now,
        RenderPredictionInfo* predictionInfo = nullptr) {
        const auto presentation = PresentationTiming::Evaluate(entity.previous_render_sample_tick_ms,
            entity.render_sample_tick_ms, now, entity.has_previous_render_sample);
        if (presentation.leadMs == 0)
            return false;
        if (predictionInfo)
            predictionInfo->shortExtrapolationCandidate = true;

        const OW::Motion::EntityMotionEstimate motion = OW::Motion::EstimateEntityMotion(entity);
        if (!motion.valid)
            return false;

        float speed = 0.0f;
        if (!IsReasonableRenderVelocity(motion.effectiveVelocity, &speed))
            return false;

        constexpr float kMaxOffsetMeters = 1.35f;
        float leadSeconds = static_cast<float>(presentation.leadMs) / 1000.0f;
        Vector3 offset = motion.effectiveVelocity * leadSeconds;
        float offsetSize = offset.Size();
        if (!std::isfinite(offsetSize) || offsetSize < 0.005f)
            return false;
        if (offsetSize > kMaxOffsetMeters) {
            const float scale = kMaxOffsetMeters / offsetSize;
            offset = offset * scale;
            offsetSize = kMaxOffsetMeters;
            leadSeconds = (std::min)(leadSeconds, offsetSize / speed);
        }

        OffsetRenderPoint(entity.pos, offset);
        OffsetRenderPoint(entity.head_pos, offset);
        OffsetRenderPoint(entity.neck_pos, offset);
        OffsetRenderPoint(entity.chest_pos, offset);
        if (entity.cached_bot_chest_bone_valid)
            OffsetRenderPoint(entity.cached_bot_chest_bone, offset);

        for (size_t i = 0; i < entity.skeleton_bones.size(); ++i) {
            if (entity.skeleton_bone_valid[i])
                OffsetRenderPoint(entity.skeleton_bones[i], offset);
        }

        if (predictionInfo) {
            predictionInfo->trainingBot = OW::GameData::IsTrainingBotHeroId(entity.HeroID);
            predictionInfo->velocityReasonable = true;
            predictionInfo->applied = true;
            predictionInfo->usedWorldDeltaFallback = motion.usedWorldDeltaFallback;
            predictionInfo->leadMs = static_cast<int>(std::lround(leadSeconds * 1000.0f));
            predictionInfo->offsetCm = static_cast<int>(std::lround(offsetSize * 100.0f));
        }
        return true;
    }

    inline bool ApplyPresentRenderMicroExtrapolation(
        OW::c_entity& entity,
        uint64_t nowUs,
        RenderPredictionInfo* predictionInfo = nullptr) {
        const DWORD maxLeadMs = OW::PresentRenderMicroExtrapolationMs();
        if (maxLeadMs == 0 ||
            entity.present_tick_us == 0 ||
            nowUs <= entity.present_tick_us ||
            entity.present_source == OW::PresentSnapshotSource::Hold ||
            entity.present_confidence < 0.75f) {
            return false;
        }

        const uint64_t ageUs = nowUs - entity.present_tick_us;
        const uint64_t maxLeadUs = static_cast<uint64_t>(maxLeadMs) * 1000ULL;
        if (ageUs == 0 || ageUs > maxLeadUs)
            return false;
        if (predictionInfo)
            predictionInfo->shortExtrapolationCandidate = true;

        const OW::Motion::EntityMotionEstimate motion = OW::Motion::EstimateEntityMotion(entity);
        if (!motion.valid)
            return false;

        float speed = 0.0f;
        if (!IsReasonableRenderVelocity(motion.effectiveVelocity, &speed))
            return false;

        float leadSeconds = static_cast<float>(ageUs) / 1000000.0f;
        OW::Vector3 offset = motion.effectiveVelocity * leadSeconds;
        float offsetSize = offset.Size();
        if (!std::isfinite(offsetSize) || offsetSize < 0.002f)
            return false;

        constexpr float kMaxPresentRenderOffsetMeters = 0.20f;
        if (offsetSize > kMaxPresentRenderOffsetMeters) {
            const float scale = kMaxPresentRenderOffsetMeters / offsetSize;
            offset = offset * scale;
            offsetSize = kMaxPresentRenderOffsetMeters;
            leadSeconds = (std::min)(leadSeconds, offsetSize / speed);
        }

        OffsetRenderPoint(entity.pos, offset);
        OffsetRenderPoint(entity.head_pos, offset);
        OffsetRenderPoint(entity.neck_pos, offset);
        OffsetRenderPoint(entity.chest_pos, offset);
        if (entity.cached_bot_chest_bone_valid)
            OffsetRenderPoint(entity.cached_bot_chest_bone, offset);

        for (size_t i = 0; i < entity.skeleton_bones.size(); ++i) {
            if (entity.skeleton_bone_valid[i])
                OffsetRenderPoint(entity.skeleton_bones[i], offset);
        }

        if (predictionInfo) {
            predictionInfo->velocityReasonable = true;
            predictionInfo->applied = true;
            predictionInfo->usedWorldDeltaFallback =
                predictionInfo->usedWorldDeltaFallback || motion.usedWorldDeltaFallback;
            const int totalLeadMs = static_cast<int>(std::lround(
                entity.present_prediction_ms + leadSeconds * 1000.0f));
            predictionInfo->leadMs = (std::max)(predictionInfo->leadMs, totalLeadMs);
            predictionInfo->offsetCm = (std::max)(
                predictionInfo->offsetCm,
                static_cast<int>(std::lround(offsetSize * 100.0f)));
        }
        return true;
    }

    inline RenderPredictionInfo ApplyTrainingBotRenderPrediction(OW::c_entity& entity, DWORD now) {
        RenderPredictionInfo info{};
        if (!OW::GameData::IsTrainingBotHeroId(entity.HeroID))
            return info;
        info.trainingBot = true;

        if (entity.position_sample_tick_ms == 0 || now <= entity.position_sample_tick_ms)
            return info;

        // Render-only extrapolation snaps back when a fresh bot position sample resets the age.
        constexpr bool kTrainingBotRenderPredictionEnabled = false;
        if (!kTrainingBotRenderPredictionEnabled)
            return info;

        float speed = 0.0f;
        if (!IsReasonableRenderVelocity(entity.velocity, &speed))
            return info;
        info.velocityReasonable = true;

        const DWORD ageMs = now - entity.position_sample_tick_ms;
        if (ageMs < 20)
            return info;

        constexpr float kMaxTrainingBotRenderLeadSeconds = 0.22f;
        const float leadSeconds = (std::min)(
            static_cast<float>(ageMs) / 1000.0f,
            kMaxTrainingBotRenderLeadSeconds);
        info.applied = true;
        info.leadMs = static_cast<int>(std::lround(leadSeconds * 1000.0f));
        const Vector3 offset = entity.velocity * leadSeconds;
        info.offsetCm = static_cast<int>(std::lround(offset.Size() * 100.0f));

        OffsetRenderPoint(entity.pos, offset);
        OffsetRenderPoint(entity.head_pos, offset);
        OffsetRenderPoint(entity.neck_pos, offset);
        OffsetRenderPoint(entity.chest_pos, offset);
        if (entity.cached_bot_chest_bone_valid)
            OffsetRenderPoint(entity.cached_bot_chest_bone, offset);

        for (size_t i = 0; i < entity.skeleton_bones.size(); ++i) {
            if (entity.skeleton_bone_valid[i])
                OffsetRenderPoint(entity.skeleton_bones[i], offset);
        }

        return info;
    }

    inline OW::c_entity InterpolateEntityForRender(
        const OW::c_entity& source,
        DWORD now,
        RenderPredictionInfo* predictionInfo = nullptr) {
        if (predictionInfo)
            *predictionInfo = RenderPredictionInfo{};

        const float alpha = EntityInterpolationAlpha(source, now);
        // Interpolation and extrapolation share now - delay. Adding a second
        // prediction to an interpolated sample makes each new observation rewind.
        if (alpha < 1.0f)
            return InterpolateEntitySamples(source, alpha);
        OW::c_entity entity = source;
        ApplyShortRenderExtrapolation(entity, now, predictionInfo);
        return entity;
    }

    inline int ToByte(float value) {
        return static_cast<int>(Clamp01(value) * 255.0f + 0.5f);
    }

    inline Render::Color ToRenderColor(const ImVec4& color) {
        return Render::Color(ToByte(color.x), ToByte(color.y), ToByte(color.z), ToByte(color.w));
    }

    inline float VisibilityAlpha(const OW::c_entity& entity, float opacity) {
        (void)entity;
        return Clamp01(opacity);
    }

    inline ImVec4 ApplyVisualState(ImVec4 color, const OW::c_entity& entity, float opacity) {
        const float brightness = entity.Vis ? 1.18f : 0.90f;
        color.x = Clamp01(color.x * brightness);
        color.y = Clamp01(color.y * brightness);
        color.z = Clamp01(color.z * brightness);
        color.w = Clamp01(color.w * VisibilityAlpha(entity, opacity));
        return color;
    }

    inline bool IsValidScreenPoint(const Vector2& point) {
        return point.X > 0.0f && point.Y > 0.0f &&
               point.X < OW::ScreenWidth() && point.Y < OW::ScreenHeight() &&
               std::isfinite(point.X) && std::isfinite(point.Y);
    }

    inline bool IsReasonableProjectedPoint(const Vector2& point) {
        return std::isfinite(point.X) && std::isfinite(point.Y) &&
               point.X > -OW::ScreenWidth() && point.X < OW::ScreenWidth() * 2.0f &&
               point.Y > -OW::ScreenHeight() && point.Y < OW::ScreenHeight() * 2.0f;
    }

    struct ProjectedEntityBounds {
        float left = 0.0f;
        float top = 0.0f;
        float bottom = 0.0f;
        float width = 0.0f;
        float height = 0.0f;
        float centerX = 0.0f;
    };

    struct ProjectedBoundsState {
        bool valid = false;
        float centerX = 0.0f;
        float centerY = 0.0f;
        float height = 0.0f;
        float width = 0.0f;
        DWORD tick = 0;
    };

    struct ProjectionJumpTrack {
        bool valid = false;
        Vector3 world{};
        float centerX = 0.0f;
        float centerY = 0.0f;
        DWORD tick = 0;
    };

    struct TrainingBotPredictionTrack {
        bool valid = false;
        int leadMs = 0;
        int offsetCm = 0;
        DWORD tick = 0;
    };

    inline bool TryProjectPoint(const Vector3& world, Vector2& screen, const OW::Matrix* vm = nullptr) {
        if (!IsFiniteVector(world) || !IsNonZeroVector(world))
            return false;
        OW::Matrix snapshot{}, camera{};
        if (!vm && !OW::TryGetViewMatricesSnapshot(snapshot, camera))
            return false;
        const OW::Matrix& proj = vm ? *vm : snapshot;
        if (!proj.WorldToScreen(world, &screen, OW::ScreenSizeSnapshot()))
            return false;
        return IsReasonableProjectedPoint(screen);
    }

    inline bool TryBuildHeadOffsetBounds(const OW::c_entity& entity, ProjectedEntityBounds& bounds, const OW::Matrix* vm = nullptr) {
        Vector2 low{}, high{};
        const Vector3 head = entity.head_pos;
        if (!TryProjectPoint(Vector3(head.X, head.Y - 1.5f, head.Z), low, vm))
            return false;
        if (!TryProjectPoint(Vector3(head.X, head.Y + 1.0f, head.Z), high, vm))
            return false;

        const float height = std::fabs(low.Y - high.Y);
        const float width = height * 0.85f;
        if (height <= 2.0f || width <= 2.0f || !std::isfinite(height) || !std::isfinite(width))
            return false;

        bounds.top = (low.Y < high.Y) ? low.Y : high.Y;
        bounds.bottom = (low.Y > high.Y) ? low.Y : high.Y;
        bounds.centerX = (low.X + high.X) * 0.5f;
        bounds.height = height;
        bounds.width = width;
        bounds.left = bounds.centerX - bounds.width * 0.5f;
        return true;
    }

    inline bool TryBuildProjectedBounds(const OW::c_entity& entity, ProjectedEntityBounds& bounds, const OW::Matrix* vm = nullptr) {
        float minX = 0.0f;
        float maxX = 0.0f;
        float minY = 0.0f;
        float maxY = 0.0f;
        int projectedCount = 0;

        auto includeScreenPoint = [&](const Vector2& screen) {
            if (projectedCount == 0) {
                minX = maxX = screen.X;
                minY = maxY = screen.Y;
            } else {
                minX = (screen.X < minX) ? screen.X : minX;
                maxX = (screen.X > maxX) ? screen.X : maxX;
                minY = (screen.Y < minY) ? screen.Y : minY;
                maxY = (screen.Y > maxY) ? screen.Y : maxY;
            }
            ++projectedCount;
        };

        auto includeWorldPoint = [&](const Vector3& world) {
            Vector2 screen{};
            if (TryProjectPoint(world, screen, vm))
                includeScreenPoint(screen);
        };

        for (size_t i = 0; i < entity.skeleton_bones.size(); ++i) {
            if (entity.skeleton_bone_valid[i])
                includeWorldPoint(entity.skeleton_bones[i]);
        }
        includeWorldPoint(entity.head_pos);
        includeWorldPoint(entity.neck_pos);
        includeWorldPoint(entity.chest_pos);
        includeWorldPoint(entity.pos);
        if (entity.cached_bot_chest_bone_valid)
            includeWorldPoint(entity.cached_bot_chest_bone);

        if (projectedCount < 3)
            return TryBuildHeadOffsetBounds(entity, bounds, vm);

        float height = maxY - minY;
        if (height <= 8.0f || !std::isfinite(height))
            return TryBuildHeadOffsetBounds(entity, bounds, vm);

        Vector2 headScreen{};
        Vector2 posScreen{};
        const bool hasHead = TryProjectPoint(entity.head_pos, headScreen, vm);
        const bool hasPos = TryProjectPoint(entity.pos, posScreen, vm);
        float centerX = (minX + maxX) * 0.5f;
        if (hasHead && hasPos)
            centerX = (headScreen.X + posScreen.X) * 0.5f;
        else if (hasHead)
            centerX = headScreen.X;

        const float verticalMarginTop = height * 0.08f;
        const float verticalMarginBottom = height * 0.05f;
        const float top = minY - verticalMarginTop;
        const float bottom = maxY + verticalMarginBottom;
        height = bottom - top;

        const float bodyWidth = (maxX - minX) * 1.20f;
        const float minimumBodyWidth = height * 0.55f;
        float width = (bodyWidth > minimumBodyWidth) ? bodyWidth : minimumBodyWidth;
        width = std::clamp(width, height * 0.45f, height * 0.95f);

        if (height <= 2.0f || width <= 2.0f ||
            !std::isfinite(top) || !std::isfinite(bottom) ||
            !std::isfinite(centerX) || !std::isfinite(width)) {
            return false;
        }

        bounds.top = top;
        bounds.bottom = bottom;
        bounds.height = height;
        bounds.width = width;
        bounds.centerX = centerX;
        bounds.left = centerX - width * 0.5f;
        return true;
    }

    inline void StabilizeProjectedBounds(
        uint64_t address,
        ProjectedEntityBounds& bounds,
        DWORD now,
        std::unordered_map<uint64_t, ProjectedBoundsState>& states) {
        auto stateIt = states.find(address);
        if (stateIt != states.end() && stateIt->second.valid &&
            stateIt->second.height > 2.0f && now - stateIt->second.tick <= 250) {
            const DWORD elapsed = now - stateIt->second.tick;
            const float frameScale = std::clamp(static_cast<float>(elapsed) / 16.0f, 1.0f, 4.0f);
            const float maxScaleStep = 1.0f + 0.10f * frameScale;
            const float minHeight = stateIt->second.height / maxScaleStep;
            const float maxHeight = stateIt->second.height * maxScaleStep;
            const float targetHeight = std::clamp(bounds.height, minHeight, maxHeight);
            float centerX = bounds.centerX;
            float centerY = (bounds.top + bounds.bottom) * 0.5f;

            bounds.height = Lerp(stateIt->second.height, targetHeight, 0.65f);
            bounds.width = std::clamp(bounds.width, bounds.height * 0.45f, bounds.height * 0.95f);
            bounds.top = centerY - bounds.height * 0.5f;
            bounds.bottom = centerY + bounds.height * 0.5f;
            bounds.left = bounds.centerX - bounds.width * 0.5f;
        }

        ProjectedBoundsState next{};
        next.valid = true;
        next.centerX = bounds.centerX;
        next.centerY = (bounds.top + bounds.bottom) * 0.5f;
        next.height = bounds.height;
        next.width = bounds.width;
        next.tick = now;
        states.insert_or_assign(address, next);
    }

    inline void PruneProjectedBoundsStates(
        std::unordered_map<uint64_t, ProjectedBoundsState>& states,
        DWORD now) {
        for (auto it = states.begin(); it != states.end();) {
            if (now - it->second.tick > 1000)
                it = states.erase(it);
            else
                ++it;
        }
    }

    inline void PruneProjectionJumpTracks(
        std::unordered_map<uint64_t, ProjectionJumpTrack>& states,
        DWORD now) {
        for (auto it = states.begin(); it != states.end();) {
            if (now - it->second.tick > 1000)
                it = states.erase(it);
            else
                ++it;
        }
    }

    inline void PruneTrainingBotPredictionTracks(
        std::unordered_map<uint64_t, TrainingBotPredictionTrack>& states,
        DWORD now) {
        for (auto it = states.begin(); it != states.end();) {
            if (now - it->second.tick > 1000)
                it = states.erase(it);
            else
                ++it;
        }
    }

    inline bool IsSpecialEntity(const OW::c_entity& entity) {
        return entity.HeroID == 0x16dd || entity.HeroID == 0x16ee || entity.HeroID == 0x16bb;
    }

    inline uint64_t EntitySelectionKey(const OW::c_entity& entity) {
        return OW::TargetingDetail::StableTargetKey(entity);
    }

    inline bool IsSelectedEnemyTarget(const OW::c_entity& entity,
                                      size_t index,
                                      uint64_t selectedTargetKey,
                                      int selectedTargetIndex) {
        if (!entity.Team)
            return false;

        const uint64_t entityKey = EntitySelectionKey(entity);
        if (selectedTargetKey != 0 && entityKey != 0)
            return entityKey == selectedTargetKey;

        return selectedTargetIndex >= 0 &&
               index == static_cast<size_t>(selectedTargetIndex);
    }

    inline ImVec4 EntityBaseColor(const OW::c_entity& entity,
                                  size_t index,
                                  uint64_t selectedTargetKey,
                                  int selectedTargetIndex) {
        if (IsSelectedEnemyTarget(entity, index, selectedTargetKey, selectedTargetIndex)) {
            return OW::Config::targetargb;
        }
        if (entity.Team && !entity.Vis)
            return OW::Config::invisnenargb;
        return entity.Team ? OW::Config::enargb : OW::Config::allyargb;
    }

    inline ImVec4 HealthGradientColor(const OW::c_entity& entity) {
        float ratio = 0.0f;
        if (entity.PlayerHealthMax > 0.0f)
            ratio = Clamp01(entity.PlayerHealth / entity.PlayerHealthMax);

        if (ratio >= 0.5f) {
            const float t = (ratio - 0.5f) * 2.0f;
            return ImVec4(Lerp(1.0f, 0.0f, t), 1.0f, 0.0f, 1.0f);
        }

        const float t = ratio * 2.0f;
        return ImVec4(1.0f, Lerp(0.0f, 1.0f, t), 0.0f, 1.0f);
    }

    inline bool ShouldRenderAtDistance(float distance) {
        if (OW::Config::visualMaxDist <= 0.0f)
            return true;
        return distance <= OW::Config::visualMaxDist;
    }

    inline float DistanceOpacity(float distance) {
        (void)distance;
        // Keep visualMaxDist as a render-distance gate only; ESP alpha is not distance-faded.
        return 1.0f;
    }

    inline ImU32 EntityColor(const OW::c_entity& entity,
                             size_t index,
                             uint64_t selectedTargetKey,
                             int selectedTargetIndex,
                             float opacity = 1.0f) {
        return ToImU32(ApplyVisualState(
            EntityBaseColor(entity, index, selectedTargetKey, selectedTargetIndex),
            entity,
            opacity));
    }

    inline ImU32 EntityBoxColor(const OW::c_entity& entity,
                                size_t index,
                                uint64_t selectedTargetKey,
                                int selectedTargetIndex,
                                float opacity) {
        if (IsSelectedEnemyTarget(entity, index, selectedTargetKey, selectedTargetIndex))
            return EntityColor(entity, index, selectedTargetKey, selectedTargetIndex, opacity);
        if (OW::Config::drawhealth)
            return ToImU32(ApplyVisualState(HealthGradientColor(entity), entity, opacity));
        return EntityColor(entity, index, selectedTargetKey, selectedTargetIndex, opacity);
    }

    inline ImU32 BoxOutlineColor(float opacity) {
        ImVec4 color = OW::Config::EnemyCol;
        color.w = Clamp01(color.w * opacity);
        return ToImU32(color);
    }

    inline Render::Color EntityRenderColor(const OW::c_entity& entity,
                                           size_t index,
                                           uint64_t selectedTargetKey,
                                           int selectedTargetIndex,
                                           float opacity = 1.0f) {
        return ToRenderColor(ApplyVisualState(
            EntityBaseColor(entity, index, selectedTargetKey, selectedTargetIndex),
            entity,
            opacity));
    }

    inline void DrawCenteredText(const ImVec2& center, ImU32 color, const std::string& text, float fontSize) {
        if (text.empty()) return;
        const ImVec2 size = ImGui::GetFont()->CalcTextSizeA(fontSize, FLT_MAX, 0.0f, text.c_str());
        Render::DrawStrokeText(ImVec2(center.x - size.x * 0.5f, center.y), color, text.c_str(), fontSize);
    }

    inline void DrawCenteredCanvasText(const ImVec2& canvasCenter, ImU32 color,
                                       const std::string& text, float canvasFontSize) {
        if (text.empty()) return;
        const ImVec2 size = ImGui::GetFont()->CalcTextSizeA(
            canvasFontSize, FLT_MAX, 0.0f, text.c_str());
        Render::DrawCanvasText(
            ImVec2(canvasCenter.x - size.x * 0.5f, canvasCenter.y),
            color,
            text.c_str(),
            canvasFontSize);
    }

    inline ImColor ImColorWithAlpha(int r, int g, int b, float opacity) {
        return ImColor(r, g, b, ToByte(opacity));
    }

    inline ImU32 ImU32WithAlpha(int r, int g, int b, float opacity) {
        return IM_COL32(r, g, b, ToByte(opacity));
    }

    inline float PositiveFinite(float value) {
        return (std::isfinite(value) && value > 0.0f) ? value : 0.0f;
    }

    inline void DrawStackedResourceBar(const OW::c_entity& entity, float x, float top, float height, float opacity) {
        const float baseHealth = PositiveFinite(entity.MinHealth);
        const float maxHealth = PositiveFinite(entity.MaxHealth);
        const float armor = PositiveFinite(entity.MinArmorHealth);
        const float maxArmor = PositiveFinite(entity.MaxArmorHealth);
        const float barrier = PositiveFinite(entity.MinBarrierHealth);
        const float maxBarrier = PositiveFinite(entity.MaxBarrierHealth);
        const float totalMax = maxHealth + maxArmor + maxBarrier;

        if (totalMax <= 0.0f || height <= 2.0f || opacity <= 0.0f)
            return;

        constexpr float barWidth = 4.0f;
        Render::DrawFilledRect(Vector2(x, top), barWidth, height, ImColorWithAlpha(0, 0, 0, opacity * 0.55f));

        float y = top + height;
        auto drawSegment = [&](float current, float maximum, const ImColor& color) {
            if (current <= 0.0f || maximum <= 0.0f || y <= top)
                return;

            const float clampedValue = (current < maximum) ? current : maximum;
            float segmentHeight = height * Clamp01(clampedValue / totalMax);
            if (segmentHeight <= 0.0f)
                return;
            const float remainingHeight = y - top;
            segmentHeight = (segmentHeight < remainingHeight) ? segmentHeight : remainingHeight;

            y -= segmentHeight;
            Render::DrawFilledRect(Vector2(x, y), barWidth, segmentHeight, color);
            if (y > top + 1.0f)
                Render::DrawFilledRect(Vector2(x, y), barWidth, 1.0f, ImColorWithAlpha(0, 0, 0, opacity * 0.7f));
        };

        drawSegment(baseHealth, maxHealth, ImColorWithAlpha(245, 245, 245, opacity));
        drawSegment(armor, maxArmor, ImColorWithAlpha(255, 150, 35, opacity));
        drawSegment(barrier, maxBarrier, ImColorWithAlpha(70, 170, 255, opacity));

        if (height >= 34.0f) {
            const int rawTicks = static_cast<int>(totalMax / 25.0f);
            const int maxTicks = (rawTicks < 48) ? rawTicks : 48;
            for (int tick = 1; tick < maxTicks; ++tick) {
                const float tickY = top + height - height * ((tick * 25.0f) / totalMax);
                if (tickY > top + 1.0f && tickY < top + height - 1.0f)
                    Render::DrawFilledRect(Vector2(x, tickY), barWidth, 1.0f, ImColorWithAlpha(0, 0, 0, opacity * 0.45f));
            }
        }
    }

    inline ID3D11ShaderResourceView* FindHeroIcon(const std::string& heroName) {
        IconManager* icons = Render::GetIconManager();
        if (!icons || heroName.empty() || heroName == "Unknown" || heroName == "Bot")
            return nullptr;

        const std::string slugKey = OW::HeroDisplayNameToSlug(heroName);
        return icons->LoadHeroAvatar(slugKey, IconManager::AvatarUse::Overlay);
    }

    inline std::string EntityHeroName(const OW::c_entity& entity) {
        const auto bot = OW::ResolveTrainingBotDisplay(entity.HeroID, entity.PlayerHealthMax);
        // D.Va's legacy name helper probes the process; rendering consumes identity only.
        if (entity.HeroID == OW::eHero::HERO_DVA) return OW::GameData::HeroName(entity.HeroID);
        return bot.name ? bot.name : OW::GetHeroEngNames(entity.HeroID, entity.LinkBase);
    }

    inline ID3D11ShaderResourceView* FindEntityHeroIcon(const OW::c_entity& entity) {
        const auto bot = OW::ResolveTrainingBotDisplay(entity.HeroID, entity.PlayerHealthMax);
        if (!bot.avatarSlug)
            return FindHeroIcon(EntityHeroName(entity));
        IconManager* icons = Render::GetIconManager();
        return icons ? icons->LoadHeroAvatar(bot.avatarSlug, IconManager::AvatarUse::Overlay) : nullptr;
    }

    inline void DrawUltimateReadyIndicator(const Vector2& center, float opacity) {
        if (opacity <= 0.0f)
            return;

        Render::DrawFilledCircle(center, 13.0f, Render::Color(255, 190, 25, ToByte(opacity * 0.22f)), 40);
        Render::DrawFilledCircle(center, 8.0f, Render::Color(255, 225, 70, ToByte(opacity * 0.48f)), 32);
        Render::DrawFilledCircle(center, 3.4f, Render::Color(255, 255, 235, ToByte(opacity)), 20);

        const Render::Color starColor(255, 245, 120, ToByte(opacity));
        Render::DrawLine(Vector2(center.X - 10.0f, center.Y), Vector2(center.X + 10.0f, center.Y), starColor, 1.3f);
        Render::DrawLine(Vector2(center.X, center.Y - 10.0f), Vector2(center.X, center.Y + 10.0f), starColor, 1.3f);
        Render::DrawLine(Vector2(center.X - 6.0f, center.Y - 6.0f), Vector2(center.X + 6.0f, center.Y + 6.0f), starColor, 1.0f);
        Render::DrawLine(Vector2(center.X - 6.0f, center.Y + 6.0f), Vector2(center.X + 6.0f, center.Y - 6.0f), starColor, 1.0f);
    }

    inline void DrawUltimateStatus(const OW::c_entity& entity, const Vector2& indicatorCenter,
                                   float left, float bottom, float opacity) {
        if (!std::isfinite(entity.ultimate) || entity.ultimate < 90.0f)
            return;

        const float ultimate = (entity.ultimate < 100.0f) ? entity.ultimate : 100.0f;
        if (ultimate >= 100.0f)
            DrawUltimateReadyIndicator(indicatorCenter, opacity);

        const std::string text = "ULT: " + std::to_string(static_cast<int>(ultimate + 0.5f)) + "%";
        Render::DrawStrokeText(ImVec2(left - 36.0f, bottom + 5.0f),
                               ImU32WithAlpha(255, 225, 60, opacity), text.c_str(), 12.0f);
    }

    inline bool IsSkillOnCooldown(bool active, float cooldown) {
        return !active && std::isfinite(cooldown) && cooldown > 0.05f;
    }

    inline std::string FormatCooldownLabel(const char* label, float cooldown) {
        char buffer[24] = {};
        std::snprintf(buffer, sizeof(buffer), "%s: %.1fs", label, cooldown);
        return buffer;
    }

    inline std::string CooldownSummary(const OW::c_entity& entity) {
        std::string text;
        for (size_t slot=0; slot<entity.display_cooldowns.size(); ++slot) {
            const auto& cd=entity.display_cooldowns[slot];
            if (!cd.Fresh(GetTickCount()) || cd.Remaining() <= .05f) continue;
            if (!text.empty()) text += " ";
            text += FormatCooldownLabel(slot==0 ? "S1" : "S2", cd.Remaining());
        }
        return text;
    }
    inline void DrawSkillTextCooldowns(const OW::c_entity& entity, float x, float y, float opacity) {
        const auto text = CooldownSummary(entity);
        if (!text.empty()) Render::DrawStrokeText(ImVec2(x,y), ImU32WithAlpha(255,230,120,opacity),text.c_str(),12);
    }

    inline std::string FormatIconCooldown(float cooldown) {
        char buffer[16] = {};
        if (cooldown >= 10.0f)
            std::snprintf(buffer, sizeof(buffer), "%.0f", cooldown);
        else
            std::snprintf(buffer, sizeof(buffer), "%.1f", cooldown);
        return buffer;
    }

    inline std::string FormatUltimatePercent(float ultimate) {
        if (!std::isfinite(ultimate))
            return "--%";

        char buffer[16] = {};
        std::snprintf(buffer, sizeof(buffer), "%d%%",
                      static_cast<int>(std::clamp(ultimate, 0.0f, 100.0f) + 0.5f));
        return buffer;
    }

    inline void AdvanceIconSlot(float& cursorX, float iconSize, float gap) {
        cursorX += iconSize + gap;
    }

    inline void DrawFallbackIconText(float slotX, float y, float iconSize, float opacity,
                                     const std::string& text, ImU32 color, float fontSize) {
        const float alpha = std::clamp(opacity, 0.0f, 1.0f);
        Render::DrawFilledRect(Vector2(slotX, y), iconSize, iconSize,
                               ImColorWithAlpha(0, 0, 0, alpha * 0.28f));
        Render::DrawRect(Vector2(slotX, y), iconSize, iconSize,
                         Render::Color(255, 255, 255, ToByte(alpha * 0.24f)), 1.0f);
        ImVec2 textSize = ImGui::CalcTextSize(text.c_str());
        const float baseFontSize = ImGui::GetFontSize();
        if (baseFontSize > 0.0f) {
            const float textScale = fontSize / baseFontSize;
            textSize.x *= textScale;
            textSize.y *= textScale;
        }
        Render::DrawText(ImVec2(slotX + (iconSize - textSize.x) * 0.5f,
                                y + (iconSize - textSize.y) * 0.5f),
                         color, text.c_str(), fontSize);
    }

    inline void DrawSkillFallbackSlot(float slotX, float y, float iconSize, float opacity,
                                      const char* label, bool active, float cooldown) {
        const bool onCooldown = IsSkillOnCooldown(active, cooldown);
        const std::string text = onCooldown ? FormatIconCooldown(cooldown) : std::string(label);
        const ImU32 color = onCooldown
            ? ImU32WithAlpha(255, 230, 120, opacity)
            : ImU32WithAlpha(235, 235, 235, opacity);
        DrawFallbackIconText(slotX, y, iconSize, opacity, text, color, (std::max)(8.0f, iconSize * 0.5f));
    }

    inline void DrawUltimateFallbackSlot(float slotX, float y, float iconSize,
                                         float opacity, float ultimate) {
        DrawFallbackIconText(slotX, y, iconSize, opacity, FormatUltimatePercent(ultimate),
                             ImU32WithAlpha(255, 225, 60, opacity), 8.0f);
    }

    inline void DrawIconSlotText(float slotX, float y, float iconSize, const std::string& text,
                                 ImU32 color, float fontSize) {
        if (text.empty())
            return;

        ImVec2 textSize = ImGui::CalcTextSize(text.c_str());
        const float baseFontSize = ImGui::GetFontSize();
        if (baseFontSize > 0.0f) {
            const float textScale = fontSize / baseFontSize;
            textSize.x *= textScale;
            textSize.y *= textScale;
        }

        Render::DrawText(ImVec2(slotX + (iconSize - textSize.x) * 0.5f,
                                y + (iconSize - textSize.y) * 0.5f),
                         color, text.c_str(), fontSize);
    }

    inline void EnsureAbilityIconsLoaded(IconManager* iconManager, const OW::HeroAbilityIcons& icons) {
        if (!iconManager)
            return;
        const std::string prefix = std::string(icons.heroSlug) + "/";
        const auto loaded = [&](const char* key) {
            return !key || !key[0] || iconManager->GetIcon(prefix + key);
        };
        if (loaded(icons.ability1Icon) && loaded(icons.ability2Icon) && loaded(icons.ultimateIcon) && loaded(icons.ability3Icon))
            return;
        std::vector<std::string> names{icons.ability1Icon, icons.ability2Icon, icons.ultimateIcon};
        if (icons.ability3Icon && icons.ability3Icon[0]) names.emplace_back(icons.ability3Icon);
        iconManager->LoadAbilityIcons(icons.heroSlug, names);
    }

    inline bool DrawAbilityIconSlot(IconManager* iconManager, const OW::HeroAbilityIcons& icons,
                                    const char* abilityIcon, float& cursorX, float y,
                                    float iconSize, float gap, float opacity,
                                    bool onCooldown, float cooldown, const char* missingLabel = nullptr, ImU32 tint=IM_COL32_WHITE) {
        ID3D11ShaderResourceView* texture = nullptr;
        if (iconManager && abilityIcon && abilityIcon[0])
            texture = iconManager->GetIcon(std::string(icons.heroSlug) + "/" + abilityIcon);
        if (!texture) {
            if (missingLabel)
                DrawSkillFallbackSlot(cursorX, y, iconSize, opacity, missingLabel, !onCooldown, cooldown);
            AdvanceIconSlot(cursorX, iconSize, gap);
            return missingLabel != nullptr;
        }

        const float alpha = std::clamp(opacity, 0.0f, 1.0f);
        const float imagePadding = std::clamp(iconSize * 0.09f, 1.0f, 2.0f);
        const float imageSize = (std::max)(1.0f, iconSize - imagePadding * 2.0f);
        Render::DrawIcon(texture,
                         ImVec2(cursorX + imagePadding, y + imagePadding),
                         ImVec2(imageSize, imageSize),
                         (tint & ~IM_COL32_A_MASK) | (static_cast<ImU32>(((tint>>IM_COL32_A_SHIFT)&255)*alpha)<<IM_COL32_A_SHIFT));

        if (onCooldown) {
            Render::DrawFilledRect(Vector2(cursorX, y), iconSize, iconSize,
                                   ImColorWithAlpha(0, 0, 0, alpha * 0.58f));
            const std::string text = FormatIconCooldown(cooldown);
            DrawIconSlotText(cursorX, y, iconSize, text,
                             tint==IM_COL32_WHITE?ImU32WithAlpha(255,230,120,alpha):tint, iconSize*.5f);
        }

        AdvanceIconSlot(cursorX, iconSize, gap);
        return true;
    }

    inline bool DrawUltimateIconSlot(IconManager* iconManager, const OW::HeroAbilityIcons& icons,
                                     float& cursorX, float y, float iconSize, float gap,
                                     float opacity, float ultimate) {
        bool drew = DrawAbilityIconSlot(iconManager, icons, icons.ultimateIcon, cursorX, y,
                                        iconSize, gap, opacity, false, 0.0f);
        if (!drew)
            return false;

        if (std::isfinite(ultimate) && ultimate < 100.0f) {
            const float iconX = cursorX - iconSize - gap;
            const float alpha = std::clamp(opacity, 0.0f, 1.0f);
            Render::DrawFilledRect(Vector2(iconX, y), iconSize, iconSize,
                                   ImColorWithAlpha(0, 0, 0, alpha * 0.42f));

            const std::string text = FormatUltimatePercent(ultimate);
            DrawIconSlotText(iconX, y, iconSize, text,
                             ImU32WithAlpha(255, 225, 60, alpha), 8.0f);
        }
        return true;
    }

    inline void DrawSkillCooldowns(const OW::c_entity& entity, const std::string& heroName,
                                   float x, float y, float opacity, const V2::VisualSettings::Style* style=nullptr, ImU32 tint=IM_COL32_WHITE, float uiScale = 1.0f) {
        const OW::HeroAbilityIcons* icons = OW::GetAbilityIcons(heroName);
        IconManager* iconManager = Render::GetIconManager();
        if (icons && iconManager) EnsureAbilityIconsLoaded(iconManager, *icons);
        const float size = (style ? style->size : 18.0f) * uiScale;
        const float gap = 3.0f * uiScale;
        for (size_t slot = 0; slot < entity.display_cooldowns.size(); ++slot) {
            const auto& sample = entity.display_cooldowns[slot];
            const bool known = sample.Fresh(GetTickCount());
            const float remaining = sample.Remaining();
            const float sx = x + float(slot) * (size + gap);
            float cursor = sx;
            if (icons && iconManager)
                DrawAbilityIconSlot(iconManager, *icons, slot == 0 ? icons->ability1Icon : icons->ability2Icon,
                    cursor, y, size, gap, opacity * (known ? 1.0f : .25f),
                    known && remaining > .05f, remaining, slot == 0 ? "S1" : "S2", tint);
            if (!known || !icons || !iconManager) {
                const std::string text = known ? FormatIconCooldown(remaining) : "--";
                Render::DrawStrokeText(ImVec2(sx, y), tint==IM_COL32_WHITE?ImU32WithAlpha(255,230,120,opacity):tint, text.c_str(), size*.67f);
            }
        }
    }

#ifdef UNLEASHED_ASSET_PREVIEW
    // Offline capture uses the same loading and cooldown draw calls as the overlay.
    void DrawAssetPreviewRow(const std::string& slug, float x, float y, float opacity) {
        IconManager* manager = Render::GetIconManager();
        const auto* icons = OW::GetAbilityIcons(slug);
        if (!manager || !icons) return;
        EnsureAbilityIconsLoaded(manager, *icons);
        if (auto* avatar = FindHeroIcon(slug)) {
            Render::DrawIcon(avatar, ImVec2(x, y), ImVec2(42, 42), ImU32WithAlpha(255,255,255,opacity));
            Render::DrawIcon(avatar, ImVec2(x+48, y), ImVec2(24, 24), ImU32WithAlpha(255,255,255,opacity));
        }
        float cursor = x+80;
        DrawAbilityIconSlot(manager, *icons, icons->ability1Icon, cursor, y, 24, 4, opacity, false, 0, "S1");
        DrawAbilityIconSlot(manager, *icons, icons->ability2Icon, cursor, y, 24, 4, opacity, true, 3.5f, "S2");
        DrawUltimateIconSlot(manager, *icons, cursor, y, 24, 4, opacity, 65.0f);
    }
#endif

    inline void DrawBoneSegment(const Vector2& from, const Vector2& to, const Render::Color& color, float thickness) {
        if (IsValidScreenPoint(from) && IsValidScreenPoint(to))
            Render::DrawLine(from, to, color, thickness);
    }

    inline void DrawSkeleton(const OW::c_entity& entity, const OW::Matrix& view, const Render::Color& color, float thickness, Vector2 offset={}) {
        Vector2 points[18]{};
        bool projected[18]{};
        const Vector2 windowSize(OW::ScreenWidth(), OW::ScreenHeight());
        constexpr int kRenderSkeletonSlots =
            static_cast<int>(OW::Plexies20260609::kSkeletonSlotCount);

        for (int i = 0; i < kRenderSkeletonSlots; ++i) {
            Vector3 point{};
            if (!entity.TrySkeletonPoint(size_t(i), point)) continue;
            projected[i] = view.WorldToScreen(point, &points[i], windowSize);
            points[i]=points[i]+offset;
        }

        auto draw = [&](int from, int to) {
            if (from < 0 || from >= 18 || to < 0 || to >= 18) return;
            if (projected[from] && projected[to])
                DrawBoneSegment(points[from], points[to], color, thickness);
        };

        static constexpr std::pair<int, int> kBoneConnections[] = {
            {0, 1}, {1, 2}, {2, 3},
            {1, 4}, {4, 6}, {6, 12},
            {1, 5}, {5, 7}, {7, 13},
            {3, 8}, {8, 10}, {10, 14},
            {3, 9}, {9, 11}, {11, 15},
        };

        for (const auto& link : kBoneConnections)
            draw(link.first, link.second);
    }

    struct BoneHitboxEllipse {
        Vector2 center{};
        Vector2 radius{};
    };

    inline bool TryAccumulateProjectedRadius(const OW::Matrix& view,
                                             const Vector3& centerWorld,
                                             const Vector3& axis,
                                             float radiusWorld,
                                             const Vector2& centerScreen,
                                             float& radiusX,
                                             float& radiusY) {
        bool accepted = false;
        const Vector2 windowSize(OW::ScreenWidth(), OW::ScreenHeight());
        for (float sign : { -1.0f, 1.0f }) {
            Vector2 point{};
            const Vector3 worldPoint = centerWorld + axis * (radiusWorld * sign);
            if (!view.WorldToScreen(worldPoint, &point, windowSize) ||
                !IsReasonableProjectedPoint(point)) {
                continue;
            }
            radiusX = (std::max)(radiusX, std::fabs(point.X - centerScreen.X));
            radiusY = (std::max)(radiusY, std::fabs(point.Y - centerScreen.Y));
            accepted = true;
        }
        return accepted;
    }

    inline bool TryBuildBoneHitboxEllipse(const OW::c_entity& entity,
                                          const OW::Matrix& view,
                                          size_t renderSlot,
                                          const Vector3& bonePoint,
                                          BoneHitboxEllipse& ellipse) {
        const int hitboxBoneId = OW::Plexies20260609::HitboxBoneIdForRenderSlot(renderSlot);
        if (hitboxBoneId < 0)
            return false;

        const float radiusWorld = OW::ResolveEffectiveHitWindow(
            entity.HeroID,
            hitboxBoneId,
            nullptr,
            OW::Config::View_hitbox(),
            OW::Config::kLegacyDefaultHitboxRadius);
        if (radiusWorld <= 0.0f || !std::isfinite(radiusWorld))
            return false;

        const Vector3 centerWorld = OW::ResolveBoneHitboxCenter(entity, hitboxBoneId, bonePoint);
        if (!IsFiniteVector(centerWorld) || !IsNonZeroVector(centerWorld))
            return false;

        Vector2 centerScreen{};
        if (!view.WorldToScreen(centerWorld, &centerScreen, OW::ScreenSizeSnapshot()) ||
            !IsReasonableProjectedPoint(centerScreen)) {
            return false;
        }

        float radiusX = 0.0f;
        float radiusY = 0.0f;
        bool accepted = false;
        accepted |= TryAccumulateProjectedRadius(
            view, centerWorld, Vector3(1.0f, 0.0f, 0.0f), radiusWorld, centerScreen, radiusX, radiusY);
        accepted |= TryAccumulateProjectedRadius(
            view, centerWorld, Vector3(0.0f, 1.0f, 0.0f), radiusWorld, centerScreen, radiusX, radiusY);
        accepted |= TryAccumulateProjectedRadius(
            view, centerWorld, Vector3(0.0f, 0.0f, 1.0f), radiusWorld, centerScreen, radiusX, radiusY);
        if (!accepted)
            return false;

        radiusX = std::clamp(radiusX, 1.5f, OW::ScreenWidth() * 0.25f);
        radiusY = std::clamp(radiusY, 1.5f, OW::ScreenHeight() * 0.25f);
        if (!std::isfinite(radiusX) || !std::isfinite(radiusY))
            return false;

        ellipse.center = centerScreen;
        ellipse.radius = Vector2(radiusX, radiusY);
        return true;
    }

    inline float BoneEllipseJointRadius(const BoneHitboxEllipse& ellipse) {
        return (std::max)(ellipse.radius.X, ellipse.radius.Y);
    }

    inline void DrawBoneSegmentCapsuleWireframe(const BoneHitboxEllipse& from,
                                                const BoneHitboxEllipse& to,
                                                const Render::Color& color,
                                                float thickness) {
        if (!IsValidScreenPoint(from.center) || !IsValidScreenPoint(to.center))
            return;

        const Vector2 delta = to.center - from.center;
        const float length = delta.Length();
        if (length <= 2.0f || !std::isfinite(length))
            return;

        const float maxCapsuleRadius = (std::max)(2.0f, length * 0.45f);
        const float fromRadius = std::clamp(BoneEllipseJointRadius(from), 1.5f, maxCapsuleRadius);
        const float toRadius = std::clamp(BoneEllipseJointRadius(to), 1.5f, maxCapsuleRadius);
        const Vector2 direction = delta / length;
        const Vector2 normal(-direction.Y, direction.X);
        const Vector2 fromSideA = from.center + normal * fromRadius;
        const Vector2 fromSideB = from.center - normal * fromRadius;
        const Vector2 toSideA = to.center + normal * toRadius;
        const Vector2 toSideB = to.center - normal * toRadius;

        const float railThickness = (std::max)(1.0f, thickness * 0.75f);
        Render::DrawLine(fromSideA, toSideA, color, railThickness);
        Render::DrawLine(fromSideB, toSideB, color, railThickness);
    }

    inline bool TryDrawBoneCapsuleWireframe(const BoneHitboxEllipse& from,
                                            const BoneHitboxEllipse& to,
                                            const Render::Color& color,
                                            float thickness,
                                            bool& usedFrom,
                                            bool& usedTo) {
        if (!IsValidScreenPoint(from.center) || !IsValidScreenPoint(to.center))
            return false;

        const Vector2 delta = to.center - from.center;
        const float length = delta.Length();
        if (length <= 2.0f || !std::isfinite(length))
            return false;

        DrawBoneSegmentCapsuleWireframe(from, to, color, thickness);
        usedFrom = true;
        usedTo = true;
        return true;
    }

    inline bool DrawHitboxes(const OW::c_entity& entity,
                             const OW::Matrix& view,
                             const V2::VisualSettings::Settings& display,
                             float opacity,
                             const ImVec4& contourColor,
                             const ImVec4& fillColor) {
        using OW::ModelShapes::CriticalState;
        const auto& wire = display.elements[int(V2::VisualSettings::Element::Hitbox)];
        const auto& fill = display.hitboxFillStyle;
        const auto& contour = display.hitboxContourStyle;
        const float uiScale = Render::SourceUiScale();
        const Vector2 offset{wire.offsetX * uiScale, wire.offsetY * uiScale};
        const float thickness = wire.thickness * uiScale;
        const Vector2 fillOffset{fill.offsetX * uiScale, fill.offsetY * uiScale};
        const Vector2 contourOffset{contour.offsetX * uiScale, contour.offsetY * uiScale};
        const bool classifiedFill = display.hitboxFill && display.hitboxFillByClass;
        const bool uniformFill = display.hitboxFill && !display.hitboxFillByClass;
        const bool drawWire = OW::Config::draw_hitbox;
        const auto project = [&](Vector3 point, Vector2& screen) {
            return view.WorldToScreen(point, &screen, OW::ScreenSizeSnapshot());
        };
        const auto drawLayers = [&](const auto& shapes, auto worldPoint, auto classify, auto valid,
                                    auto drawUnion, auto record) {
            bool drew = false;
            if (uniformFill)
                drew |= drawUnion(false, fillColor, fill.thickness * uiScale, fillOffset);
            auto drawClassLayer = [&](bool isFill) {
                const auto& palette = isFill ? display.hitboxFillColors : display.hitboxWireColors;
                for (size_t i = 0; i < shapes.size(); ++i) {
                    if (!valid(i)) continue;
                    const auto selected = V2::Visuals::ClassColor(palette, classify(i), opacity);
                    const auto world = [&](Vector3 point) { return worldPoint(i, point); };
                    const bool submitted = isFill
                        ? V2::Visuals::DrawShapeFill(shapes[i], world, project, selected, fillOffset)
                        : V2::Visuals::DrawShapeWire(shapes[i], world, project, selected, thickness, offset);
                    drew |= submitted;
                    record(i, isFill, submitted, selected);
                }
            };
            if (classifiedFill) drawClassLayer(true);
            if (drawWire) drawClassLayer(false);
            if (display.hitboxContour)
                drew |= drawUnion(true, contourColor, contour.thickness * uiScale, contourOffset);
            return drew;
        };
        if(OW::offset::UsesNativeModelGeometry()) {
            const auto& native=entity.native_hitboxes;
            if(!entity.HasCurrentModelWorldTransform() || !native.Matches(entity.VelocityBase,entity.skeleton_revision) ||
               GetTickCount()-native.tick>100)return false;
            const auto delta = entity.pos - native.world.sampleAnchor;
            const auto world = [&](size_t i, Vector3 point) {
                return native.world.TransformPoint(OW::ModelShapes::Transform(point, native.poses[i])) + delta;
            };
            const auto drawUnion = [&](bool outline, const ImVec4& fillValue, float width, Vector2 shift) {
                return DrawNativeHitboxContour(entity, view, ImGui::ColorConvertFloat4ToU32(fillValue),
                                               fillValue, width, shift, outline, !outline);
            };
            static std::unordered_map<uint64_t, DWORD> lastLog;
            const DWORD now = GetTickCount();
            auto& previous = lastLog[entity.address];
            const bool log = (drawWire || classifiedFill) && now - previous >= 1000;
            if (log) previous = now;
            const auto record = [&](size_t i, bool isFill, bool submitted, Render::Color selected) {
                if (!log) return;
                const auto& shape = native.shapes[i];
                const int mapped = i < native.shapeMapped.size() ? native.shapeMapped[i] : -1;
                Diagnostics::Info(
                    "[RAY-DRAW] entity=0x%llX model=0x%llX hitbox=0x%llX index=%zu "
                    "shapeMapped=%d bone=%d criticalRaw=0x%llX critical=%s "
                    "submitted=%d color=(%d,%d,%d,%d) pose_tick_ms=%u draw_tick_ms=%u "
                    "layer=%s fill_mode=%s frame=%d",
                    static_cast<unsigned long long>(entity.address),
                    static_cast<unsigned long long>(native.modelBase),
                    static_cast<unsigned long long>(native.hitboxSet), i, mapped, shape.boneId,
                    static_cast<unsigned long long>(shape.criticalRaw), OW::ModelShapes::CriticalStateName(shape.critical),
                    int(submitted), selected.R, selected.G, selected.B, selected.A, native.tick, now,
                    isFill ? "fill" : "wire", classifiedFill ? "class" : uniformFill ? "uniform" : "off",
                    ImGui::GetFrameCount());
            };
            return drawLayers(native.shapes, world, [&](size_t i) { return native.shapes[i].critical; },
                              [](size_t) { return true; }, drawUnion, record);
        }
        // Original model shapes use their own size; never apply the aim window scale.
        if(OW::ModelShapes::Supported(entity.HeroID) && entity.model_shapes.modelIndex>=0) {
            if (!entity.model_shapes.MatchesSkeleton(entity.skeleton_revision) ||
                !entity.model_shapes.tick || GetTickCount()-entity.model_shapes.tick>100) return false;
            const auto yaw=DirectX::XMMatrixRotationY(entity.Rot.X);
            const auto& model=OW::ModelShapes::kModels[entity.model_shapes.modelIndex];
            if(model.heroId!=entity.HeroID || entity.model_shapes.valid.size()!=model.shapes.size() ||
               entity.model_shapes.poses.size()!=model.shapes.size())return false;
            const auto world = [&](size_t i, const Vector3& point) {
                    const Vector3 local=OW::ModelShapes::Transform(point,entity.model_shapes.poses[i]);
                    DirectX::XMFLOAT3 value{local.X,local.Y,local.Z},out{};
                    DirectX::XMStoreFloat3(&out,DirectX::XMVector3Transform(DirectX::XMLoadFloat3(&value),yaw));
                    return Vector3(out.x,out.y,out.z)+entity.pos;
                };
            const auto classify = [&](size_t i) {
                return i < entity.model_shapes.critical.size()
                    ? entity.model_shapes.critical[i] : CriticalState::Unknown;
            };
            const auto drawUnion = [&](bool outline, const ImVec4& fillValue, float width, Vector2 shift) {
                return DrawModelHitboxContour(entity, view, ImGui::ColorConvertFloat4ToU32(fillValue),
                                              fillValue, width, shift, outline, !outline);
            };
            return drawLayers(model.shapes, world, classify,
                              [&](size_t i) { return bool(entity.model_shapes.valid[i]); },
                              drawUnion, [](size_t, bool, bool, Render::Color) {});
        }
        bool drewAny = false;
        const std::array<int, 18> renderBoneIds = entity.GetRenderSkel();
        BoneHitboxEllipse ellipses[18]{};
        bool ellipseReady[18]{};
        bool capsuleJointUsed[18]{};
        constexpr size_t kRenderableSlots = OW::Plexies20260609::kSkeletonSlotCount;

        for (size_t slotIndex = 0; slotIndex < kRenderableSlots; ++slotIndex) {
            if (slotIndex >= renderBoneIds.size() ||
                renderBoneIds[slotIndex] == OW::Plexies20260609::kUnusedRenderSkeletonBone) {
                continue;
            }

            Vector3 point{};
            if (!entity.TrySkeletonPoint(slotIndex, point)) continue;
            ellipseReady[slotIndex] = TryBuildBoneHitboxEllipse(
                entity, view, slotIndex, point, ellipses[slotIndex]);
        }

        const Render::Color hitboxColor = V2::Visuals::ClassColor(
            display.hitboxWireColors, CriticalState::Unknown, opacity);
        const float capsuleThickness=thickness;
        for(auto& ellipse:ellipses)ellipse.center=ellipse.center+offset;
        static constexpr std::pair<int, int> kBoneConnections[] = {
            {0, 1}, {1, 2}, {2, 3},
            {1, 4}, {4, 6}, {6, 12},
            {1, 5}, {5, 7}, {7, 13},
            {3, 8}, {8, 10}, {10, 14},
            {3, 9}, {9, 11}, {11, 15},
        };

        for (const auto& link : kBoneConnections) {
            const int from = link.first;
            const int to = link.second;
            if (ellipseReady[from] && ellipseReady[to]) {
                drewAny |= TryDrawBoneCapsuleWireframe(
                    ellipses[from],
                    ellipses[to],
                    hitboxColor,
                    capsuleThickness,
                    capsuleJointUsed[from],
                    capsuleJointUsed[to]);
            }
        }

        for (size_t slotIndex = 0; slotIndex < kRenderableSlots; ++slotIndex) {
            if (!capsuleJointUsed[slotIndex])
                continue;
            Render::DrawEllipse(
                ellipses[slotIndex].center,
                ellipses[slotIndex].radius,
                hitboxColor,
                0.0f,
                48,
                capsuleThickness);
            drewAny = true;
        }

        return drewAny;
    }

    inline void DrawVisibleEyeIndicator(const Vector2& center, const Render::Color& color, float uiScale = 1.0f) {
        const float s = uiScale;
        Render::DrawLine(Vector2(center.X - 6.0f * s, center.Y), Vector2(center.X - 3.0f * s, center.Y - 2.0f * s), color, 1.0f * s);
        Render::DrawLine(Vector2(center.X - 6.0f * s, center.Y), Vector2(center.X - 3.0f * s, center.Y + 2.0f * s), color, 1.0f * s);
        Render::DrawLine(Vector2(center.X + 6.0f * s, center.Y), Vector2(center.X + 3.0f * s, center.Y - 2.0f * s), color, 1.0f * s);
        Render::DrawLine(Vector2(center.X + 6.0f * s, center.Y), Vector2(center.X + 3.0f * s, center.Y + 2.0f * s), color, 1.0f * s);
        Render::DrawCircle(center, 3.2f * s, color, 16, 1.0f * s);
        Render::DrawFilledCircle(center, 1.2f * s, color, 12);
    }

    inline const char* YesNo(bool value) {
        return value ? "Y" : "N";
    }

    std::string CurrentLocalHeroName(uint64_t heroId) {
        if (heroId == 0)
            return "Waiting for local player";

        std::string heroName = OW::GameData::HeroName(heroId);
        if (heroName.empty() || heroName == "Unknown") {
            char fallback[64]{};
            std::snprintf(fallback, sizeof(fallback), "Hero 0x%llX",
                          static_cast<unsigned long long>(heroId));
            return fallback;
        }
        return heroName;
    }

    #include "SlotStatusHud.inl"

} // namespace OverlayRenderDetail

float DrawAimTriggerStatusPanel() {
    return OverlayRenderDetail::DrawAimTriggerStatusPanel();
}

void PlayerInfoFromSnapshot(const std::vector<OW::c_entity>& entity_snapshot,
                                   const OW::c_entity& local_snapshot,
                                   bool boxPerfMode) {
    const auto visualState=V2::HeroSettings::Snapshot();
    const auto& display=visualState->display;
    if(!display.enabled)return;
    using VisualElement=V2::VisualSettings::Element;
    const auto playerInfoStart = std::chrono::steady_clock::now();

    Diagnostics::PlayerInfoStats renderStats{};
    renderStats.boxPerfMode = boxPerfMode;
    renderStats.fastBoxPath = boxPerfMode && OW::Config::boxPerfFastRect;
    renderStats.input = entity_snapshot.size();
    static DWORD lastPlayerInfoLogTick = 0;
    static std::unordered_map<uint64_t,uint64_t> renderedActorGenerations;
    static std::unordered_map<uint64_t, OverlayRenderDetail::ProjectionJumpTrack> projectionJumpTracks;
    static std::unordered_map<uint64_t, OverlayRenderDetail::TrainingBotPredictionTrack> trainingBotPredictionTracks;
    static uint64_t renderedScene=0;
    const auto scene=OW::EntitySceneGeneration();
    if(renderedScene!=scene) {
        renderedActorGenerations.clear();projectionJumpTracks.clear();trainingBotPredictionTracks.clear();
        renderedScene=scene;
    }
    static size_t trainingBotPredictionLeadDropTotal = 0;
    static uint64_t trainingBotPredictionLastDropAddress = 0;
    static int trainingBotPredictionLastDropFromMs = 0;
    static int trainingBotPredictionLastDropToMs = 0;
    static int trainingBotPredictionLastDropOffsetCm = 0;
    const DWORD renderTick = GetTickCount();
    const uint64_t renderTickUs = PresentSteadyNowUs();
    const OW::TargetingDetail::TargetLockRuntime targetLock =
        OW::TargetingDetail::SnapshotTargetLockRuntime();
    const uint64_t selectedTargetKey = targetLock.active ? targetLock.entityKey : 0;
    const int selectedTargetIndex = OW::Config::Targetenemyi;
    std::vector<float> projectionJumpDx;
    std::vector<float> projectionJumpDy;
    std::vector<float> projectionJumpDelta;

    OW::Matrix renderViewMatrix{}, renderViewMatrixXor{};
    uint64_t renderCameraSequence=0;
    if (!OW::TryGetLiveViewMatricesSnapshot(renderViewMatrix, renderViewMatrixXor,&renderCameraSequence)) {
        RuntimeTrace::Emit(RuntimeTrace::RenderSkip, 1, entity_snapshot.size());
        projectionJumpTracks.clear();
        trainingBotPredictionTracks.clear();
        Diagnostics::SetPlayerInfoStats(renderStats);
        return;
    }
    Diagnostics::RecordRenderViewMatrixUse();
    RuntimeTrace::Emit(RuntimeTrace::RenderCameraUse,renderCameraSequence,renderTick,entity_snapshot.size());

    const auto camLoc = renderViewMatrixXor.get_location();
    const Vector3 cameraDistanceOrigin(camLoc.x, camLoc.y, camLoc.z);
    const bool cameraDistanceOriginValid =
        OverlayRenderDetail::IsFiniteVector(cameraDistanceOrigin) &&
        OverlayRenderDetail::IsNonZeroVector(cameraDistanceOrigin);
    const Vector3 localDistanceOrigin =
        (OverlayRenderDetail::IsFiniteVector(local_snapshot.pos) &&
         OverlayRenderDetail::IsNonZeroVector(local_snapshot.pos))
            ? local_snapshot.pos
            : local_snapshot.head_pos;
    const bool localDistanceOriginValid =
        local_snapshot.HasUsableBody(renderTick,scene) && OverlayRenderDetail::IsFiniteVector(localDistanceOrigin) &&
        OverlayRenderDetail::IsNonZeroVector(localDistanceOrigin);

    auto publishStats = [&]() {
        const auto playerInfoEnd = std::chrono::steady_clock::now();
        renderStats.elapsedMs = std::chrono::duration<double, std::milli>(
            playerInfoEnd - playerInfoStart).count();
        Diagnostics::SetPlayerInfoStats(renderStats);
        if (!OW::PipelineDebugEnabled())
            return;

        const DWORD now = GetTickCount();
        if (lastPlayerInfoLogTick == 0 || now - lastPlayerInfoLogTick >= 1000) {
            Diagnostics::Info("[PIPELINE] Stage 5 PlayerInfo hero=0x%llX target[index/key]=%d/0x%llX colorA[enemy/hidden/target]=%.2f/%.2f/%.2f input=%zu projected=%zu drawn=%zu skip[dead/localhp/self/dist/opacity/w2s/box/window]=%zu/%zu/%zu/%zu/%zu/%zu/%zu/%zu w2s[low/high]=%zu/%zu.",
                static_cast<unsigned long long>(local_snapshot.HeroID),
                selectedTargetIndex,
                static_cast<unsigned long long>(selectedTargetKey),
                OW::Config::enargb.w,
                OW::Config::invisnenargb.w,
                OW::Config::targetargb.w,
                renderStats.input,
                renderStats.projected,
                renderStats.drawn,
                renderStats.skippedDead,
                renderStats.skippedLocalHealth,
                renderStats.skippedLocalEntity,
                renderStats.skippedDistance,
                renderStats.skippedOpacity,
                renderStats.skippedWorldToScreen,
                renderStats.skippedBox,
                renderStats.skippedWindow,
                renderStats.skippedWorldToScreenLow,
                renderStats.skippedWorldToScreenHigh);
            lastPlayerInfoLogTick = now;
        }
    };

    if (entity_snapshot.empty()) {
        RuntimeTrace::Emit(RuntimeTrace::RenderSkip,4);
        projectionJumpTracks.clear();
        trainingBotPredictionTracks.clear();
        publishStats();
        return;
    }
    if (OW::ScreenWidth() <= 0.0f || OW::ScreenHeight() <= 0.0f) {
        RuntimeTrace::Emit(RuntimeTrace::RenderSkip,5,entity_snapshot.size());
        renderStats.skippedWindow = entity_snapshot.size();
        publishStats();
        return;
    }

    auto captureProjectedSample = [&](const OW::c_entity& entity, float left, float top,
        float width, float height, float centerX, float bottom, float distance) {
        if (renderStats.sampleProjected)
            return;
        renderStats.sampleProjected = true;
        renderStats.sampleProjectedAddress = entity.address;
        renderStats.sampleProjectedHeroId = entity.HeroID;
        renderStats.sampleProjectedLeft = static_cast<int>(std::lround(left));
        renderStats.sampleProjectedTop = static_cast<int>(std::lround(top));
        renderStats.sampleProjectedWidth = static_cast<int>(std::lround(width));
        renderStats.sampleProjectedHeight = static_cast<int>(std::lround(height));
        renderStats.sampleProjectedCenterX = static_cast<int>(std::lround(centerX));
        renderStats.sampleProjectedBottom = static_cast<int>(std::lround(bottom));
        renderStats.sampleProjectedDistanceM = static_cast<int>(std::lround(distance));
    };

    auto captureDrawnSample = [&](const OW::c_entity& entity, float left, float top,
        float width, float height, float centerX, float bottom, float distance) {
        if (renderStats.sampleDrawn)
            return;
        renderStats.sampleDrawn = true;
        renderStats.sampleDrawnAddress = entity.address;
        renderStats.sampleDrawnHeroId = entity.HeroID;
        renderStats.sampleDrawnLeft = static_cast<int>(std::lround(left));
        renderStats.sampleDrawnTop = static_cast<int>(std::lround(top));
        renderStats.sampleDrawnWidth = static_cast<int>(std::lround(width));
        renderStats.sampleDrawnHeight = static_cast<int>(std::lround(height));
        renderStats.sampleDrawnCenterX = static_cast<int>(std::lround(centerX));
        renderStats.sampleDrawnBottom = static_cast<int>(std::lround(bottom));
        renderStats.sampleDrawnDistanceM = static_cast<int>(std::lround(distance));
    };

    auto medianFloat = [](std::vector<float> values) -> float {
        if (values.empty())
            return 0.0f;
        const size_t mid = values.size() / 2;
        std::nth_element(values.begin(), values.begin() + mid, values.end());
        const float upper = values[mid];
        if ((values.size() & 1U) != 0)
            return upper;
        std::nth_element(values.begin(), values.begin() + mid - 1, values.end());
        return (values[mid - 1] + upper) * 0.5f;
    };

    auto trackProjectionJump = [&](const OW::c_entity& entity, float centerX, float centerY) {
        Vector3 world = entity.chest_pos;
        if (!OverlayRenderDetail::IsFiniteVector(world) || !OverlayRenderDetail::IsNonZeroVector(world))
            world = entity.pos;
        if (!OverlayRenderDetail::IsFiniteVector(world) || !OverlayRenderDetail::IsNonZeroVector(world))
            return;

        auto stateIt = projectionJumpTracks.find(entity.address);
        if (stateIt != projectionJumpTracks.end() && stateIt->second.valid &&
            renderTick - stateIt->second.tick <= 250) {
            const float dx = centerX - stateIt->second.centerX;
            const float dy = centerY - stateIt->second.centerY;
            const float screenDelta = std::sqrt(dx * dx + dy * dy);
            const float worldDelta = world.DistTo(stateIt->second.world);
            if (screenDelta >= 70.0f && worldDelta <= 0.75f) {
                const float jumpValues[]={dx,dy,screenDelta,worldDelta};
                RuntimeTrace::Emit(RuntimeTrace::ProjectionJump,entity.address,entity.actor_generation,
                    entity.publish_sequence,renderCameraSequence,jumpValues,4);
                projectionJumpDx.push_back(dx);
                projectionJumpDy.push_back(dy);
                projectionJumpDelta.push_back(screenDelta);
            }
        }

        OverlayRenderDetail::ProjectionJumpTrack next{};
        next.valid = true;
        next.world = world;
        next.centerX = centerX;
        next.centerY = centerY;
        next.tick = renderTick;
        projectionJumpTracks.insert_or_assign(entity.address, next);
    };

    auto trackTrainingBotPrediction = [&](uint64_t address,
                                          const OverlayRenderDetail::RenderPredictionInfo& info) {
        if (!info.trainingBot || address == 0)
            return;

        renderStats.trainingBotPredictionCandidates++;
        if (info.applied) {
            renderStats.trainingBotPredictionApplied++;
            renderStats.trainingBotPredictionMaxLeadMs =
                (std::max)(renderStats.trainingBotPredictionMaxLeadMs, info.leadMs);
            renderStats.trainingBotPredictionMaxOffsetCm =
                (std::max)(renderStats.trainingBotPredictionMaxOffsetCm, info.offsetCm);
        }

        auto previousIt = trainingBotPredictionTracks.find(address);
        if (previousIt != trainingBotPredictionTracks.end() &&
            previousIt->second.valid &&
            renderTick - previousIt->second.tick <= 500) {
            const OverlayRenderDetail::TrainingBotPredictionTrack& previous = previousIt->second;
            if (previous.leadMs >= 80 && info.leadMs <= 20 && previous.offsetCm >= 25) {
                trainingBotPredictionLeadDropTotal++;
                trainingBotPredictionLastDropAddress = address;
                trainingBotPredictionLastDropFromMs = previous.leadMs;
                trainingBotPredictionLastDropToMs = info.leadMs;
                trainingBotPredictionLastDropOffsetCm = previous.offsetCm;
            }
        }

        OverlayRenderDetail::TrainingBotPredictionTrack next{};
        next.valid = true;
        next.leadMs = info.leadMs;
        next.offsetCm = info.offsetCm;
        next.tick = renderTick;
        trainingBotPredictionTracks.insert_or_assign(address, next);
    };

    auto trackRenderPrediction = [&](uint64_t address,
                                     const OverlayRenderDetail::RenderPredictionInfo& info) {
        if (address == 0)
            return;

        if (info.shortExtrapolationCandidate || info.trainingBot)
            renderStats.renderPredictionCandidates++;

        if (!info.applied)
            return;

        renderStats.renderPredictionApplied++;
        if (info.usedWorldDeltaFallback)
            renderStats.renderPredictionWorldDeltaFallback++;
        renderStats.renderPredictionMaxLeadMs =
            (std::max)(renderStats.renderPredictionMaxLeadMs, info.leadMs);
        renderStats.renderPredictionMaxOffsetCm =
            (std::max)(renderStats.renderPredictionMaxOffsetCm, info.offsetCm);
    };

    const bool usingPresentEntitySnapshot =
        OW::PresentUseForRenderEnabled() &&
        !entity_snapshot.empty() &&
        entity_snapshot.front().present_tick_ms != 0;
    const auto offscreenForward = renderViewMatrixXor.get_rotation();
    const float offscreenYaw = std::atan2(offscreenForward.x, offscreenForward.z);
    const float offscreenCosYaw = std::cos(offscreenYaw);
    const float offscreenSinYaw = std::sin(offscreenYaw);

    for (size_t index = 0; index < entity_snapshot.size(); ++index) {
        OverlayRenderDetail::RenderPredictionInfo predictionInfo{};
        OW::c_entity presentedEntityStorage{};
        const OW::c_entity* entityPtr = &entity_snapshot[index];
        auto [generationIt,inserted]=renderedActorGenerations.try_emplace(entityPtr->address,entityPtr->actor_generation);
        if(!inserted && generationIt->second!=entityPtr->actor_generation) {
            projectionJumpTracks.erase(entityPtr->address);
            trainingBotPredictionTracks.erase(entityPtr->address);generationIt->second=entityPtr->actor_generation;
        }
        if(renderedActorGenerations.size()>1024)renderedActorGenerations.clear();
        if (usingPresentEntitySnapshot) {
            presentedEntityStorage = entity_snapshot[index];
            if (presentedEntityStorage.present_source == OW::PresentSnapshotSource::Extrap) {
                predictionInfo.shortExtrapolationCandidate = true;
                predictionInfo.applied = true;
                predictionInfo.leadMs = static_cast<int>(std::lround(presentedEntityStorage.present_prediction_ms));
            }
            OverlayRenderDetail::ApplyPresentRenderMicroExtrapolation(
                presentedEntityStorage,
                renderTickUs,
                &predictionInfo);
            entityPtr = &presentedEntityStorage;
        }
        const OW::c_entity& entity = *entityPtr;
        trackRenderPrediction(entity.address, predictionInfo);
        trackTrainingBotPrediction(entity.address, predictionInfo);
        if (!entity.team_known || !entity.HasUsableBody(renderTick,OW::EntitySceneGeneration()) ||
            !OW::PresentationTiming::Fresh(entity.anchor_sample_tick_ms, renderTick)) {
            RuntimeTrace::Emit(RuntimeTrace::RenderSkip, 2, entity.address,entity.anchor_sample_tick_ms,renderTick);
            renderStats.skippedDead++;
            continue;
        }
        // Other live bodies remain drawable during local death/spectating. Their own freshness
        // and confirmed team relation above are independent of the local body's health.
        if (entity.address == local_snapshot.address || (entity.participant_key &&
            entity.participant_key==local_snapshot.participant_key && entity.connection_epoch==local_snapshot.connection_epoch &&
            entity.scene_generation==local_snapshot.scene_generation)) {
            RuntimeTrace::Emit(RuntimeTrace::RenderSkip,6,entity.address);
            renderStats.skippedLocalEntity++;
            continue;
        }

        if((entity.Team&&!display.enemies)||(!entity.Team&&!display.allies)||(display.visibleOnly&&!entity.Vis))continue;
        Vector3 Vec3 = entity.head_pos;
        const float dist = cameraDistanceOriginValid
            ? cameraDistanceOrigin.DistTo(Vec3)
            : (localDistanceOriginValid ? localDistanceOrigin.DistTo(Vec3) : 0.0f);
        if (!OverlayRenderDetail::ShouldRenderAtDistance(dist)) {
            RuntimeTrace::Emit(RuntimeTrace::RenderSkip,7,entity.address);
            renderStats.skippedDistance++;
            continue;
        }

        const float distanceOpacity = OverlayRenderDetail::DistanceOpacity(dist);
        if (distanceOpacity <= 0.0f) {
            RuntimeTrace::Emit(RuntimeTrace::RenderSkip,8,entity.address);
            renderStats.skippedOpacity++;
            continue;
        }

        auto elementColor=[&](VisualElement element) {
            const auto& style=display.elements[int(element)];
            const bool selected=OverlayRenderDetail::IsSelectedEnemyTarget(entity,index,selectedTargetKey,selectedTargetIndex);
            return OverlayRenderDetail::ToImU32(OverlayRenderDetail::ApplyVisualState(V2::VisualSettings::Color(style,entity.Team,entity.Vis,selected),entity,distanceOpacity));
        };
        auto customElementColor=[&](const V2::VisualSettings::Style& style) {
            const bool selected=OverlayRenderDetail::IsSelectedEnemyTarget(entity,index,selectedTargetKey,selectedTargetIndex);
            return OverlayRenderDetail::ToImU32(OverlayRenderDetail::ApplyVisualState(V2::VisualSettings::Color(style,entity.Team,entity.Vis,selected),entity,distanceOpacity));
        };
        auto elementStyle=[&](VisualElement element)->const V2::VisualSettings::Style& {return display.elements[int(element)];};
        const float uiScale = Render::SourceUiScale();
        const float canvasScale = Render::CanvasUiScale();
        const ImVec2 canvasSize = Render::CanvasSize();
        const float canvasW = canvasSize.x > 0.0f ? canvasSize.x : OW::ScreenWidth();
        const float canvasH = canvasSize.y > 0.0f ? canvasSize.y : OW::ScreenHeight();
        auto arrow=[&] {
            if(!display.offscreen || !local_snapshot.HasUsableBody(renderTick,OW::EntitySceneGeneration()) || !OverlayRenderDetail::IsFiniteVector(local_snapshot.pos))return;
            const float dx=entity.pos.X-local_snapshot.pos.X,dz=entity.pos.Z-local_snapshot.pos.Z;
            float x=dx*offscreenCosYaw-dz*offscreenSinYaw,y=-(dx*offscreenSinYaw+dz*offscreenCosYaw);
            const float length=std::sqrt(x*x+y*y);if(length<.001f)return;x/=length;y/=length;
            const auto& style=elementStyle(VisualElement::Offscreen);
            const float cx=canvasW*.5f,cy=canvasH*.5f;
            const float margin = 30.0f * canvasScale;
            const float reach=(std::min)((std::max)(0.0f, cx - margin)/(std::max)(.001f,std::abs(x)),(std::max)(0.0f, cy - margin)/(std::max)(.001f,std::abs(y)));
            const ImVec2 tip(cx+x*reach+style.offsetX*canvasScale,cy+y*reach+style.offsetY*canvasScale);
            const float size=style.size*canvasScale;
            ImGui::GetForegroundDrawList()->AddTriangleFilled(tip,ImVec2(tip.x-x*size-y*size*.5f,tip.y-y*size+x*size*.5f),ImVec2(tip.x-x*size+y*size*.5f,tip.y-y*size-x*size*.5f),elementColor(VisualElement::Offscreen));
        };
        auto centerArrow=[&](float screenX,float screenY)->bool {
            if(!display.centerArrow || !std::isfinite(screenX) || !std::isfinite(screenY))return false;
            const auto& style=elementStyle(VisualElement::CenterArrow);
            const ImVec2 targetCanvas = Render::ToCanvas(ImVec2(screenX, screenY));
            const float centerX=canvasW*.5f+style.offsetX*canvasScale,centerY=canvasH*.5f+style.offsetY*canvasScale;
            float x=targetCanvas.x-centerX,y=targetCanvas.y-centerY;const float length=std::sqrt(x*x+y*y);if(length<1.f)return false;x/=length;y/=length;
            const float size=style.size*canvasScale;
            const float maxRadius=(std::min)(canvasW,canvasH)*.5f-(std::max)(8.f*canvasScale,size);
            const float radius=(std::max)(0.f,(std::min)(style.radius*canvasScale,maxRadius));
            const ImVec2 tip(centerX+x*radius,centerY+y*radius);const ImVec2 back(tip.x-x*size,tip.y-y*size);const ImVec2 side(-y*size*.5f,x*size*.5f);
            ImGui::GetForegroundDrawList()->AddTriangleFilled(tip,ImVec2(back.x+side.x,back.y+side.y),ImVec2(back.x-side.x,back.y-side.y),elementColor(VisualElement::CenterArrow));
            return true;
        };
        OverlayRenderDetail::ProjectedEntityBounds bounds{};
        if (!OverlayRenderDetail::TryBuildProjectedBounds(entity, bounds, &renderViewMatrix)) {
            arrow();
            RuntimeTrace::Emit(RuntimeTrace::RenderSkip,9,entity.address,renderCameraSequence);
            renderStats.skippedWorldToScreen++;
            continue;
        }

        const float traceBounds[] = {bounds.centerX,bounds.top,bounds.bottom,bounds.width,bounds.height};
        RuntimeTrace::Emit(RuntimeTrace::Bounds,entity.address,entity.actor_generation,entity.publish_sequence,renderCameraSequence,traceBounds,5);
        float height = bounds.height;
        float width  = bounds.width;
        if (height <= 2.0f || width <= 2.0f || !std::isfinite(height) || !std::isfinite(width)) {
            RuntimeTrace::Emit(RuntimeTrace::RenderSkip,10,entity.address,renderCameraSequence);
            renderStats.skippedBox++;
            continue;
        }
        renderStats.projected++;

        if(bounds.centerX+bounds.width*.5f<0||bounds.centerX-bounds.width*.5f>OW::ScreenWidth()||bounds.bottom<0||bounds.top>OW::ScreenHeight()){arrow();continue;}
        float top = bounds.top;
        float bottom = bounds.bottom;
        float centerX = bounds.centerX;
        const float centerY = (top + bottom) * 0.5f;
        float left = bounds.left;
        const bool centerArrowDrawn=centerArrow(centerX,centerY);
        trackProjectionJump(entity, centerX, centerY);
        captureProjectedSample(entity, left, top, width, height, centerX, bottom, dist);

        const ImU32 boxColor=OW::Config::drawhealth?OverlayRenderDetail::EntityBoxColor(entity,index,selectedTargetKey,selectedTargetIndex,distanceOpacity):elementColor(VisualElement::Box);
        if (boxPerfMode) {
            const float perfThickness = (entity.Vis ? 1.5f : 1.0f) * uiScale;
            if (OW::Config::boxPerfFastRect) {
                Render::DrawFastRectBox(left, top, width, bottom - top, boxColor, perfThickness);
            } else {
                Render::DrawCorneredBox(left, top, width, bottom - top, boxColor, perfThickness, 0);
            }
            captureDrawnSample(entity, left, top, width, height, centerX, bottom, dist);
            renderStats.drawn++;
            continue;
        }

        ImU32 color = OverlayRenderDetail::EntityColor(
            entity, index, selectedTargetKey, selectedTargetIndex, distanceOpacity);
        ImU32 boxOutlineColor = OverlayRenderDetail::BoxOutlineColor(distanceOpacity);
        Render::Color lineColor = OverlayRenderDetail::EntityRenderColor(
            entity, index, selectedTargetKey, selectedTargetIndex, distanceOpacity);
        const float visualOpacity = OverlayRenderDetail::VisibilityAlpha(entity, distanceOpacity);
        const float outlineThickness = elementStyle(VisualElement::Box).thickness * uiScale;
        const float skeletonThickness = elementStyle(VisualElement::Skeleton).thickness * uiScale;
        const float fontScale = display.dynamicFont ? std::clamp(30.f/(std::max)(1.f,dist),.7f,1.6f) : 1.f;
        auto text=[&](VisualElement element,float x,float y,const std::string& label) {
            const auto& style=elementStyle(element);
            const ImVec2 canvasAnchor = Render::ToCanvas(ImVec2(x, y));
            const ImVec2 canvasCenter(
                canvasAnchor.x + style.offsetX * canvasScale,
                canvasAnchor.y + style.offsetY * canvasScale);
            OverlayRenderDetail::DrawCenteredCanvasText(
                canvasCenter,
                elementColor(element),
                label,
                style.size * fontScale * canvasScale);
        };
        const bool specialEntity = OverlayRenderDetail::IsSpecialEntity(entity);
        bool drewAny = centerArrowDrawn;

        std::string heroName;
        const bool showAboveHeadSkillCooldowns = OW::Config::skillinfo;
        if (OW::Config::skillinfo || OW::Config::healthbar2 || OW::Config::draw_avatar || OW::Config::name)
            heroName = OverlayRenderDetail::EntityHeroName(entity);

        ID3D11ShaderResourceView* heroIcon = nullptr;
        if (OW::Config::draw_avatar &&
            !specialEntity && heroName != "Unknown")
            heroIcon = OverlayRenderDetail::FindEntityHeroIcon(entity);

        const float iconSize = elementStyle(VisualElement::Avatar).size * uiScale;
        Vector2 ultimateIndicatorCenter(centerX, top - 18.0f * uiScale);
        if (heroIcon) {
            const float iconY = top - iconSize - 10.0f * uiScale;
            ultimateIndicatorCenter = Vector2(centerX, iconY + iconSize * 0.5f);
        }

        if (OW::Config::draw_info || OW::Config::draw_edge || OW::Config::drawbox3d) {
            const auto& style=elementStyle(VisualElement::Box);
            const float x=left+style.offsetX*uiScale,y=top+style.offsetY*uiScale;
            if(display.boxType==0)Render::DrawCorneredBox(x,y,width,bottom-top,boxColor,outlineThickness,boxOutlineColor);
            else {
                const ImU32 fillColor = display.boxType == 2
                    ? ((boxColor & ~IM_COL32_A_MASK) | (((((boxColor >> IM_COL32_A_SHIFT) & 255u) * 45u) / 255u) << IM_COL32_A_SHIFT))
                    : 0u;
                Render::DrawFastRectBox(x, y, width, bottom - top, boxColor, outlineThickness, fillColor);
            }
            drewAny = true;
        }

        float labelY = heroIcon
            ? (top - iconSize - 24.0f * uiScale)
            : (top - 18.0f * uiScale);
        if (OW::Config::name && !specialEntity && heroName != "Unknown" && heroName != "Bot" && !heroName.empty()) {
            const std::string label=display.nameType==1?(entity.battletag.empty()?"--":entity.battletag):display.nameType==2?heroName+" / "+(entity.battletag.empty()?"--":entity.battletag):heroName;
            text(VisualElement::Name,centerX,labelY,label);
            labelY += 13.0f * uiScale;
            drewAny = true;
        }
        if (OW::Config::drawbattletag && !entity.battletag.empty()) {
            text(VisualElement::BattleTag,centerX,labelY,entity.battletag);
            drewAny = true;
        }

        if (OW::Config::healthbar || OW::Config::healthbar2) {
            const auto& style=elementStyle(VisualElement::Health);
            const float ratio=entity.PlayerHealthMax>0?std::clamp(entity.PlayerHealth/entity.PlayerHealthMax,0.f,1.f):0;
            auto* draw=ImGui::GetForegroundDrawList();
            const float thickness = (std::max)(1.0f, style.thickness * canvasScale);
            const float pad = (std::max)(1.0f, 1.0f * canvasScale);
            if(display.healthBarStyle==1) {
                const ImVec2 boxTopLeft = Render::ToCanvas(Vector2(left, top));
                const ImVec2 boxBottomLeft = Render::ToCanvas(Vector2(left, bottom));
                const float barHeight = boxBottomLeft.y - boxTopLeft.y;
                const float x = boxTopLeft.x + (-5.0f + style.offsetX) * canvasScale;
                const float y = boxTopLeft.y + style.offsetY * canvasScale;
                draw->AddRectFilled(ImVec2(x-pad,y-pad),ImVec2(x+thickness+pad,y+barHeight+pad),IM_COL32(0,0,0,200));
                draw->AddRectFilled(ImVec2(x,y+barHeight*(1-ratio)),ImVec2(x+thickness,y+barHeight),elementColor(VisualElement::Health));
            } else {
                const ImVec2 boxTopLeft = Render::ToCanvas(Vector2(left, top));
                const ImVec2 boxTopRight = Render::ToCanvas(Vector2(left + width, top));
                const float canvasBoxW = boxTopRight.x - boxTopLeft.x;
                const float canvasCenterX = (boxTopLeft.x + boxTopRight.x) * 0.5f;
                const float barWidth = display.healthBarStyle==2 ? canvasBoxW : style.size * 5.0f * canvasScale;
                const float barHeight = (display.healthBarStyle==2 ? style.thickness : style.thickness * 3.0f) * canvasScale;
                const float x = canvasCenterX - barWidth * 0.5f + style.offsetX * canvasScale;
                const float y = boxTopLeft.y - barHeight - 4.0f * canvasScale + style.offsetY * canvasScale;
                draw->AddRectFilled(ImVec2(x-pad,y-pad),ImVec2(x+barWidth+pad,y+barHeight+pad),IM_COL32(0,0,0,200));
                if(display.healthBarStyle==2) {
                    draw->AddRectFilled(ImVec2(x,y),ImVec2(x+barWidth*ratio,y+barHeight),elementColor(VisualElement::Health));
                } else {
                    const float maximum=entity.MaxHealth+entity.MaxArmorHealth+entity.MaxBarrierHealth;
                    const float current[]={entity.MinHealth,entity.MinArmorHealth,entity.MinBarrierHealth};
                    const float maxima[]={entity.MaxHealth,entity.MaxArmorHealth,entity.MaxBarrierHealth};
                    const ImU32 colors[]={IM_COL32(245,245,245,255),IM_COL32(255,150,35,255),IM_COL32(70,170,255,255)};
                    float cursor=x;
                    if(maximum>0 && std::isfinite(maximum))for(int pool=0;pool<3;++pool)if(maxima[pool]>0) {
                        const float segment=barWidth*maxima[pool]/maximum,fill=segment*std::clamp(current[pool]/maxima[pool],0.f,1.f);
                        auto tint=ImGui::ColorConvertU32ToFloat4(colors[pool]);tint.w=visualOpacity;
                        draw->AddRectFilled(ImVec2(cursor,y),ImVec2(cursor+fill,y+barHeight),ImGui::ColorConvertFloat4ToU32(tint));
                        cursor+=segment;
                    }
                    if(maximum>0)for(int tick=1;tick<(std::min)(int(maximum/25),80);++tick) {
                        const float tickX=x+barWidth*(tick*25.f/maximum);
                        draw->AddLine(ImVec2(tickX,y),ImVec2(tickX,y+barHeight),IM_COL32(0,0,0,150), (std::max)(1.0f, 1.0f * canvasScale));
                    }
                }
            }
            drewAny=true;
        }

        if (OW::Config::drawline) {
            const auto& style = elementStyle(VisualElement::Snapline);
            Render::DrawLine(Vector2(OW::ScreenWidth()*.5f,OW::ScreenHeight()),Vector2(centerX+style.offsetX*uiScale,bottom+style.offsetY*uiScale),OverlayRenderDetail::ToRenderColor(ImGui::ColorConvertU32ToFloat4(elementColor(VisualElement::Snapline))),style.thickness*uiScale);
            drewAny = true;
        }

        if (OW::Config::draw_skel && !specialEntity) {
            const auto& style = elementStyle(VisualElement::Skeleton);
            OverlayRenderDetail::DrawSkeleton(entity,renderViewMatrix,OverlayRenderDetail::ToRenderColor(ImGui::ColorConvertU32ToFloat4(elementColor(VisualElement::Skeleton))),skeletonThickness,Vector2(style.offsetX*uiScale,style.offsetY*uiScale));
            drewAny = true;
        }

        if ((OW::Config::draw_hitbox || display.hitboxContour || display.hitboxFill) && !specialEntity)
            drewAny |= OverlayRenderDetail::DrawHitboxes(entity, renderViewMatrix, display, visualOpacity,
                ImGui::ColorConvertU32ToFloat4(customElementColor(display.hitboxContourStyle)),
                ImGui::ColorConvertU32ToFloat4(customElementColor(display.hitboxFillStyle)));

        if (entity.Vis) {
            OverlayRenderDetail::DrawVisibleEyeIndicator(
                Vector2(left + width + 8.0f * uiScale, top + 7.0f * uiScale), lineColor, uiScale);
            drewAny = true;
        }

        if (OW::Config::eyeray) {
            Vector2 eyeStart{}, eyeEnd{};
            Vector3 rayEnd(Vec3.X + sinf(entity.Rot.X) * 5.0f, Vec3.Y, Vec3.Z + cosf(entity.Rot.X) * 5.0f);
            if (renderViewMatrix.WorldToScreen(Vec3, &eyeStart, OW::ScreenSizeSnapshot()) &&
                renderViewMatrix.WorldToScreen(rayEnd, &eyeEnd, OW::ScreenSizeSnapshot())) {
                const auto& style = elementStyle(VisualElement::EyeRay);
                const Vector2 offset(style.offsetX*uiScale,style.offsetY*uiScale);
                Render::DrawLine(eyeStart+offset,eyeEnd+offset,OverlayRenderDetail::ToRenderColor(ImGui::ColorConvertU32ToFloat4(elementColor(VisualElement::EyeRay))),style.thickness*uiScale);
                drewAny = true;
            }
        }

        if (OW::Config::dist) {
            std::string distanceText = std::to_string(static_cast<int>(dist)) + "m";
            text(VisualElement::Distance,centerX,bottom+4.0f*uiScale,distanceText);
            drewAny = true;
        }

        if (heroIcon) {
            const auto& style = elementStyle(VisualElement::Avatar);
            Render::DrawIcon(heroIcon,
                             ImVec2(centerX-iconSize*.5f+style.offsetX*uiScale,ultimateIndicatorCenter.Y-iconSize*.5f+style.offsetY*uiScale),
                             ImVec2(iconSize,iconSize),elementColor(VisualElement::Avatar));
            drewAny = true;
        }

        if(display.worldUltimate && entity.display_ultimate_tick && DWORD(renderTick-entity.display_ultimate_tick)<2000 && std::isfinite(entity.ultimate) && entity.ultimate>=0 && entity.ultimate<=100.01f) {
            char label[24];std::snprintf(label,sizeof(label),"%.0f%%",entity.ultimate);
            text(VisualElement::Ultimate,left+width+18.0f*uiScale,top,label);drewAny=true;
        }
        if(display.prediction) {
            Vector2 predicted{};
            const Vector3 end=OW::TargetingDetail::ApplyPrediction(entity,entity.chest_pos,true,false);
            if(renderViewMatrix.WorldToScreen(end,&predicted,OW::ScreenSizeSnapshot())) {
                const auto& style=elementStyle(VisualElement::Prediction);
                const ImVec2 canvasPoint = Render::ToCanvas(predicted);
                ImGui::GetForegroundDrawList()->AddCircle(
                    ImVec2(canvasPoint.x+style.offsetX*canvasScale,canvasPoint.y+style.offsetY*canvasScale),
                    style.size*.25f*canvasScale,elementColor(VisualElement::Prediction),24,style.thickness*canvasScale);
            }
        }
        if (showAboveHeadSkillCooldowns) {
            const auto& style = elementStyle(VisualElement::Skills);
            OverlayRenderDetail::DrawSkillCooldowns(entity,heroName,left+width+8.0f*uiScale+style.offsetX*uiScale,top+20.0f*uiScale+style.offsetY*uiScale,visualOpacity,&style,elementColor(VisualElement::Skills),uiScale);
            drewAny = true;
        }

        if (drewAny) {
            captureDrawnSample(entity, left, top, width, height, centerX, bottom, dist);
            renderStats.drawn++;
        }
    }
    OverlayRenderDetail::PruneProjectionJumpTracks(projectionJumpTracks, renderTick);
    OverlayRenderDetail::PruneTrainingBotPredictionTracks(trainingBotPredictionTracks, renderTick);
    renderStats.trainingBotPredictionLeadDrops = trainingBotPredictionLeadDropTotal;
    renderStats.trainingBotPredictionLastDropAddress = trainingBotPredictionLastDropAddress;
    renderStats.trainingBotPredictionLastDropFromMs = trainingBotPredictionLastDropFromMs;
    renderStats.trainingBotPredictionLastDropToMs = trainingBotPredictionLastDropToMs;
    renderStats.trainingBotPredictionLastDropOffsetCm = trainingBotPredictionLastDropOffsetCm;
    if (projectionJumpDelta.size() >= 3) {
        const float medianDx = medianFloat(projectionJumpDx);
        const float medianDy = medianFloat(projectionJumpDy);
        const float medianDelta = medianFloat(projectionJumpDelta);
        const float medianMagnitude = std::sqrt(medianDx * medianDx + medianDy * medianDy);
        size_t aligned = 0;
        if (medianMagnitude > 1.0f && medianDelta >= 70.0f) {
            for (size_t i = 0; i < projectionJumpDelta.size(); ++i) {
                const float dot = projectionJumpDx[i] * medianDx + projectionJumpDy[i] * medianDy;
                if (dot >= medianMagnitude * projectionJumpDelta[i] * 0.65f)
                    ++aligned;
            }
        }
        if (aligned >= 3 && aligned * 2 >= projectionJumpDelta.size())
            Diagnostics::RecordProjectionGlobalJump(aligned, medianDx, medianDy, medianDelta);
    }
    publishStats();
}

void PlayerInfo(bool boxPerfMode) {
    const auto entity_snapshot = OW::TargetingDetail::SnapshotEntities();
    const auto local_snapshot = OW::TargetingDetail::SnapshotLocalEntity();
    PlayerInfoFromSnapshot(entity_snapshot, local_snapshot, boxPerfMode);
}

#include "HeroStatusHud.inl"
