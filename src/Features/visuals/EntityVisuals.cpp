#include "EntityVisuals.hpp"
#include "Game/Overwatch.hpp"
#include "Game/TrainingBotDisplay.hpp"
#include "Game/snapshots/Interpolation.hpp"
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

    inline bool IsSkillReadyCooldownSentinel(float cooldown) {
        return std::isfinite(cooldown) && std::fabs(cooldown - 1.0f) <= 0.001f;
    }

    inline bool IsSkillOnCooldown(bool active, float cooldown) {
        return !active && std::isfinite(cooldown) && cooldown > 0.05f &&
               !IsSkillReadyCooldownSentinel(cooldown);
    }

    inline std::string FormatCooldownLabel(const char* label, float cooldown) {
        char buffer[24] = {};
        std::snprintf(buffer, sizeof(buffer), "%s: %.1fs", label, cooldown);
        return buffer;
    }

    inline std::string CooldownSummary(const OW::c_entity& entity) {
        std::string text;
        if (IsSkillOnCooldown(entity.skill1act, entity.skillcd1))
            text = FormatCooldownLabel("S1", entity.skillcd1);
        if (IsSkillOnCooldown(entity.skill2act, entity.skillcd2)) {
            if (!text.empty()) text += " ";
            text += FormatCooldownLabel("S2", entity.skillcd2);
        }
        return text;
    }

    inline void DrawSkillTextCooldowns(const OW::c_entity& entity, float x, float y, float opacity) {
        int line = 0;
        const ImU32 cooldownColor = ImU32WithAlpha(255, 230, 120, opacity);
        if (IsSkillOnCooldown(entity.skill1act, entity.skillcd1)) {
            const std::string text = FormatCooldownLabel("S1", entity.skillcd1);
            Render::DrawStrokeText(ImVec2(x, y + line * 13.0f), cooldownColor, text.c_str(), 12.0f);
            ++line;
        }
        if (IsSkillOnCooldown(entity.skill2act, entity.skillcd2)) {
            const std::string text = FormatCooldownLabel("S2", entity.skillcd2);
            Render::DrawStrokeText(ImVec2(x, y + line * 13.0f), cooldownColor, text.c_str(), 12.0f);
        }
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
        if (loaded(icons.ability1Icon) && loaded(icons.ability2Icon) && loaded(icons.ultimateIcon))
            return;
        iconManager->LoadAbilityIcons(icons.heroSlug, {
            icons.ability1Icon,
            icons.ability2Icon,
            icons.ultimateIcon
        });
    }

    inline bool DrawAbilityIconSlot(IconManager* iconManager, const OW::HeroAbilityIcons& icons,
                                    const char* abilityIcon, float& cursorX, float y,
                                    float iconSize, float gap, float opacity,
                                    bool onCooldown, float cooldown, const char* missingLabel = nullptr) {
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
                         ImU32WithAlpha(255, 255, 255, alpha));

        if (onCooldown) {
            Render::DrawFilledRect(Vector2(cursorX, y), iconSize, iconSize,
                                   ImColorWithAlpha(0, 0, 0, alpha * 0.58f));
            const std::string text = FormatIconCooldown(cooldown);
            DrawIconSlotText(cursorX, y, iconSize, text,
                             ImU32WithAlpha(255, 230, 120, alpha), 9.0f);
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
                                   float x, float y, float opacity) {
        const OW::HeroAbilityIcons* icons = OW::GetAbilityIcons(heroName);
        IconManager* iconManager = Render::GetIconManager();
        if (!icons || !iconManager) {
            DrawSkillTextCooldowns(entity, x, y, opacity);
            return;
        }

        EnsureAbilityIconsLoaded(iconManager, *icons);

        constexpr float iconSize = 18.0f;
        constexpr float gap = 3.0f;
        float cursorX = x;
        const bool includeUltimateIcon = false;
        const bool drewFirst = DrawAbilityIconSlot(iconManager, *icons, icons->ability1Icon, cursorX, y,
                                iconSize, gap, opacity,
                                IsSkillOnCooldown(entity.skill1act, entity.skillcd1), entity.skillcd1, "S1");
        const bool drewSecond = DrawAbilityIconSlot(iconManager, *icons, icons->ability2Icon, cursorX, y,
                                iconSize, gap, opacity,
                                IsSkillOnCooldown(entity.skill2act, entity.skillcd2), entity.skillcd2, "S2");
        const bool drewUltimate = includeUltimateIcon && DrawUltimateIconSlot(iconManager, *icons, cursorX, y,
                                                         iconSize, gap, opacity, entity.ultimate);

        if (!drewFirst && !drewSecond && !drewUltimate)
            DrawSkillTextCooldowns(entity, x, y, opacity);
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

    inline void DrawSkeleton(const OW::c_entity& entity, const OW::Matrix& view, const Render::Color& color, float thickness) {
        Vector2 points[18]{};
        bool projected[18]{};
        const Vector2 windowSize(OW::WX, OW::WY);
        constexpr int kRenderSkeletonSlots =
            static_cast<int>(OW::Plexies20260609::kSkeletonSlotCount);

        for (int i = 0; i < kRenderSkeletonSlots; ++i) {
            if (!entity.skeleton_bone_valid[i])
                continue;
            projected[i] = view.WorldToScreen(entity.skeleton_bones[i], &points[i], windowSize);
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
                             float opacity) {
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
                        Render::DrawLine(sa,sb,shapeColor,1.f);drew=true;
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
        constexpr float capsuleThickness = 1.0f;
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

    std::vector<AimTriggerStatusRow> BuildAimTriggerStatusRowsForHero(uint64_t heroId) {
        OW::Config::ScopedConfigRead configRead;
        std::vector<AimTriggerStatusRow> rows;

        rows.push_back({
            "Hero: " + CurrentLocalHeroName(heroId),
            ImU32WithAlpha(255, 255, 255, 0.96f),
            13.5f
        });

        const OW::HeroPerkRuntime::Snapshot perkSnapshot = OW::HeroPerkRuntime::CurrentSnapshot();
        if (perkSnapshot.supported && perkSnapshot.heroId == heroId) {
            std::string perkLine = "Perk: ";
            perkLine += perkSnapshot.effectivePerkOn ? "On" : "Off";
            perkLine += " | ";
            perkLine += OW::HeroPerkRuntime::EffectiveSourceName(perkSnapshot.source);
            if (perkSnapshot.variantId && perkSnapshot.variantId[0] != '\0') {
                perkLine += " | ";
                perkLine += perkSnapshot.variantId;
            }

            rows.push_back({
                perkLine,
                perkSnapshot.effectivePerkOn
                    ? ImU32WithAlpha(166, 255, 199, 0.96f)
                    : ImU32WithAlpha(226, 232, 240, 0.72f),
                11.5f
            });
        }

        std::vector<std::pair<int, OW::Config::HeroSlotPreset>> aimSlots;
        std::vector<std::pair<int, OW::Config::HeroSlotPreset>> triggerSlots;
        aimSlots.reserve(OW::Config::kMaxHeroPresetSlots);
        triggerSlots.reserve(OW::Config::kMaxHeroPresetSlots);

        for (int slotIndex = 0; slotIndex < OW::Config::kMaxHeroPresetSlots; ++slotIndex) {
            OW::Config::HeroSlotPreset slot{};
            if (OW::Config::TryGetHeroAimSlot(heroId, slotIndex, slot))
                aimSlots.emplace_back(slotIndex, slot);
            if (OW::Config::TryGetHeroTriggerSlot(heroId, slotIndex, slot))
                triggerSlots.emplace_back(slotIndex, slot);
        }

        auto firstEnabledSlot = [](const std::vector<std::pair<int, OW::Config::HeroSlotPreset>>& slots) {
            for (const auto& item : slots) {
                if (item.second.enabled)
                    return item.first;
            }
            return -1;
        };

        const int activeAimSlot = firstEnabledSlot(aimSlots);
        const int runtimeTriggerSlot = firstEnabledSlot(triggerSlots);

        if (!aimSlots.empty()) {
            rows.push_back({ "Aim active slots", ImU32WithAlpha(126, 220, 255, 0.94f), 11.5f });
            for (const auto& item : aimSlots) {
                const int slotIndex = item.first;
                const OW::Config::HeroSlotPreset& slot = item.second;
                const bool active = slot.enabled && slotIndex == activeAimSlot;
                char line[240]{};
                std::snprintf(line, sizeof(line),
                              "#%d %s | %s | FOV %.0f deg | Hold | enabled:%s%s",
                              slotIndex + 1,
                              OW::Labels::AimModeName(slot.preset.aimMode),
                              OW::Labels::AimActivationKeyName(slot.preset.key),
                              slot.preset.fov,
                              YesNo(slot.enabled),
                              active ? " | active" : "");
                rows.push_back({
                    line,
                    active
                        ? ImU32WithAlpha(162, 255, 188, 0.96f)
                        : ImU32WithAlpha(226, 232, 240, slot.enabled ? 0.90f : 0.58f),
                    11.2f
                });
            }
        }

        if (!triggerSlots.empty()) {
            rows.push_back({ "Trigger active slots", ImU32WithAlpha(255, 204, 118, 0.95f), 11.5f });
            for (const auto& item : triggerSlots) {
                const int slotIndex = item.first;
                const OW::Config::HeroSlotPreset& slot = item.second;
                const OW::Config::TriggerPreset& trigger = slot.preset.trigger;
                const bool active = slot.enabled && trigger.enabled && slotIndex == runtimeTriggerSlot;
                char line[320]{};
                std::snprintf(line, sizeof(line),
                              "#%d %s | %s | %s | reload-block:%s | charge:%s | invis:%s | slot:%s trig:%s%s",
                              slotIndex + 1,
                              OW::AttackActionCompactNameForHero(heroId, trigger.action),
                              OW::Labels::TriggerbotModeName(trigger.mode),
                              OW::Labels::AimActivationKeyName(trigger.key),
                              YesNo(trigger.disableWhileReloading),
                              YesNo(trigger.chargeAware),
                              YesNo(trigger.ignoreInvisible),
                              YesNo(slot.enabled),
                              YesNo(trigger.enabled),
                              active ? " | active" : "");
                rows.push_back({
                    line,
                    active
                        ? ImU32WithAlpha(255, 232, 161, 0.97f)
                        : ImU32WithAlpha(226, 232, 240, (slot.enabled && trigger.enabled) ? 0.90f : 0.58f),
                    10.8f
                });
            }
        }

        if (OW::Config::secondaim || OW::Config::View_triggerbot2()) {
            rows.push_back({ "Secondary", ImU32WithAlpha(190, 168, 255, 0.95f), 11.5f });
            if (OW::Config::secondaim) {
                const int secondaryAimMode = OW::Config::Flick2 ? 1 : 0;
                char line[192]{};
                std::snprintf(line, sizeof(line),
                              "Secondary Aim | %s | %s | Hold | enabled:Y",
                              OW::Labels::AimModeName(secondaryAimMode),
                              OW::Labels::AimActivationKeyName(OW::Config::aim_key2));
                rows.push_back({ line, ImU32WithAlpha(204, 194, 255, 0.96f), 11.0f });
            }
            if (OW::Config::View_triggerbot2()) {
                char line[256]{};
                std::snprintf(line, sizeof(line),
                              "Triggerbot2 | Primary | %s | %s | reload-block:%s | charge:%s | invis:%s | enabled:Y",
                              OW::Labels::TriggerbotModeName(OW::Config::triggerbotMode2),
                              OW::Labels::AimActivationKeyName(OW::Config::triggerbotKey2),
                              YesNo(OW::Config::triggerbotDisableWhileReloading2),
                              YesNo(OW::Config::triggerbotChargeAware2),
                              YesNo(OW::Config::triggerbotIgnoreInvisible2));
                rows.push_back({ line, ImU32WithAlpha(204, 194, 255, 0.96f), 10.8f });
            }
        }

        if (rows.size() == 1)
            rows.push_back({ heroId == 0 ? "Hero configuration unavailable" : "No hero Aim/Trigger slots",
                ImU32WithAlpha(176, 184, 196, 0.78f), 11.0f });

        return rows;
    }

    inline std::vector<AimTriggerStatusRow> BuildAimTriggerStatusRows() {
        return BuildAimTriggerStatusRowsForHero(OW::SnapshotLocalHeroIdForStatus());
    }

    inline float ScaledTextWidth(const std::string& text, float fontSize) {
        ImVec2 size = ImGui::CalcTextSize(text.c_str());
        const float baseFontSize = ImGui::GetFontSize();
        if (baseFontSize > 0.0f)
            size.x *= fontSize / baseFontSize;
        return size.x;
    }

    inline float AimTriggerStatusPanelWidth(const std::vector<AimTriggerStatusRow>& rows) {
        float width = 0.0f;
        for (const AimTriggerStatusRow& row : rows)
            width = (std::max)(width, ScaledTextWidth(row.text, row.fontSize));
        return std::clamp(width + 18.0f, 260.0f, 360.0f);
    }

    float AimTriggerStatusPanelHeight(const std::vector<AimTriggerStatusRow>& rows) {
        if (rows.empty())
            return 0.0f;

        constexpr float paddingY = 7.0f;
        constexpr float lineGap = 2.0f;
        float height = paddingY * 2.0f;
        for (const AimTriggerStatusRow& row : rows)
            height += row.fontSize + lineGap;
        return height - lineGap;
    }

    float AimTriggerStatusPanelHeight() {
        return AimTriggerStatusPanelHeight(BuildAimTriggerStatusRows());
    }

    inline float AimTriggerStatusPanelReservedBottomY() {
        const float height = AimTriggerStatusPanelHeight();
        return height > 0.0f ? 10.0f + height + 8.0f : 10.0f;
    }

    inline float DrawAimTriggerStatusPanelRows(const std::vector<AimTriggerStatusRow>& rows) {
        if (rows.empty())
            return 0.0f;

        constexpr float panelX = 10.0f;
        constexpr float panelY = 10.0f;
        constexpr float paddingX = 9.0f;
        constexpr float paddingY = 7.0f;
        constexpr float lineGap = 2.0f;
        const float panelWidth = AimTriggerStatusPanelWidth(rows);
        const float panelHeight = AimTriggerStatusPanelHeight(rows);

        Render::DrawFilledRect(Vector2(panelX, panelY), panelWidth, panelHeight,
                               ImColorWithAlpha(6, 9, 13, 0.74f));
        Render::DrawFilledRect(Vector2(panelX, panelY), 3.0f, panelHeight,
                               ImColorWithAlpha(80, 190, 255, 0.88f));
        Render::DrawRect(Vector2(panelX, panelY), panelWidth, panelHeight,
                         Render::Color(255, 255, 255, ToByte(0.14f)), 1.0f);

        float textY = panelY + paddingY;
        for (const AimTriggerStatusRow& row : rows) {
            Render::DrawText(ImVec2(panelX + paddingX, textY), row.color,
                             row.text.c_str(), row.fontSize);
            textY += row.fontSize + lineGap;
        }

        return panelY + panelHeight;
    }

    float DrawAimTriggerStatusPanel() {
        const std::vector<AimTriggerStatusRow> rows = BuildAimTriggerStatusRows();
        return DrawAimTriggerStatusPanelRows(rows);
    }

} // namespace OverlayRenderDetail

float DrawAimTriggerStatusPanel() {
    return OverlayRenderDetail::DrawAimTriggerStatusPanel();
}

void PlayerInfoFromSnapshot(const std::vector<OW::c_entity>& entity_snapshot,
                                   const OW::c_entity& local_snapshot,
                                   bool boxPerfMode) {
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

        OverlayRenderDetail::ProjectedEntityBounds bounds{};
        if (!OverlayRenderDetail::TryBuildProjectedBounds(entity, bounds, &renderViewMatrix)) {
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

        float top = bounds.top;
        float bottom = bounds.bottom;
        float centerX = bounds.centerX;
        const float centerY = (top + bottom) * 0.5f;
        float left = bounds.left;
        trackProjectionJump(entity, centerX, centerY);
        captureProjectedSample(entity, left, top, width, height, centerX, bottom, dist);

        const ImU32 boxColor = OverlayRenderDetail::EntityBoxColor(
            entity, index, selectedTargetKey, selectedTargetIndex, distanceOpacity);
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
        const float outlineThickness = entity.Vis ? 1.8f : 1.2f;
        const float skeletonThickness = entity.Vis ? 1.5f : 1.0f;
        const bool specialEntity = OverlayRenderDetail::IsSpecialEntity(entity);
        bool drewAny = false;

        std::string heroName;
        const bool showAboveHeadSkillCooldowns = OW::Config::skillinfo && OW::Config::skillDisplayMode == 0;
        if (OW::Config::skillinfo || OW::Config::healthbar2 || OW::Config::draw_avatar || OW::Config::name)
            heroName = OverlayRenderDetail::EntityHeroName(entity);

        ID3D11ShaderResourceView* heroIcon = nullptr;
        if (OW::Config::draw_avatar &&
            OW::EntityRosterPolicy::ShouldRenderWorldHeroAvatar(entity.Team) &&
            !specialEntity && heroName != "Unknown")
            heroIcon = OverlayRenderDetail::FindEntityHeroIcon(entity);

        constexpr float iconSize = 24.0f;
        Vector2 ultimateIndicatorCenter(centerX, top - 18.0f);
        if (heroIcon) {
            const float iconY = top - iconSize - 10.0f;
            ultimateIndicatorCenter = Vector2(centerX, iconY + iconSize * 0.5f);
        }

        if (OW::Config::draw_info || OW::Config::draw_edge || OW::Config::drawbox3d) {
            Render::DrawCorneredBox(left, top, width, bottom - top, boxColor, outlineThickness, boxOutlineColor);
            drewAny = true;
        }

        float labelY = heroIcon
            ? (top - iconSize - 24.0f)
            : (top - 18.0f);
        if (OW::Config::name && !specialEntity && heroName != "Unknown" && heroName != "Bot" && !heroName.empty()) {
            OverlayRenderDetail::DrawCenteredText(ImVec2(centerX, labelY), color, heroName, 13.0f);
            labelY += 13.0f;
            drewAny = true;
        }
        if (OW::Config::drawbattletag && !entity.battletag.empty()) {
            OverlayRenderDetail::DrawCenteredText(ImVec2(centerX, labelY), color, entity.battletag, 12.0f);
            drewAny = true;
        }

        if (OW::Config::healthbar) {
            Render::DrawHealthBar(Vector2(left - 7.0f, top), bottom - top,
                                  entity.PlayerHealth, entity.PlayerHealthMax, visualOpacity);
            OverlayRenderDetail::DrawStackedResourceBar(entity, left - 13.0f, top, bottom - top, visualOpacity);
            drewAny = true;
        }

        if (OW::Config::healthbar2) {
            int shield = static_cast<int>(entity.MinArmorHealth + entity.MinBarrierHealth);
            int maxShield = static_cast<int>(entity.MaxArmorHealth + entity.MaxBarrierHealth);
            const float compactHealthTop = heroIcon
                ? (ultimateIndicatorCenter.Y + iconSize * 0.5f + 3.0f)
                : (top - 9.0f);
            Render::DrawSeerLikeHealth(centerX, compactHealthTop, shield, maxShield,
                                        static_cast<int>(entity.MinHealth),
                                        static_cast<int>(entity.MaxHealth));
            drewAny = true;
        }

        if (OW::Config::drawline) {
            Render::DrawLine(Vector2(OW::WX * 0.5f, OW::WY), Vector2(centerX, bottom), lineColor, 1.0f);
            drewAny = true;
        }

        if (OW::Config::draw_skel && !specialEntity) {
            OverlayRenderDetail::DrawSkeleton(entity, renderViewMatrix, lineColor, skeletonThickness);
            drewAny = true;
        }

        if (OW::Config::draw_hitbox && !specialEntity)
            drewAny |= OverlayRenderDetail::DrawHitboxes(entity, renderViewMatrix, lineColor, visualOpacity);

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
                Render::DrawLine(eyeStart, eyeEnd, lineColor, 1.0f);
                drewAny = true;
            }
        }

        if (OW::Config::dist) {
            std::string distanceText = std::to_string(static_cast<int>(dist)) + "m";
            OverlayRenderDetail::DrawCenteredText(ImVec2(centerX, bottom + 4.0f), color, distanceText, 14.0f);
            drewAny = true;
        }

        if (heroIcon) {
            Render::DrawIcon(heroIcon,
                             ImVec2(centerX - iconSize * 0.5f, ultimateIndicatorCenter.Y - iconSize * 0.5f),
                             ImVec2(iconSize, iconSize),
                             OverlayRenderDetail::ImU32WithAlpha(255, 255, 255, visualOpacity));
            drewAny = true;
        }

        if (showAboveHeadSkillCooldowns) {
            OverlayRenderDetail::DrawSkillCooldowns(entity, heroName, left + width + 8.0f, top + 20.0f, visualOpacity);
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

void skillinfo(const std::vector<OW::c_entity>& entity_snapshot) {
    const bool showUltimateLeft = OW::Config::ult;
    const bool showUltimateRight = false;
    const bool showSkillLeft = OW::Config::skillinfo && OW::Config::skillDisplayMode == 1;
    const bool showSkillRight = OW::Config::skillinfo && OW::Config::skillDisplayMode == 2;
    if ((!showUltimateLeft && !showUltimateRight && !showSkillLeft && !showSkillRight) || entity_snapshot.empty())
        return;

    IconManager* iconManager = Render::GetIconManager();

    std::vector<OW::c_entity> enemies;
    std::vector<OW::c_entity> allies;
    for (const OW::c_entity& entity : entity_snapshot) {
        if(!entity.team_known || entity.scene_generation!=OW::EntitySceneGeneration() ||
            OW::EntityRosterPolicy::Expired(entity.last_discovered_tick_ms,GetTickCount()))continue;
        std::string heroname = OverlayRenderDetail::EntityHeroName(entity);
        if (heroname == "Bot" || heroname == "Unknown")
            continue;
        if (entity.HeroID == 0x16dd || entity.HeroID == 0x16ee || entity.HeroID == 0x16bb)
            continue;

        if (entity.Team)
            enemies.push_back(entity);
        else
            allies.push_back(entity);
    }

    if (enemies.empty() && allies.empty())
        return;

    auto renderEntitySkillRow = [&](const OW::c_entity& entity, float x, float y, float opacity,
                                    bool includeUltimate, bool includeCooldowns) -> float {
        if(entity.scene_generation!=OW::EntitySceneGeneration() ||
            OW::EntityRosterPolicy::Expired(entity.last_discovered_tick_ms,GetTickCount()))return y;
        const std::string heroname = OverlayRenderDetail::EntityHeroName(entity);
        constexpr float rowHeight = 54.0f;
        constexpr float cardWidth = 232.0f;
        constexpr float avatarSize = 42.0f;
        constexpr float barWidth = 4.0f;
        constexpr float barHeight = 40.0f;
        constexpr float gap = 7.0f;
        constexpr float skillSlotSize = 28.0f;
        constexpr float skillSlotGap = 6.0f;
        constexpr float ultBarWidth = 62.0f;
        constexpr float ultBarHeight = 14.0f;
        constexpr float statusStackGap = 4.0f;

        const bool rosterFresh = entity.roster_state == OW::EntityRosterState::Fresh;
        const bool rosterDead = entity.roster_state == OW::EntityRosterState::Dead;
        const bool rosterMissing = entity.roster_state == OW::EntityRosterState::Missing;
        const float alpha = std::clamp(opacity * (rosterFresh ? 1.0f : 0.52f), 0.0f, 1.0f);
        const float maskAlpha = std::clamp(opacity, 0.0f, 1.0f);
        const bool isEnemy = entity.Team;
        Render::DrawFilledRect(Vector2(x, y + 1.0f), cardWidth, rowHeight - 2.0f,
                               OverlayRenderDetail::ImColorWithAlpha(8, 11, 15, alpha * 0.70f));
        Render::DrawFilledRect(Vector2(x, y + 4.0f), 3.0f, rowHeight - 8.0f,
                               isEnemy
                                   ? OverlayRenderDetail::ImColorWithAlpha(255, 82, 92, alpha * 0.86f)
                                   : OverlayRenderDetail::ImColorWithAlpha(72, 190, 255, alpha * 0.86f));
        Render::DrawRect(Vector2(x, y + 1.0f), cardWidth, rowHeight - 2.0f,
                         Render::Color(255, 255, 255, OverlayRenderDetail::ToByte(alpha * 0.14f)), 1.0f);

        auto scaledTextSize = [](const std::string& text, float fontSize) {
            ImVec2 textSize = ImGui::CalcTextSize(text.c_str());
            const float baseFontSize = ImGui::GetFontSize();
            if (baseFontSize > 0.0f) {
                const float scale = fontSize / baseFontSize;
                textSize.x *= scale;
                textSize.y *= scale;
            }
            return textSize;
        };

        auto drawCenteredText = [&](float centerX, float centerY, const std::string& text,
                                    float fontSize, ImU32 color) {
            const ImVec2 textSize = scaledTextSize(text, fontSize);
            Render::DrawText(ImVec2(centerX - textSize.x * 0.5f,
                                    centerY - textSize.y * 0.5f),
                             color, text.c_str(), fontSize);
        };

        auto heroInitials = [&]() {
            std::string initials;
            for (unsigned char ch : heroname) {
                if (std::isalnum(ch)) {
                    initials.push_back(static_cast<char>(std::toupper(ch)));
                    if (initials.size() == 2)
                        break;
                }
            }
            return initials.empty() ? std::string("?") : initials;
        };

        const float avatarX = x + 6.0f;
        const float avatarY = y + (rowHeight - avatarSize) * 0.5f;
        if (ID3D11ShaderResourceView* avatar = OverlayRenderDetail::FindEntityHeroIcon(entity)) {
            Render::DrawIcon(avatar, ImVec2(avatarX, avatarY), ImVec2(avatarSize, avatarSize),
                             OverlayRenderDetail::ImU32WithAlpha(255, 255, 255, alpha));
        } else {
            const Render::Color fallbackColor = isEnemy
                ? Render::Color(136, 42, 48, OverlayRenderDetail::ToByte(alpha * 0.88f))
                : Render::Color(40, 112, 150, OverlayRenderDetail::ToByte(alpha * 0.88f));
            Render::DrawFilledCircle(Vector2(avatarX + avatarSize * 0.5f,
                                             avatarY + avatarSize * 0.5f),
                                     avatarSize * 0.5f, fallbackColor, 40);
            drawCenteredText(avatarX + avatarSize * 0.5f, avatarY + avatarSize * 0.5f,
                             heroInitials(), 13.0f,
                             OverlayRenderDetail::ImU32WithAlpha(255, 255, 255, alpha));
        }

        const float baseHealth = OverlayRenderDetail::PositiveFinite(entity.MinHealth);
        const float armorHealth = OverlayRenderDetail::PositiveFinite(entity.MinArmorHealth);
        const float shieldHealth = OverlayRenderDetail::PositiveFinite(entity.MinBarrierHealth);
        const float liveCurrentHealth = OverlayRenderDetail::PositiveFinite(entity.PlayerHealth) > 0.0f
            ? OverlayRenderDetail::PositiveFinite(entity.PlayerHealth)
            : baseHealth + armorHealth + shieldHealth;
        const float currentHealth = rosterFresh ? liveCurrentHealth : 0.0f;
        float maxHealth = OverlayRenderDetail::PositiveFinite(entity.PlayerHealthMax);
        if (maxHealth <= 0.0f) {
            maxHealth = OverlayRenderDetail::PositiveFinite(entity.MaxHealth) +
                        OverlayRenderDetail::PositiveFinite(entity.MaxArmorHealth) +
                        OverlayRenderDetail::PositiveFinite(entity.MaxBarrierHealth);
        }
        const float healthRatio = maxHealth > 0.0f ? OverlayRenderDetail::Clamp01(currentHealth / maxHealth) : 0.0f;
        const ImColor healthColor = !rosterFresh
            ? OverlayRenderDetail::ImColorWithAlpha(132, 138, 148, alpha)
            : (healthRatio > 0.60f
            ? OverlayRenderDetail::ImColorWithAlpha(55, 230, 95, alpha)
            : (healthRatio >= 0.30f
                ? OverlayRenderDetail::ImColorWithAlpha(245, 210, 72, alpha)
                : OverlayRenderDetail::ImColorWithAlpha(245, 76, 72, alpha)));

        auto drawVerticalBar = [&](float barX, float barY, float ratio, const ImColor& fillColor) {
            Render::DrawFilledRect(Vector2(barX, barY), barWidth, barHeight,
                                   OverlayRenderDetail::ImColorWithAlpha(0, 0, 0, alpha * 0.48f));
            const float fillHeight = barHeight * OverlayRenderDetail::Clamp01(ratio);
            if (fillHeight > 0.0f) {
                Render::DrawFilledRect(Vector2(barX, barY + barHeight - fillHeight),
                                       barWidth, fillHeight, fillColor);
            }
            Render::DrawRect(Vector2(barX, barY), barWidth, barHeight,
                             Render::Color(255, 255, 255, OverlayRenderDetail::ToByte(alpha * 0.18f)), 1.0f);
        };

        float cursorX = avatarX + avatarSize + 4.0f;
        const float barY = y + (rowHeight - barHeight) * 0.5f;
        drawVerticalBar(cursorX, barY, healthRatio, healthColor);
        cursorX += barWidth + 3.0f;

        const float resourceX = cursorX;
        const float armorMax = OverlayRenderDetail::PositiveFinite(entity.MaxArmorHealth);
        const float shieldMax = OverlayRenderDetail::PositiveFinite(entity.MaxBarrierHealth);
        const float resourceCurrent = armorHealth + shieldHealth;
        float resourceMax = armorMax + shieldMax;
        if (resourceMax <= 0.0f)
            resourceMax = resourceCurrent;
        if (resourceMax > 0.0f || resourceCurrent > 0.0f) {
            Render::DrawFilledRect(Vector2(resourceX, barY), barWidth, barHeight,
                                   OverlayRenderDetail::ImColorWithAlpha(0, 0, 0, alpha * 0.48f));
            float fillBottom = barY + barHeight;
            const float clampedArmor = armorMax > 0.0f && armorHealth > armorMax ? armorMax : armorHealth;
            const float clampedShield = shieldMax > 0.0f && shieldHealth > shieldMax ? shieldMax : shieldHealth;
            const float armorHeight = barHeight * OverlayRenderDetail::Clamp01(clampedArmor / resourceMax);
            const float shieldHeight = barHeight * OverlayRenderDetail::Clamp01(clampedShield / resourceMax);
            if (armorHeight > 0.0f) {
                fillBottom -= armorHeight;
                Render::DrawFilledRect(Vector2(resourceX, fillBottom), barWidth, armorHeight,
                                       OverlayRenderDetail::ImColorWithAlpha(255, 194, 58, alpha));
            }
            if (shieldHeight > 0.0f && fillBottom > barY) {
                const float segmentHeight = (shieldHeight < fillBottom - barY) ? shieldHeight : (fillBottom - barY);
                fillBottom -= segmentHeight;
                Render::DrawFilledRect(Vector2(resourceX, fillBottom), barWidth, segmentHeight,
                                       OverlayRenderDetail::ImColorWithAlpha(68, 220, 255, alpha));
            }
            Render::DrawRect(Vector2(resourceX, barY), barWidth, barHeight,
                             Render::Color(255, 255, 255, OverlayRenderDetail::ToByte(alpha * 0.18f)), 1.0f);
        }
        cursorX += barWidth + gap;

        const std::string hpText = rosterFresh
            ? std::to_string(static_cast<int>(currentHealth + 0.5f))
            : (rosterDead ? std::string("0") : std::string("--"));
        Render::DrawText(ImVec2(cursorX, y + 17.0f),
                         OverlayRenderDetail::ImU32WithAlpha(255, 255, 255, alpha),
                         hpText.c_str(), 20.0f);
        cursorX += 48.0f + gap;

        const float statusBlockX = x + cardWidth - 7.0f - ultBarWidth;
        const float statusBlockTop = y + (rowHeight - (includeCooldowns && includeUltimate
            ? skillSlotSize + statusStackGap + ultBarHeight
            : (includeCooldowns ? skillSlotSize : ultBarHeight))) * 0.5f;
        const float skillRowY = statusBlockTop;
        const float ultRowY = statusBlockTop + (includeCooldowns ? skillSlotSize + statusStackGap : 0.0f);

        if (includeUltimate) {
            const float ultimate = std::isfinite(entity.ultimate)
                ? std::clamp(entity.ultimate, 0.0f, 100.0f)
                : 0.0f;
            const bool ultimateReady = ultimate >= 100.0f;
            const float ultY = ultRowY;
            Render::DrawFilledRect(Vector2(statusBlockX, ultY), ultBarWidth, ultBarHeight,
                                   ultimateReady
                                       ? OverlayRenderDetail::ImColorWithAlpha(116, 82, 16, alpha * 0.92f)
                                       : OverlayRenderDetail::ImColorWithAlpha(56, 60, 68, alpha * 0.86f));
            if (ultimate > 0.0f) {
                Render::DrawFilledRect(Vector2(statusBlockX, ultY), ultBarWidth * (ultimate / 100.0f), ultBarHeight,
                                       ultimateReady
                                           ? OverlayRenderDetail::ImColorWithAlpha(255, 202, 42, alpha)
                                           : OverlayRenderDetail::ImColorWithAlpha(66, 214, 255, alpha));
            }
            Render::DrawRect(Vector2(statusBlockX, ultY), ultBarWidth, ultBarHeight,
                             ultimateReady
                                 ? Render::Color(255, 238, 112, OverlayRenderDetail::ToByte(alpha))
                                 : Render::Color(255, 255, 255, OverlayRenderDetail::ToByte(alpha * 0.24f)),
                             1.0f);
            drawCenteredText(statusBlockX + ultBarWidth * 0.5f, ultY + ultBarHeight * 0.5f,
                             "Ult " + OverlayRenderDetail::FormatUltimatePercent(entity.ultimate),
                             12.5f,
                             ultimateReady
                                 ? OverlayRenderDetail::ImU32WithAlpha(34, 21, 0, alpha)
                                 : OverlayRenderDetail::ImU32WithAlpha(255, 255, 255, alpha));
        }

        if (includeCooldowns) {
            const OW::HeroAbilityIcons* icons = OW::GetAbilityIcons(heroname);
            if (icons && iconManager)
                OverlayRenderDetail::EnsureAbilityIconsLoaded(iconManager, *icons);

            auto skillTexture = [&](const char* abilityIcon) -> ID3D11ShaderResourceView* {
                if (!icons || !iconManager || !abilityIcon || abilityIcon[0] == '\0')
                    return nullptr;
                return iconManager->GetIcon(std::string(icons->heroSlug) + "/" + abilityIcon);
            };

            auto drawSkillCooldownSlot = [&](const char* abilityIcon, const char* label,
                                             bool active, float cooldown) {
                const float slotX = cursorX;
                const float slotY = skillRowY;
                const bool onCooldown = OverlayRenderDetail::IsSkillOnCooldown(active, cooldown);
                const bool readyCooldownSentinel = OverlayRenderDetail::IsSkillReadyCooldownSentinel(cooldown);
                const bool ready = active || readyCooldownSentinel;
                const ImColor readyFillColor = isEnemy
                    ? OverlayRenderDetail::ImColorWithAlpha(74, 24, 18, alpha * 0.96f)
                    : OverlayRenderDetail::ImColorWithAlpha(18, 58, 48, alpha * 0.94f);
                const Render::Color readyBorderColor = isEnemy
                    ? Render::Color(255, 178, 92, OverlayRenderDetail::ToByte(alpha))
                    : Render::Color(172, 255, 226, OverlayRenderDetail::ToByte(alpha));

                Render::DrawFilledRect(Vector2(slotX, slotY), skillSlotSize, skillSlotSize,
                                       ready
                                           ? readyFillColor
                                           : OverlayRenderDetail::ImColorWithAlpha(12, 16, 22, alpha * 0.82f));
                if (ID3D11ShaderResourceView* texture = skillTexture(abilityIcon)) {
                    Render::DrawIcon(texture, ImVec2(slotX + 1.0f, slotY + 1.0f),
                                     ImVec2(skillSlotSize - 2.0f, skillSlotSize - 2.0f),
                                     OverlayRenderDetail::ImU32WithAlpha(255, 255, 255, alpha));
                } else {
                    drawCenteredText(slotX + skillSlotSize * 0.5f, slotY + skillSlotSize * 0.5f,
                                     label, 13.0f,
                                     OverlayRenderDetail::ImU32WithAlpha(255, 255, 255, ready ? alpha : alpha * 0.66f));
                }

                if (onCooldown) {
                    Render::DrawFilledRect(Vector2(slotX, slotY), skillSlotSize, skillSlotSize,
                                           OverlayRenderDetail::ImColorWithAlpha(0, 0, 0, alpha * 0.66f));
                    drawCenteredText(slotX + skillSlotSize * 0.5f, slotY + skillSlotSize * 0.5f,
                                     OverlayRenderDetail::FormatIconCooldown(cooldown), 13.0f,
                                     OverlayRenderDetail::ImU32WithAlpha(255, 244, 128, alpha));
                } else if (!ready) {
                    Render::DrawFilledRect(Vector2(slotX, slotY), skillSlotSize, skillSlotSize,
                                           OverlayRenderDetail::ImColorWithAlpha(0, 0, 0, alpha * 0.34f));
                }

                Render::DrawRect(Vector2(slotX, slotY), skillSlotSize, skillSlotSize,
                                 ready
                                     ? readyBorderColor
                                     : Render::Color(255, 255, 255, OverlayRenderDetail::ToByte(alpha * 0.28f)),
                                 ready ? 1.6f : 1.0f);
                cursorX += skillSlotSize + skillSlotGap;
            };

            cursorX = statusBlockX + (ultBarWidth - (skillSlotSize * 2.0f + skillSlotGap)) * 0.5f;
            drawSkillCooldownSlot(icons ? icons->ability1Icon : nullptr, "S1",
                                  entity.skill1act, entity.skillcd1);
            drawSkillCooldownSlot(icons ? icons->ability2Icon : nullptr, "S2",
                                  entity.skill2act, entity.skillcd2);
        }

        if (rosterDead || rosterMissing) {
            const char* statusText = rosterDead ? "DEAD" : "MISSING";
            Render::DrawFilledRect(Vector2(x, y + 1.0f), cardWidth, rowHeight - 2.0f,
                                   OverlayRenderDetail::ImColorWithAlpha(0, 0, 0, maskAlpha * 0.38f));
            constexpr float badgeWidth = 74.0f;
            constexpr float badgeHeight = 19.0f;
            const float badgeX = x + cardWidth - badgeWidth - 9.0f;
            const float badgeY = y + (rowHeight - badgeHeight) * 0.5f;
            Render::DrawFilledRect(Vector2(badgeX, badgeY), badgeWidth, badgeHeight,
                                   OverlayRenderDetail::ImColorWithAlpha(18, 21, 26, maskAlpha * 0.90f));
            Render::DrawRect(Vector2(badgeX, badgeY), badgeWidth, badgeHeight,
                             Render::Color(210, 218, 230, OverlayRenderDetail::ToByte(maskAlpha * 0.35f)), 1.0f);
            drawCenteredText(badgeX + badgeWidth * 0.5f, badgeY + badgeHeight * 0.5f,
                             statusText, 12.5f,
                             OverlayRenderDetail::ImU32WithAlpha(235, 240, 248, maskAlpha));
        }

        return y + rowHeight;
    };

    auto renderSidePanel = [&](bool isRight, bool includeUltimate, bool includeCooldowns) {
        constexpr float panelWidth = 236.0f;
        const float panelX = isRight ? OW::WX - panelWidth - 10.0f : 10.0f;
        constexpr float rowHeight = 54.0f;
        constexpr float sectionGap = 34.0f;
        constexpr float headerHeight = 26.0f;
        constexpr float opacity = 1.0f;

        const bool showEnemyRows = !includeUltimate ||
            OW::EntityRosterPolicy::ShouldRenderUltimateRosterEntry(
                OW::Config::ultimateRosterFilter, true);
        const bool showAllyRows = !includeUltimate ||
            OW::EntityRosterPolicy::ShouldRenderUltimateRosterEntry(
                OW::Config::ultimateRosterFilter, false);
        const int enemyCount = showEnemyRows ? static_cast<int>(enemies.size()) : 0;
        const int allyCount = showAllyRows ? static_cast<int>(allies.size()) : 0;

        float totalHeight = 0.0f;
        if (enemyCount > 0)
            totalHeight += headerHeight + enemyCount * rowHeight;
        if (allyCount > 0)
            totalHeight += headerHeight + allyCount * rowHeight;
        if (enemyCount > 0 && allyCount > 0)
            totalHeight += sectionGap;

        float y = (OW::WY - totalHeight) * 0.5f;
        if (y < 10.0f)
            y = 10.0f;
        if (!isRight)
            y = (std::max)(y, OverlayRenderDetail::AimTriggerStatusPanelReservedBottomY());

        auto renderRoster = [&](float startY, bool rowUltimate, bool rowCooldowns) {
            float rowY = startY;
            if (enemyCount > 0) {
                Render::DrawText(ImVec2(panelX, rowY),
                                 OverlayRenderDetail::ImU32WithAlpha(255, 106, 116, opacity),
                                 "ENEMIES", 16.5f);
                rowY += headerHeight;
                for (const OW::c_entity& entity : enemies)
                    rowY = renderEntitySkillRow(entity, panelX, rowY, opacity, rowUltimate, rowCooldowns);
            }

            if (allyCount > 0) {
                if (enemyCount > 0)
                    rowY += sectionGap;
                Render::DrawText(ImVec2(panelX, rowY),
                                 OverlayRenderDetail::ImU32WithAlpha(102, 204, 255, opacity),
                                 "ALLIES", 16.5f);
                rowY += headerHeight;
                for (const OW::c_entity& entity : allies)
                    rowY = renderEntitySkillRow(entity, panelX, rowY, opacity, rowUltimate, rowCooldowns);
            }
            return rowY;
        };

        renderRoster(y, includeUltimate, includeCooldowns);
    };

    if (showUltimateLeft || showSkillLeft)
        renderSidePanel(false, showUltimateLeft, showSkillLeft);
    if (showUltimateRight || showSkillRight)
        renderSidePanel(true, showUltimateRight, showSkillRight);
}

void skillinfo() {
    const auto entity_snapshot = OW::TargetingDetail::SnapshotEntities();
    skillinfo(entity_snapshot);
}
