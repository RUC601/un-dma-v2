#include "EntityVisuals.hpp"
#include "Game/Overwatch.hpp"
#include "Game/HeroSettings.hpp"
#include "Game/TrainingBotDisplay.hpp"
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
               point.X < OW::WX && point.Y < OW::WY &&
               std::isfinite(point.X) && std::isfinite(point.Y);
    }

    inline bool IsReasonableProjectedPoint(const Vector2& point) {
        return std::isfinite(point.X) && std::isfinite(point.Y) &&
               point.X > -OW::WX && point.X < OW::WX * 2.0f &&
               point.Y > -OW::WY && point.Y < OW::WY * 2.0f;
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
        if (!proj.WorldToScreen(world, &screen, Vector2(OW::WX, OW::WY)))
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
        ImVec2 size = ImGui::CalcTextSize(text.c_str());
        Render::DrawStrokeText(ImVec2(center.x - size.x * 0.5f, center.y), color, text.c_str(), fontSize);
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
        DrawFallbackIconText(slotX, y, iconSize, opacity, text, color, 9.0f);
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
                                   float x, float y, float opacity, const V2::VisualSettings::Style* style=nullptr,ImU32 tint=IM_COL32_WHITE) {
        const OW::HeroAbilityIcons* icons = OW::GetAbilityIcons(heroName);
        IconManager* iconManager = Render::GetIconManager();
        if (icons && iconManager) EnsureAbilityIconsLoaded(iconManager, *icons);
        for (size_t slot = 0; slot < entity.display_cooldowns.size(); ++slot) {
            const auto& sample = entity.display_cooldowns[slot];
            const bool known = sample.Fresh(GetTickCount());
            const float remaining = sample.Remaining();
            const float size=style?style->size:18.f;
            const float sx = x + float(slot) * (size+3);
            float cursor = sx;
            if (icons && iconManager)
                DrawAbilityIconSlot(iconManager, *icons, slot == 0 ? icons->ability1Icon : icons->ability2Icon,
                    cursor, y, size, 3, opacity * (known ? 1.0f : .25f),
                    known && remaining > .05f, remaining, slot == 0 ? "S1" : "S2",tint);
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
        const Vector2 windowSize(OW::WX, OW::WY);
        constexpr int kRenderSkeletonSlots =
            static_cast<int>(OW::Plexies20260609::kSkeletonSlotCount);

        for (int i = 0; i < kRenderSkeletonSlots; ++i) {
            if (!entity.skeleton_bone_valid[i])
                continue;
            projected[i] = view.WorldToScreen(entity.skeleton_bones[i], &points[i], windowSize);
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
        const Vector2 windowSize(OW::WX, OW::WY);
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
        if (!view.WorldToScreen(centerWorld, &centerScreen, Vector2(OW::WX, OW::WY)) ||
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

        radiusX = std::clamp(radiusX, 1.5f, OW::WX * 0.25f);
        radiusY = std::clamp(radiusY, 1.5f, OW::WY * 0.25f);
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
                             const Render::Color& color,
                             float opacity, float thickness=1,Vector2 offset={}) {
        // Original model shapes use their own size; never apply the aim window scale.
        if(OW::ModelShapes::Supported(entity.HeroID) && entity.model_shapes.modelIndex>=0) {
            if(!entity.model_shapes.tick || GetTickCount()-entity.model_shapes.tick>100)return false;
            const Render::Color shapeColor(color.R,color.G,color.B,ToByte(opacity*.7f));
            const auto yaw=DirectX::XMMatrixRotationY(entity.Rot.X);
            bool drew=false;
            const auto& model=OW::ModelShapes::kModels[entity.model_shapes.modelIndex];
            if(model.heroId!=entity.HeroID || entity.model_shapes.valid.size()!=model.shapes.size())return false;
            for(size_t s=0;s<model.shapes.size();++s) {
                if(!entity.model_shapes.valid[s])continue;
                auto world=[&](const Vector3& point) {
                    const Vector3 local=OW::ModelShapes::Transform(point,entity.model_shapes.poses[s]);
                    DirectX::XMFLOAT3 value{local.X,local.Y,local.Z},out{};
                    DirectX::XMStoreFloat3(&out,DirectX::XMVector3Transform(DirectX::XMLoadFloat3(&value),yaw));
                    return Vector3(out.x,out.y,out.z)+entity.pos;
                };
                OW::ModelShapes::Wire(model.shapes[s],[&](Vector3 a,Vector3 b) {
                    Vector2 sa{},sb{};
                    if(view.WorldToScreen(world(a),&sa,Vector2(OW::WX,OW::WY)) &&
                       view.WorldToScreen(world(b),&sb,Vector2(OW::WX,OW::WY)) &&
                       std::isfinite(sa.X) && std::isfinite(sa.Y) && std::isfinite(sb.X) && std::isfinite(sb.Y)) {
                        Render::DrawLine(sa+offset,sb+offset,shapeColor,thickness);drew=true;
                    }
                });
            }
            return drew;
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

            if (slotIndex >= entity.skeleton_bones.size() || !entity.skeleton_bone_valid[slotIndex])
                continue;
            ellipseReady[slotIndex] = TryBuildBoneHitboxEllipse(
                entity, view, slotIndex, entity.skeleton_bones[slotIndex], ellipses[slotIndex]);
        }

        const Render::Color hitboxColor(color.R, color.G, color.B, ToByte(opacity * 0.70f));
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

    inline void DrawVisibleEyeIndicator(const Vector2& center, const Render::Color& color) {
        Render::DrawLine(Vector2(center.X - 6.0f, center.Y), Vector2(center.X - 3.0f, center.Y - 2.0f), color, 1.0f);
        Render::DrawLine(Vector2(center.X - 6.0f, center.Y), Vector2(center.X - 3.0f, center.Y + 2.0f), color, 1.0f);
        Render::DrawLine(Vector2(center.X + 6.0f, center.Y), Vector2(center.X + 3.0f, center.Y - 2.0f), color, 1.0f);
        Render::DrawLine(Vector2(center.X + 6.0f, center.Y), Vector2(center.X + 3.0f, center.Y + 2.0f), color, 1.0f);
        Render::DrawCircle(center, 3.2f, color, 16, 1.0f);
        Render::DrawFilledCircle(center, 1.2f, color, 12);
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
    static std::unordered_map<uint64_t, OverlayRenderDetail::ProjectedBoundsState> projectedBoundsStates;
    static std::unordered_map<uint64_t,uint64_t> renderedActorGenerations;
    static std::unordered_map<uint64_t, OverlayRenderDetail::ProjectionJumpTrack> projectionJumpTracks;
    static std::unordered_map<uint64_t, OverlayRenderDetail::TrainingBotPredictionTrack> trainingBotPredictionTracks;
    static uint64_t renderedScene=0;
    const auto scene=OW::EntitySceneGeneration();
    if(renderedScene!=scene) {
        projectedBoundsStates.clear();renderedActorGenerations.clear();projectionJumpTracks.clear();trainingBotPredictionTracks.clear();
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
    if (!OW::TryGetViewMatricesSnapshot(renderViewMatrix, renderViewMatrixXor,&renderCameraSequence)) {
        RuntimeTrace::Emit(RuntimeTrace::RenderSkip, 1, entity_snapshot.size());
        projectedBoundsStates.clear();
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
        projectedBoundsStates.clear();
        projectionJumpTracks.clear();
        trainingBotPredictionTracks.clear();
        publishStats();
        return;
    }
    if (OW::WX <= 0.0f || OW::WY <= 0.0f) {
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

    for (size_t index = 0; index < entity_snapshot.size(); ++index) {
        OverlayRenderDetail::RenderPredictionInfo predictionInfo{};
        OW::c_entity entity = entity_snapshot[index];
        auto [generationIt,inserted]=renderedActorGenerations.try_emplace(entity.address,entity.actor_generation);
        if(!inserted && generationIt->second!=entity.actor_generation) {
            projectedBoundsStates.erase(entity.address);projectionJumpTracks.erase(entity.address);
            trainingBotPredictionTracks.erase(entity.address);generationIt->second=entity.actor_generation;
        }
        if(renderedActorGenerations.size()>1024)renderedActorGenerations.clear();
        if (usingPresentEntitySnapshot) {
            if (entity.present_source == OW::PresentSnapshotSource::Extrap) {
                predictionInfo.shortExtrapolationCandidate = true;
                predictionInfo.applied = true;
                predictionInfo.leadMs = static_cast<int>(std::lround(entity.present_prediction_ms));
            }
            OverlayRenderDetail::ApplyPresentRenderMicroExtrapolation(
                entity,
                renderTickUs,
                &predictionInfo);
        } else {
            entity = OverlayRenderDetail::InterpolateEntityForRender(
                entity_snapshot[index],
                renderTick,
                &predictionInfo);
        }
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
        auto elementStyle=[&](VisualElement element)->const V2::VisualSettings::Style& {return display.elements[int(element)];};
        auto arrow=[&] {
            if(!display.offscreen || !local_snapshot.HasUsableBody(renderTick,OW::EntitySceneGeneration()) || !OverlayRenderDetail::IsFiniteVector(local_snapshot.pos))return;
            Matrix view{},camera{};OW::GetViewMatricesSnapshot(view,camera);
            const auto forward=camera.get_rotation();const float yaw=std::atan2(forward.x,forward.z);
            const float dx=entity.pos.X-local_snapshot.pos.X,dz=entity.pos.Z-local_snapshot.pos.Z;
            float x=dx*std::cos(yaw)-dz*std::sin(yaw),y=-(dx*std::sin(yaw)+dz*std::cos(yaw));
            const float length=std::sqrt(x*x+y*y);if(length<.001f)return;x/=length;y/=length;
            const auto& style=elementStyle(VisualElement::Offscreen);
            const float cx=OW::WX*.5f,cy=OW::WY*.5f;
            const float reach=(std::min)((cx-30)/(std::max)(.001f,std::abs(x)),(cy-30)/(std::max)(.001f,std::abs(y)));
            const ImVec2 tip(cx+x*reach+style.offsetX,cy+y*reach+style.offsetY);
            const float size=style.size;
            ImGui::GetForegroundDrawList()->AddTriangleFilled(tip,ImVec2(tip.x-x*size-y*size*.5f,tip.y-y*size+x*size*.5f),ImVec2(tip.x-x*size+y*size*.5f,tip.y-y*size-x*size*.5f),elementColor(VisualElement::Offscreen));
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
        OverlayRenderDetail::StabilizeProjectedBounds(entity.address, bounds, renderTick, projectedBoundsStates);
        float height = bounds.height;
        float width  = bounds.width;
        if (height <= 2.0f || width <= 2.0f || !std::isfinite(height) || !std::isfinite(width)) {
            RuntimeTrace::Emit(RuntimeTrace::RenderSkip,10,entity.address,renderCameraSequence);
            renderStats.skippedBox++;
            continue;
        }
        renderStats.projected++;

        if(bounds.centerX+bounds.width*.5f<0||bounds.centerX-bounds.width*.5f>OW::WX||bounds.bottom<0||bounds.top>OW::WY){arrow();continue;}
        float top = bounds.top;
        float bottom = bounds.bottom;
        float centerX = bounds.centerX;
        const float centerY = (top + bottom) * 0.5f;
        float left = bounds.left;
        trackProjectionJump(entity, centerX, centerY);
        captureProjectedSample(entity, left, top, width, height, centerX, bottom, dist);

        const ImU32 boxColor=OW::Config::drawhealth?OverlayRenderDetail::EntityBoxColor(entity,index,selectedTargetKey,selectedTargetIndex,distanceOpacity):elementColor(VisualElement::Box);
        if (boxPerfMode) {
            const float perfThickness = entity.Vis ? 1.5f : 1.0f;
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
        const float outlineThickness=elementStyle(VisualElement::Box).thickness;
        const float skeletonThickness=elementStyle(VisualElement::Skeleton).thickness;
        const float fontScale=display.dynamicFont?std::clamp(30.f/(std::max)(1.f,dist),.7f,1.6f):1.f;
        auto text=[&](VisualElement element,float x,float y,const std::string& label) {
            const auto& style=elementStyle(element);
            OverlayRenderDetail::DrawCenteredText(ImVec2(x+style.offsetX,y+style.offsetY),elementColor(element),label,style.size*fontScale);
        };
        const bool specialEntity = OverlayRenderDetail::IsSpecialEntity(entity);
        bool drewAny = false;

        std::string heroName;
        const bool showAboveHeadSkillCooldowns = OW::Config::skillinfo;
        if (OW::Config::skillinfo || OW::Config::healthbar2 || OW::Config::draw_avatar || OW::Config::name)
            heroName = OverlayRenderDetail::EntityHeroName(entity);

        ID3D11ShaderResourceView* heroIcon = nullptr;
        if (OW::Config::draw_avatar &&
            !specialEntity && heroName != "Unknown")
            heroIcon = OverlayRenderDetail::FindEntityHeroIcon(entity);

        const float iconSize=elementStyle(VisualElement::Avatar).size;
        Vector2 ultimateIndicatorCenter(centerX, top - 18.0f);
        if (heroIcon) {
            const float iconY = top - iconSize - 10.0f;
            ultimateIndicatorCenter = Vector2(centerX, iconY + iconSize * 0.5f);
        }

        if (OW::Config::draw_info || OW::Config::draw_edge || OW::Config::drawbox3d) {
            const auto& style=elementStyle(VisualElement::Box);
            const float x=left+style.offsetX,y=top+style.offsetY;
            if(display.boxType==0)Render::DrawCorneredBox(x,y,width,bottom-top,boxColor,outlineThickness,boxOutlineColor);
            else {
                if(display.boxType==2)ImGui::GetForegroundDrawList()->AddRectFilled(ImVec2(x,y),ImVec2(x+width,y+bottom-top),(boxColor&~IM_COL32_A_MASK)|(((((boxColor>>IM_COL32_A_SHIFT)&255u)*45u)/255u)<<IM_COL32_A_SHIFT));
                ImGui::GetForegroundDrawList()->AddRect(ImVec2(x,y),ImVec2(x+width,y+bottom-top),boxColor,0,0,outlineThickness);
            }
            drewAny = true;
        }

        float labelY = heroIcon
            ? (top - iconSize - 24.0f)
            : (top - 18.0f);
        if (OW::Config::name && !specialEntity && heroName != "Unknown" && heroName != "Bot" && !heroName.empty()) {
            const std::string label=display.nameType==1?(entity.battletag.empty()?"--":entity.battletag):display.nameType==2?heroName+" / "+(entity.battletag.empty()?"--":entity.battletag):heroName;
            text(VisualElement::Name,centerX,labelY,label);
            labelY += 13.0f;
            drewAny = true;
        }
        if (OW::Config::drawbattletag && !entity.battletag.empty()) {
            text(VisualElement::BattleTag,centerX,labelY,entity.battletag);
            drewAny = true;
        }

        if (OW::Config::healthbar) {
            const auto& style=elementStyle(VisualElement::Health);
            const float ratio=entity.PlayerHealthMax>0?std::clamp(entity.PlayerHealth/entity.PlayerHealthMax,0.f,1.f):0;
            auto* draw=ImGui::GetForegroundDrawList();
            const float x=left-7+style.offsetX,y=top+style.offsetY;
            draw->AddRectFilled(ImVec2(x-1,y-1),ImVec2(x+style.thickness+1,y+bottom-top+1),IM_COL32(0,0,0,200));
            draw->AddRectFilled(ImVec2(x,y+(bottom-top)*(1-ratio)),ImVec2(x+style.thickness,y+bottom-top),elementColor(VisualElement::Health));
            OverlayRenderDetail::DrawStackedResourceBar(entity, left - 13.0f, top, bottom - top, visualOpacity);
            drewAny = true;
        }

        if (OW::Config::healthbar2) {
            const auto& style=elementStyle(VisualElement::Resources);
            const float maximum=entity.MaxHealth+entity.MaxArmorHealth+entity.MaxBarrierHealth;
            if(maximum>0&&std::isfinite(maximum)) {
                const float barWidth=style.size*5,barHeight=style.thickness*3;
                const float x=centerX-barWidth*.5f+style.offsetX,y=top-9+style.offsetY;
                auto* draw=ImGui::GetForegroundDrawList();draw->AddRectFilled(ImVec2(x-1,y-1),ImVec2(x+barWidth+1,y+barHeight+1),IM_COL32(0,0,0,200));
                const float current[]={entity.MinHealth,entity.MinArmorHealth,entity.MinBarrierHealth};
                const float maxima[]={entity.MaxHealth,entity.MaxArmorHealth,entity.MaxBarrierHealth};
                float cursor=x;auto tint=ImGui::ColorConvertU32ToFloat4(elementColor(VisualElement::Resources));
                for(int pool=0;pool<3;++pool)if(maxima[pool]>0) {
                    const float segment=barWidth*maxima[pool]/maximum,fill=segment*std::clamp(current[pool]/maxima[pool],0.f,1.f);
                    auto color=tint;const float shade=1.f-pool*.16f;color.x*=shade;color.y*=shade;color.z*=shade;
                    draw->AddRectFilled(ImVec2(cursor,y),ImVec2(cursor+fill,y+barHeight),ImGui::ColorConvertFloat4ToU32(color));
                    cursor+=segment;draw->AddLine(ImVec2(cursor,y),ImVec2(cursor,y+barHeight),IM_COL32(0,0,0,180));
                }
                drewAny=true;
            }
        }

        if (OW::Config::drawline) {
            Render::DrawLine(Vector2(OW::WX*.5f,OW::WY),Vector2(centerX+elementStyle(VisualElement::Snapline).offsetX,bottom+elementStyle(VisualElement::Snapline).offsetY),OverlayRenderDetail::ToRenderColor(ImGui::ColorConvertU32ToFloat4(elementColor(VisualElement::Snapline))),elementStyle(VisualElement::Snapline).thickness);
            drewAny = true;
        }

        if (OW::Config::draw_skel && !specialEntity) {
            OverlayRenderDetail::DrawSkeleton(entity,renderViewMatrix,OverlayRenderDetail::ToRenderColor(ImGui::ColorConvertU32ToFloat4(elementColor(VisualElement::Skeleton))),skeletonThickness,Vector2(elementStyle(VisualElement::Skeleton).offsetX,elementStyle(VisualElement::Skeleton).offsetY));
            drewAny = true;
        }

        if (OW::Config::draw_hitbox && !specialEntity)
            drewAny |= OverlayRenderDetail::DrawHitboxes(entity,renderViewMatrix,OverlayRenderDetail::ToRenderColor(ImGui::ColorConvertU32ToFloat4(elementColor(VisualElement::Hitbox))),visualOpacity,elementStyle(VisualElement::Hitbox).thickness,Vector2(elementStyle(VisualElement::Hitbox).offsetX,elementStyle(VisualElement::Hitbox).offsetY));

        if (entity.Vis) {
            OverlayRenderDetail::DrawVisibleEyeIndicator(
                Vector2(left + width + 8.0f, top + 7.0f), lineColor);
            drewAny = true;
        }

        if (OW::Config::eyeray) {
            Vector2 eyeStart{}, eyeEnd{};
            Vector3 rayEnd(Vec3.X + sinf(entity.Rot.X) * 5.0f, Vec3.Y, Vec3.Z + cosf(entity.Rot.X) * 5.0f);
            if (renderViewMatrix.WorldToScreen(Vec3, &eyeStart, Vector2(OW::WX, OW::WY)) &&
                renderViewMatrix.WorldToScreen(rayEnd, &eyeEnd, Vector2(OW::WX, OW::WY))) {
                const Vector2 offset(elementStyle(VisualElement::EyeRay).offsetX,elementStyle(VisualElement::EyeRay).offsetY);
                Render::DrawLine(eyeStart+offset,eyeEnd+offset,OverlayRenderDetail::ToRenderColor(ImGui::ColorConvertU32ToFloat4(elementColor(VisualElement::EyeRay))),elementStyle(VisualElement::EyeRay).thickness);
                drewAny = true;
            }
        }

        if (OW::Config::dist) {
            std::string distanceText = std::to_string(static_cast<int>(dist)) + "m";
            text(VisualElement::Distance,centerX,bottom+4,distanceText);
            drewAny = true;
        }

        if (heroIcon) {
            Render::DrawIcon(heroIcon,
                             ImVec2(centerX-iconSize*.5f+elementStyle(VisualElement::Avatar).offsetX,ultimateIndicatorCenter.Y-iconSize*.5f+elementStyle(VisualElement::Avatar).offsetY),
                             ImVec2(iconSize,iconSize),elementColor(VisualElement::Avatar));
            drewAny = true;
        }

        if(display.worldUltimate && entity.display_ultimate_tick && DWORD(renderTick-entity.display_ultimate_tick)<2000 && std::isfinite(entity.ultimate) && entity.ultimate>=0 && entity.ultimate<=100.01f) {
            char label[24];std::snprintf(label,sizeof(label),"%.0f%%",entity.ultimate);
            text(VisualElement::Ultimate,left+width+18,top,label);drewAny=true;
        }
        if(display.prediction) {
            Vector2 predicted{};
            const Vector3 end=OW::TargetingDetail::ApplyPrediction(entity,entity.chest_pos,true,false);
            if(renderViewMatrix.WorldToScreen(end,&predicted,Vector2(OW::WX,OW::WY))) {
                const auto& style=elementStyle(VisualElement::Prediction);
                ImGui::GetForegroundDrawList()->AddCircle(ImVec2(predicted.X+style.offsetX,predicted.Y+style.offsetY),style.size*.25f,elementColor(VisualElement::Prediction),24,style.thickness);
            }
        }
        if (showAboveHeadSkillCooldowns) {
            OverlayRenderDetail::DrawSkillCooldowns(entity,heroName,left+width+8+elementStyle(VisualElement::Skills).offsetX,top+20+elementStyle(VisualElement::Skills).offsetY,visualOpacity,&elementStyle(VisualElement::Skills),elementColor(VisualElement::Skills));
            drewAny = true;
        }

        if (drewAny) {
            captureDrawnSample(entity, left, top, width, height, centerX, bottom, dist);
            renderStats.drawn++;
        }
    }
    OverlayRenderDetail::PruneProjectedBoundsStates(projectedBoundsStates, renderTick);
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
