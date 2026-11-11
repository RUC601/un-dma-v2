#include "features/visuals/EntityVisuals.hpp"
#include "SceneRenderer.hpp"
#include "Game/Overwatch.hpp"
#include "Game/map/MapReader.hpp"
#include "Game/HeroSettings.hpp"
#include "Game/HeroSkills.hpp"
#include "Game/FovGeometry.hpp"
#include "Renderer/Renderer.hpp"
#include "Renderer/IconManager.hpp"
#include "Features/UiLabels.hpp"
#include "Utils/Diagnostics.hpp"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cfloat>
#include <vector>
#include <string>

namespace {
    static float CanvasWidth()
    {
        return OW::WX > 0.0f ? OW::WX : OW::ResolveScreenWidth();
    }

    static float CanvasHeight()
    {
        return OW::WY > 0.0f ? OW::WY : OW::ResolveScreenHeight();
    }

    static Render::Color ToRenderColor(const ImVec4& color)
    {
        return Render::Color(
            static_cast<int>(color.x * 255.0f),
            static_cast<int>(color.y * 255.0f),
            static_cast<int>(color.z * 255.0f),
            static_cast<int>(color.w * 255.0f)
        );
    }

    static void DrawOverlayInfo(const std::vector<OW::c_entity>& entities, size_t objects)
    {
        if (!OW::Config::showOverlayInfo) return;
        static DWORD lastTick = 0;
        static uint64_t lastReads = 0;
        static double readsPerSecond = 0;
        const DWORD now = GetTickCount();
        if (lastTick == 0 || DWORD(now - lastTick) >= 1000) {
            const auto snapshot = Diagnostics::Snapshot();
            const uint64_t reads = snapshot.dmaReads.total;
            if (lastTick && reads >= lastReads)
                readsPerSecond = double(reads - lastReads) * 1000 / DWORD(now - lastTick);
            lastTick = now; lastReads = reads;
        }
        const auto settings=V2::HeroSettings::Snapshot();const auto& display=settings->display;
        const float scale=display.infoScale,font=12*scale,lineHeight=14*scale;
        const float originX=10+OW::OverlayStatus::Position(display.infoX,CanvasWidth(),222*scale),originY=10+OW::OverlayStatus::Position(display.infoY,CanvasHeight(),196*scale);
        ImGui::GetForegroundDrawList()->PushClipRect(ImVec2(originX,originY), ImVec2(originX+222*scale,originY+196*scale), true);
        const char* title = "Unleashed ";

        Render::DrawText(ImVec2(originX,originY), IM_COL32_WHITE, title, 13.5f*scale);
        const float titleWidth = ImGui::GetFont()->CalcTextSizeA(13.5f*scale, FLT_MAX, 0, title).x;
        char text[96]{};
        std::snprintf(text, sizeof(text), "[%.0f]", Diagnostics::RenderFps());
        Render::DrawText(ImVec2(originX+titleWidth,originY), IM_COL32(100,240,115,255), text, 13.5f*scale);
        float y=originY+18*scale;
        auto line = [&](const std::string& value) {
            Render::DrawText(ImVec2(originX,y), IM_COL32(225,230,239,240), value.c_str(), font);
            y += lineHeight;
        };
        line("Players: " + std::to_string(entities.size()));
        std::snprintf(text, sizeof(text), "Reads: %.0f/s", readsPerSecond);
        line(text);
        line("Config: " + OW::Config::lastConfigProfile);
        line("Map: " + OW::MapData::DisplayName(V2::Maps::Snapshot().id));
        line("Hero: " + OverlayRenderDetail::CurrentLocalHeroName(OW::SnapshotLocalHeroIdForStatus()));
        line("Damage boost: --");
        line("Nano boost: --");
        line("Kitsune: --");
        line("Orbital: --");
        line("Actors: " + std::to_string(entities.size() + objects));
        line("Objects: " + std::to_string(objects));
        ImGui::GetForegroundDrawList()->PopClipRect();
    }

    static Render::Color EntityRadarColor(const OW::c_entity& entity, size_t index)
    {
        const OW::TargetingDetail::TargetLockRuntime targetLock =
            OW::TargetingDetail::SnapshotTargetLockRuntime();
        const uint64_t entityKey = entity.address
            ? entity.address
            : (entity.LinkBase ? entity.LinkBase : entity.roster_key);
        const bool selectedByKey =
            targetLock.active &&
            targetLock.entityKey != 0 &&
            entityKey != 0 &&
            targetLock.entityKey == entityKey;
        const bool selectedByIndex =
            OW::Config::Targetenemyi >= 0 &&
            index == static_cast<size_t>(OW::Config::Targetenemyi);

        if (entity.Team && (selectedByKey || selectedByIndex)) {
            return ToRenderColor(OW::Config::targetargb);
        }
        return ToRenderColor(entity.Team ? OW::Config::enargb : OW::Config::allyargb);
    }

    static void DrawPipelineDiagnostics()
    {
        if (!OW::Config::outputDebugLog)
            return;

        const Diagnostics::StatusSnapshot snapshot = Diagnostics::Snapshot();
        constexpr float x = 10.0f;
        constexpr float fontSize = 13.0f;
        float y = 10.0f;

        const ImU32 okColor = IM_COL32(120, 255, 160, 235);
        const ImU32 warnColor = IM_COL32(255, 215, 90, 235);
        const ImU32 badColor = IM_COL32(255, 90, 90, 235);
        const ImU32 textColor = IM_COL32(230, 235, 245, 235);

        auto drawLine = [&](ImU32 color, const char* text) {
            Render::DrawText(ImVec2(x, y), color, text, fontSize);
            y += 16.0f;
        };

        char line[160] = {};
        if (snapshot.dmaProbeAttempted) {
            std::snprintf(line, sizeof(line), "DMA: %s MZ=0x%04X",
                snapshot.dmaProbeSucceeded ? "MZ ok" : "MZ fail",
                static_cast<unsigned int>(snapshot.dmaProbeMagic));
            drawLine(snapshot.dmaProbeSucceeded ? okColor : badColor, line);
        } else {
            drawLine(warnColor, "DMA: probe not run");
        }

        drawLine(snapshot.viewMatrixResolved && snapshot.viewMatrixValid ? okColor : badColor,
            snapshot.viewMatrixResolved && snapshot.viewMatrixValid ? "VM: valid" : "VM: zero/invalid");

        const ImGuiIO& io = ImGui::GetIO();
        std::snprintf(line, sizeof(line), "Scale: src %.0fx%.0f canvas %.0fx%.0f fps %.0f",
            OW::WX,
            OW::WY,
            io.DisplaySize.x,
            io.DisplaySize.y,
            io.Framerate);
        drawLine(textColor, line);

        std::snprintf(line, sizeof(line), "Entities: scan %zu proc %zu",
            snapshot.lastScanEntityCount,
            snapshot.entityProcess.raw);
        drawLine(snapshot.lastScanEntityCount > 0 && snapshot.entityProcess.raw == 0 ? warnColor :
            snapshot.lastScanEntityCount > 0 ? okColor : warnColor, line);

        std::snprintf(line, sizeof(line), "Entity Hz: scan %.1f proc %.1f",
            snapshot.entityScanHz,
            snapshot.entityProcessHz);
        drawLine(snapshot.entityProcessHz >= 30.0 ? okColor : warnColor, line);

        std::snprintf(line, sizeof(line), "Entity cycles: scan %llu proc %llu",
            static_cast<unsigned long long>(snapshot.entityScanCycles),
            static_cast<unsigned long long>(snapshot.entityProcessCycles));
        drawLine(textColor, line);

        std::snprintf(line, sizeof(line), "Validated: %zu", snapshot.entityProcess.validated);
        drawLine(snapshot.entityCount > 0 ? okColor : warnColor, line);

        std::snprintf(line, sizeof(line), "Roster: fresh %zu dead %zu miss %zu exp %zu hero %zu",
            snapshot.roster.fresh,
            snapshot.roster.dead,
            snapshot.roster.missing,
            snapshot.roster.expired,
            snapshot.roster.heroChanged);
        drawLine(snapshot.roster.fresh > 0 ? okColor :
            (snapshot.roster.dead > 0 || snapshot.roster.missing > 0 ? warnColor : textColor), line);

        std::snprintf(line, sizeof(line), "Drop: null %zu dup %zu hp %zu link %zu",
            snapshot.entityProcess.nullPair,
            snapshot.entityProcess.duplicate,
            snapshot.entityProcess.healthBaseFail,
            snapshot.entityProcess.linkBaseFail);
        drawLine(textColor, line);

        std::snprintf(line, sizeof(line), "Hero: miss %zu fbFail %zu name? %zu",
            snapshot.entityProcess.heroBaseMissing,
            snapshot.entityProcess.heroFallbackFail,
            snapshot.entityProcess.nameUnknown);
        drawLine(textColor, line);

        std::snprintf(line, sizeof(line), "Bone: cand %zu base %zu vbd %zu skel %zu/%zu",
            snapshot.entityProcess.boneCandidates,
            snapshot.entityProcess.boneBaseNonZero,
            snapshot.entityProcess.velocityBoneDataNonZero,
            snapshot.entityProcess.skeletonHeadValid,
            snapshot.entityProcess.skeletonAnyValid);
        drawLine(snapshot.entityProcess.boneCandidates > 0 &&
            snapshot.entityProcess.skeletonHeadValid == 0 ? warnColor : textColor, line);

        std::snprintf(line, sizeof(line), "Bone ptr: ptr %zu base %zu",
            snapshot.entityProcess.boneDataPtrNonZero,
            snapshot.entityProcess.bonesBaseNonZero);
        drawLine(snapshot.entityProcess.boneCandidates > 0 &&
            snapshot.entityProcess.bonesBaseNonZero == 0 ? warnColor : textColor, line);

        std::snprintf(line, sizeof(line), "Bone id: tbl %zu cnt %zu rd %zu head %zu",
            snapshot.entityProcess.velocityBoneIdTableNonZero,
            snapshot.entityProcess.velocityBoneCountValid,
            snapshot.entityProcess.velocityBoneIdTableReadable,
            snapshot.entityProcess.velocityBoneHeadIdFound);
        drawLine(snapshot.entityProcess.boneCandidates > 0 &&
            snapshot.entityProcess.velocityBoneHeadIdFound < snapshot.entityProcess.boneCandidates ? warnColor : textColor, line);

        std::snprintf(line, sizeof(line), "Head: res %zu id %zu loc %zu world %zu",
            snapshot.entityProcess.headProbeResolved,
            snapshot.entityProcess.headProbeIdFound,
            snapshot.entityProcess.headProbeLocalNonZero,
            snapshot.entityProcess.headProbeWorldNonZero);
        drawLine(snapshot.entityProcess.headProbeCandidates > 0 &&
            snapshot.entityProcess.headProbeWorldNonZero < snapshot.entityProcess.headProbeCandidates ? warnColor : textColor, line);

        std::snprintf(line, sizeof(line), "Head path: near %zu/%zu far %zu/%zu ex %zu",
            snapshot.entityProcess.headProbeNearWorldNonZero,
            snapshot.entityProcess.headProbeNearCandidates,
            snapshot.entityProcess.headProbeFarWorldNonZero,
            snapshot.entityProcess.headProbeFarCandidates,
            snapshot.entityProcess.headProbeExceptions);
        drawLine(snapshot.entityProcess.headProbeFarCandidates > 0 &&
            snapshot.entityProcess.headProbeFarWorldNonZero == 0 ? warnColor : textColor, line);

        std::snprintf(line, sizeof(line), "Render: radar=%d player=%d skill=%d",
            snapshot.renderDrawRadarCalled ? 1 : 0,
            snapshot.renderPlayerInfoCalled ? 1 : 0,
            snapshot.renderSkillInfoCalled ? 1 : 0);
        drawLine(textColor, line);

        std::snprintf(line, sizeof(line), "PI: in %zu proj %zu draw %zu",
            snapshot.playerInfo.input,
            snapshot.playerInfo.projected,
            snapshot.playerInfo.drawn);
        drawLine(snapshot.playerInfo.input > 0 && snapshot.playerInfo.drawn == 0 ? warnColor : textColor, line);

        std::snprintf(line, sizeof(line), "PI skip: hp %zu dead %zu self %zu dist %zu",
            snapshot.playerInfo.skippedLocalHealth,
            snapshot.playerInfo.skippedDead,
            snapshot.playerInfo.skippedLocalEntity,
            snapshot.playerInfo.skippedDistance);
        drawLine(textColor, line);

        std::snprintf(line, sizeof(line), "W2S: low %zu high %zu box %zu win %zu",
            snapshot.playerInfo.skippedWorldToScreenLow,
            snapshot.playerInfo.skippedWorldToScreenHigh,
            snapshot.playerInfo.skippedBox,
            snapshot.playerInfo.skippedWindow);
        drawLine(textColor, line);

        std::snprintf(line, sizeof(line), "Local: sel %zu hp %d best %dcm",
            snapshot.localEntity.selected,
            snapshot.localEntity.selectedHealth,
            snapshot.localEntity.bestDistanceCm);
        drawLine(snapshot.localEntity.selected == 0 || snapshot.localEntity.selectedHealth <= 0 ? warnColor : textColor, line);

        std::snprintf(line, sizeof(line), "Local cand: angle %zu near %zu named %zu",
            snapshot.localEntity.angleCandidates,
            snapshot.localEntity.nearCameraCandidates,
            snapshot.localEntity.namedCandidates);
        drawLine(textColor, line);

        std::snprintf(line, sizeof(line), "Best: hp %d hero 0x%llX",
            snapshot.localEntity.bestHealth,
            static_cast<unsigned long long>(snapshot.localEntity.bestHeroId));
        drawLine(textColor, line);

        std::snprintf(line, sizeof(line), "Coords: head0 %zu pos!0 %zu",
            snapshot.localEntity.zeroHeadCandidates,
            snapshot.localEntity.nonZeroPositionCandidates);
        drawLine(snapshot.localEntity.zeroHeadCandidates > 0 ? warnColor : textColor, line);

        if (snapshot.renderEntityListEmpty)
            drawLine(badColor, "NO ENTITY DATA \xE2\x80\x94 check pipeline");
    }

    static void DrawDiagnosticLogOverlay()
    {
        if (!Diagnostics::IsLogOverlayVisible())
            return;

        std::vector<std::string> lines = Diagnostics::GetLogLines();

        const ImGuiViewport* viewport = ImGui::GetMainViewport();
        const ImVec2 viewportPos = viewport ? viewport->Pos : ImVec2(0.0f, 0.0f);
        const ImVec2 viewportSize = viewport ? viewport->Size : ImGui::GetIO().DisplaySize;
        const float maxWidth = (std::max)(360.0f, viewportSize.x - 32.0f);
        const float logWidth = (std::min)(980.0f, maxWidth);
        const float maxHeight = (std::max)(120.0f, viewportSize.y * 0.28f);
        const float logHeight = (std::min)(260.0f, maxHeight);

        ImGui::SetNextWindowPos(
            ImVec2(viewportPos.x + viewportSize.x * 0.5f, viewportPos.y + viewportSize.y - 24.0f),
            ImGuiCond_Always,
            ImVec2(0.5f, 1.0f));
        ImGui::SetNextWindowBgAlpha(0.72f);

        const ImGuiWindowFlags flags =
            ImGuiWindowFlags_AlwaysAutoResize |
            ImGuiWindowFlags_NoCollapse |
            ImGuiWindowFlags_NoSavedSettings;

        if (ImGui::Begin("Diagnostic Log", nullptr, flags)) {
            ImGui::Text("Lines: %zu / %zu", lines.size(), Diagnostics::GetLogLineCapacity());
            ImGui::SameLine();
            if (ImGui::Button("Clear")) {
                Diagnostics::ClearLogLines();
                lines.clear();
            }

            ImGui::Separator();
            ImGui::BeginChild(
                "##DiagnosticLogLines",
                ImVec2(logWidth, logHeight),
                true,
                ImGuiWindowFlags_HorizontalScrollbar);

            if (lines.empty()) {
                ImGui::TextDisabled("No diagnostics yet.");
            } else {
                for (const std::string& line : lines)
                    ImGui::TextUnformatted(line.c_str());
                ImGui::SetScrollHereY(1.0f);
            }

            ImGui::EndChild();
        }
        ImGui::End();
    }

    static DirectX::XMMATRIX ToDirectXMatrix(const OW::Matrix& matrix)
    {
        return DirectX::XMMATRIX(
            matrix.m11, matrix.m12, matrix.m13, matrix.m14,
            matrix.m21, matrix.m22, matrix.m23, matrix.m24,
            matrix.m31, matrix.m32, matrix.m33, matrix.m34,
            matrix.m41, matrix.m42, matrix.m43, matrix.m44);
    }

    static OW::TargetingDetail::FovRuntimeContext BuildFovRuntimeContext(const OW::Matrix& viewMatrixXor)
    {
        const DirectX::XMFLOAT3 camera = viewMatrixXor.get_location();
        const DirectX::XMFLOAT3 forward = viewMatrixXor.get_rotation();

        OW::TargetingDetail::FovRuntimeContext context{};
        context.camera = OW::Vector3(camera.x, camera.y, camera.z);
        context.forward = OW::TargetingDetail::NormalizeVector(
            OW::Vector3(forward.x, forward.y, forward.z));
        context.valid = OW::TargetingDetail::IsFiniteVector(context.camera) &&
                        !OW::TargetingDetail::IsZeroVector(context.camera) &&
                        !OW::TargetingDetail::IsZeroVector(context.forward);
        return context;
    }

    struct FovProjectionContext {
        DirectX::XMMATRIX inverseViewProjection{};
        float determinant = (std::numeric_limits<float>::quiet_NaN)();
        bool inverseFinite = false;
        bool valid = false;
    };

    static bool IsFiniteMatrix(const DirectX::XMMATRIX& matrix)
    {
        for (size_t rowIndex = 0; rowIndex < 4; ++rowIndex) {
            DirectX::XMFLOAT4 row{};
            DirectX::XMStoreFloat4(&row, matrix.r[rowIndex]);
            if (!std::isfinite(row.x) ||
                !std::isfinite(row.y) ||
                !std::isfinite(row.z) ||
                !std::isfinite(row.w)) {
                return false;
            }
        }
        return true;
    }

    static FovProjectionContext BuildFovProjectionContext(const OW::Matrix& viewProjection)
    {
        DirectX::XMVECTOR determinant{};
        const DirectX::XMMATRIX inverse = DirectX::XMMatrixInverse(
            &determinant,
            ToDirectXMatrix(viewProjection));
        const float det = DirectX::XMVectorGetX(determinant);

        FovProjectionContext context{};
        context.inverseViewProjection = inverse;
        context.determinant = det;
        context.inverseFinite = IsFiniteMatrix(inverse);
        context.valid = OW::FovGeometry::IsProjectionInverseUsable(
            context.determinant,
            context.inverseFinite);
        return context;
    }

    static bool ScreenWorldHomogeneousAtClipDepth(
        const FovProjectionContext& projection,
        float ndcX,
        float ndcY,
        float clipDepth,
        OW::FovGeometry::HomogeneousPoint& outPoint)
    {
        const DirectX::XMVECTOR clip = DirectX::XMVectorSet(ndcX, ndcY, clipDepth, 1.0f);
        const DirectX::XMVECTOR homogeneousWorld = DirectX::XMVector4Transform(
            clip,
            projection.inverseViewProjection);
        DirectX::XMFLOAT4 world{};
        DirectX::XMStoreFloat4(&world, homogeneousWorld);
        outPoint.xyz = OW::Vector3(world.x, world.y, world.z);
        outPoint.w = world.w;
        return OW::FovGeometry::IsFiniteHomogeneousPoint(outPoint);
    }

    static bool ScreenRayDirection(const FovProjectionContext& projection,
                                   const OW::Vector2& screen,
                                   float width,
                                   float height,
                                   const OW::Vector3& expectedForward,
                                   OW::Vector3& outDirection)
    {
        if (!projection.valid || width <= 0.0f || height <= 0.0f)
            return false;

        const float ndcX = (screen.X / width) * 2.0f - 1.0f;
        const float ndcY = 1.0f - (screen.Y / height) * 2.0f;

        OW::FovGeometry::HomogeneousPoint depthZero{};
        OW::FovGeometry::HomogeneousPoint depthOne{};
        if (!ScreenWorldHomogeneousAtClipDepth(
                projection, ndcX, ndcY, 0.0f, depthZero) ||
            !ScreenWorldHomogeneousAtClipDepth(
                projection, ndcX, ndcY, 1.0f, depthOne)) {
            return false;
        }

        return OW::FovGeometry::ResolveProjectionRay(
            depthZero,
            depthOne,
            expectedForward,
            outDirection);
    }

    static bool ScreenRayAngleDeg(const FovProjectionContext& projection,
                                  const OW::TargetingDetail::FovRuntimeContext& context,
                                  const OW::Vector2& screen,
                                  float width,
                                  float height,
                                  float& outAngleDeg)
    {
        if (!context.valid)
            return false;

        OW::Vector3 direction{};
        if (!ScreenRayDirection(
                projection, screen, width, height, context.forward, direction)) {
            return false;
        }

        const float dot = std::clamp(context.forward | direction, -1.0f, 1.0f);
        outAngleDeg = RAD2DEG(std::acos(dot));
        return std::isfinite(outAngleDeg);
    }

    static OW::FovGeometry::RingProjectionResult FovRadiusForViewport(
        float width,
        float height,
        float fovDeg,
        const FovProjectionContext& projection,
        const OW::TargetingDetail::FovRuntimeContext& fovContext)
    {
        const float targetAngleDeg = OW::Config::FovCircleRenderAngleDeg(fovDeg);
        const float maxRadius = std::sqrt(width * width + height * height) * 0.5f;
        const OW::Vector2 center(width * 0.5f, height * 0.5f);
        return OW::FovGeometry::ResolveRingProjection(
            targetAngleDeg,
            maxRadius,
            [&](float radius, float& outAngleDeg) {
                return ScreenRayAngleDeg(
                    projection,
                    fovContext,
                    OW::Vector2(center.X + radius, center.Y),
                    width,
                    height,
                    outAngleDeg);
            });
    }

    static const char* RingProjectionStatusName(OW::FovGeometry::RingProjectionStatus status)
    {
        switch (status) {
        case OW::FovGeometry::RingProjectionStatus::Drawable:
            return "drawable";
        case OW::FovGeometry::RingProjectionStatus::OutsideViewport:
            return "outside_viewport";
        case OW::FovGeometry::RingProjectionStatus::Unavailable:
        default:
            return "unavailable";
        }
    }

    struct FovRingProjectionDiagnosticState {
        bool observedInitialized = false;
        OW::FovGeometry::RingProjectionStatus observedStatus =
            OW::FovGeometry::RingProjectionStatus::Unavailable;
        bool loggedInitialized = false;
        OW::FovGeometry::RingProjectionStatus loggedStatus =
            OW::FovGeometry::RingProjectionStatus::Unavailable;
        DWORD lastLogTick = 0;
    };

    static void LogFovRingProjectionTransition(
        int diagnosticKey,
        float fovDeg,
        const FovProjectionContext& projection,
        const OW::FovGeometry::RingProjectionResult& result)
    {
        constexpr int kDiagnosticStateCount = OW::Config::kMaxHeroPresetSlots + 1;
        if (diagnosticKey < 0 || diagnosticKey >= kDiagnosticStateCount)
            return;

        static FovRingProjectionDiagnosticState states[kDiagnosticStateCount]{};
        FovRingProjectionDiagnosticState& state = states[diagnosticKey];
        if (!state.observedInitialized || state.observedStatus != result.status) {
            state.observedInitialized = true;
            state.observedStatus = result.status;
        }
        if (state.loggedInitialized && state.loggedStatus == state.observedStatus)
            return;

        constexpr DWORD kMinimumLogIntervalMs = 1000;
        const DWORD now = GetTickCount();
        if (state.loggedInitialized && now - state.lastLogTick < kMinimumLogIntervalMs)
            return;

        state.loggedInitialized = true;
        state.loggedStatus = state.observedStatus;
        state.lastLogTick = now;
        const float targetAngleDeg = OW::Config::FovCircleRenderAngleDeg(fovDeg);
        const char* statusName = RingProjectionStatusName(result.status);
        if (result.status == OW::FovGeometry::RingProjectionStatus::Unavailable) {
            Diagnostics::Warn(
                "FOV overlay projection key=%d status=%s targetDeg=%.3f radiusPx=%.2f centerDeg=%.3f edgeDeg=%.3f determinant=%.9g inverseFinite=%d.",
                diagnosticKey,
                statusName,
                targetAngleDeg,
                result.radius,
                result.centerAngleDeg,
                result.edgeAngleDeg,
                projection.determinant,
                projection.inverseFinite ? 1 : 0);
            return;
        }

        Diagnostics::Info(
            "FOV overlay projection key=%d status=%s targetDeg=%.3f radiusPx=%.2f centerDeg=%.3f edgeDeg=%.3f determinant=%.9g inverseFinite=%d.",
            diagnosticKey,
            statusName,
            targetAngleDeg,
            result.radius,
            result.centerAngleDeg,
            result.edgeAngleDeg,
            projection.determinant,
            projection.inverseFinite ? 1 : 0);
    }

    static void DrawDashedCircle(const OW::Vector2& center,
                                 float radius,
                                 const Render::Color& color,
                                 float thickness)
    {
        constexpr float kPi = 3.14159265358979323846f;
        constexpr int kSegments = 128;
        constexpr int kDashSegments = 3;
        constexpr int kGapSegments = 2;

        for (int start = 0; start < kSegments; start += kDashSegments + kGapSegments) {
            const int end = (std::min)(start + kDashSegments, kSegments);
            for (int segment = start; segment < end; ++segment) {
                const float a0 = (2.0f * kPi * static_cast<float>(segment)) / static_cast<float>(kSegments);
                const float a1 = (2.0f * kPi * static_cast<float>(segment + 1)) / static_cast<float>(kSegments);
                Render::DrawLine(
                    OW::Vector2(center.X + std::cos(a0) * radius, center.Y + std::sin(a0) * radius),
                    OW::Vector2(center.X + std::cos(a1) * radius, center.Y + std::sin(a1) * radius),
                    color,
                    thickness);
            }
        }
    }

    static bool DrawStyledFovRing(const OW::Vector2& center,
                                  float width,
                                  float height,
                                  float fovDeg,
                                  const FovProjectionContext& projection,
                                  const OW::TargetingDetail::FovRuntimeContext& fovContext,
                                  const OW::Config::FovRingSlotStyle& rawStyle,
                                  bool active,
                                  int diagnosticKey,
                                  const char* label)
    {
        OW::Config::FovRingSlotStyle style = rawStyle;
        if (!style.visible)
            return false;

        const OW::FovGeometry::RingProjectionResult ringProjection =
            FovRadiusForViewport(width, height, fovDeg, projection, fovContext);
        LogFovRingProjectionTransition(diagnosticKey, fovDeg, projection, ringProjection);
        if (!ringProjection.IsDrawable())
            return false;
        const float radius = ringProjection.radius;

        const float thickness = active
            ? (std::max)(style.thickness + 0.8f, 2.0f)
            : style.thickness;
        const Render::Color color = ToRenderColor(style.color);
        if (style.lineStyle == 1)
            DrawDashedCircle(center, radius, color, thickness);
        else
            Render::DrawCircle(center, radius, color, 128, thickness);

        if (style.showLabel && label && *label) {
            constexpr float kPi = 3.14159265358979323846f;
            const float angle = -0.25f * kPi;
            const float labelX = std::clamp(center.X + std::cos(angle) * radius + 6.0f,
                                            4.0f,
                                            (std::max)(4.0f, width - 92.0f));
            const float labelY = std::clamp(center.Y + std::sin(angle) * radius - 7.0f,
                                            4.0f,
                                            (std::max)(4.0f, height - 18.0f));
            Render::DrawText(ImVec2(labelX, labelY), color.ToImU32(), label, active ? 14.0f : 13.0f);
        }

        return true;
    }

    static bool DrawHeroAimFovRings(uint64_t heroId,
                                    const OW::Vector2& center,
                                    float width,
                                    float height,
                                    const FovProjectionContext& projection,
                                    const OW::TargetingDetail::FovRuntimeContext& fovContext,
                                    const OW::Config::RuntimeDrawFovState& activeState)
    {
        OW::Config::ScopedConfigRead configRead;
        bool drewAny = false;
        for (int slotIndex = 0; slotIndex < OW::Config::kMaxHeroPresetSlots; ++slotIndex) {
            OW::Config::HeroSlotPreset slot{};
            const bool found = OW::Config::TryGetHeroAimSlot(heroId, slotIndex, slot);
            if (!found || !slot.enabled)
                continue;

            const OW::Config::FovRingSlotStyle style = OW::Config::ClampFovRingStyle(
                OW::Config::FovRingStyleFor(OW::Config::FovRingSlotKind::Aim, slotIndex),
                OW::Config::FovRingSlotKind::Aim,
                slotIndex);
            const bool active = activeState.active &&
                activeState.slotKind == static_cast<int>(OW::Config::FovRingSlotKind::Aim) &&
                activeState.slotIndex == slotIndex;
            const float ringFovDeg = active
                ? activeState.fovDeg
                : OW::Config::ResolveRuntimeHeroPresetFovForDistance(slot.preset, 0.0f);

            char label[32] = {};
            std::snprintf(label,
                          sizeof(label),
                          "A%d %.0f deg",
                          slotIndex + 1,
                          OW::Config::ClampFovDeg(ringFovDeg));
            drewAny |= DrawStyledFovRing(center,
                                         width,
                                         height,
                                         ringFovDeg,
                                         projection,
                                         fovContext,
                                         style,
                                         active,
                                         slotIndex,
                                         label);
        }
        return drewAny;
    }

    static bool DrawHeroTrackingDeadzoneRings(uint64_t heroId,
                                              const OW::Vector2& center)
    {
        if (!OW::Config::drawTrackingDeadzones)
            return false;

        bool drewAny = false;
        for (int slotIndex = 0; slotIndex < OW::Config::kMaxHeroPresetSlots; ++slotIndex) {
            OW::Config::HeroSlotPreset slot{};
            const bool found = OW::Config::TryGetHeroAimSlot(heroId, slotIndex, slot);
            if (!found || !slot.enabled ||
                !OW::Config::UsesTrackingDeadzone(slot.preset.aimBehavior)) {
                continue;
            }

            const float radius = OW::Config::ClampTrackingDeadzonePixels(slot.preset.trackingDeadzone);
            if (radius <= 0.0f)
                continue;

            const OW::Config::FovRingSlotStyle style = OW::Config::ClampFovRingStyle(
                OW::Config::FovRingStyleFor(OW::Config::FovRingSlotKind::Aim, slotIndex),
                OW::Config::FovRingSlotKind::Aim,
                slotIndex);
            DrawDashedCircle(center, radius, ToRenderColor(style.color), (std::max)(1.0f, style.thickness));
            drewAny = true;
        }
        return drewAny;
    }

    static void DrawFovCircle(const OW::c_entity& localSnapshot)
    {
        const float width = CanvasWidth();
        const float height = CanvasHeight();
        if (width <= 0.0f || height <= 0.0f)
            return;

        const OW::Vector2 center(width * 0.5f, height * 0.5f);
        OW::Matrix renderViewMatrix{}, renderViewMatrixXor{};
        OW::GetViewMatricesSnapshot(renderViewMatrix, renderViewMatrixXor);
        const FovProjectionContext projection = BuildFovProjectionContext(renderViewMatrix);
        const OW::TargetingDetail::FovRuntimeContext fovContext =
            BuildFovRuntimeContext(renderViewMatrixXor);

        // Skill circles have their own switch and do not enable the global Aim circle.
        int skillDiagnosticKey = 1000;
        for (const auto& definition : OW::AllHeroSkillDefinitions()) {
            ++skillDiagnosticKey;
            if (!OW::IsHeroSkillDefinitionActiveForRuntime(definition, localSnapshot.HeroID) ||
                !(definition.controls & OW::HeroSkillControls::TrackingOverlay)) continue;
            const auto settings = OW::Config::GetHeroSkillSettings(localSnapshot.HeroID, definition.skillId, definition.defaultSettings);
            if (!settings.enabled || !settings.drawSkillFov) continue;
            OW::Config::FovRingSlotStyle style{};
            style.visible = true;
            style.color = settings.skillFovColor;
            style.thickness = 1.5f;
            style.showLabel = false;
            DrawStyledFovRing(center, width, height, settings.tracking.fov, projection, fovContext,
                             style, false, skillDiagnosticKey, nullptr);
        }
        if (!OW::Config::draw_fov) return;
        const OW::Config::RuntimeDrawFovState activeState = OW::Config::SnapshotRuntimeDrawFov();
        bool drewAny = false;
        if (localSnapshot.HeroID != 0) {
            drewAny |= DrawHeroAimFovRings(localSnapshot.HeroID,
                                           center,
                                           width,
                                           height,
                                           projection,
                                           fovContext,
                                           activeState);
        }
        if (drewAny)
            return;

        OW::Config::FovRingSlotStyle fallbackStyle{};
        fallbackStyle.visible = true;
        fallbackStyle.color = OW::Config::fovcol;
        fallbackStyle.thickness = 1.5f;
        fallbackStyle.lineStyle = 0;
        fallbackStyle.showLabel = false;
        DrawStyledFovRing(center,
                          width,
                          height,
                          OW::Config::RuntimeDrawFovOrDefault(OW::Config::View_Fov()),
                          projection,
                          fovContext,
                          fallbackStyle,
                          false,
                          OW::Config::kMaxHeroPresetSlots,
                          nullptr);
    }

    static void DrawTrackingDeadzoneCircles(const OW::c_entity& localSnapshot)
    {
        if (!OW::Config::drawTrackingDeadzones)
            return;

        const float width = CanvasWidth();
        const float height = CanvasHeight();
        if (width <= 0.0f || height <= 0.0f)
            return;

        if (localSnapshot.HeroID == 0)
            return;

        const OW::Vector2 center(width * 0.5f, height * 0.5f);
        DrawHeroTrackingDeadzoneRings(localSnapshot.HeroID, center);
    }

    static void DrawCrosshair()
    {
        if (!OW::Config::crosscircle)
            return;

        const float width = CanvasWidth();
        const float height = CanvasHeight();
        if (width <= 0.0f || height <= 0.0f)
            return;

        const auto settings=V2::HeroSettings::Snapshot();
        const auto& style=settings->display.elements[int(V2::VisualSettings::Element::Crosshair)];
        const OW::Vector2 center(width*.5f+style.offsetX,height*.5f+style.offsetY);
        const Render::Color color=ToRenderColor(style.visible);
        const float arm=style.size*.5f;
        const float radius=style.size*.5f;

        Render::DrawLine(OW::Vector2(center.X - arm, center.Y), OW::Vector2(center.X + arm, center.Y), color, style.thickness);
        Render::DrawLine(OW::Vector2(center.X, center.Y - arm), OW::Vector2(center.X, center.Y + arm), color, style.thickness);
        Render::DrawCircle(center, radius, color, 48, style.thickness);
    }

    static void DrawRadar(const std::vector<OW::c_entity>& entitySnapshot,
                          const OW::c_entity& localSnapshot)
    {
        if (!OW::Config::radar)
            return;

        if (entitySnapshot.empty() || localSnapshot.PlayerHealth <= 0.0f)
            return;

        const float width = CanvasWidth();
        const float height = CanvasHeight();
        if (width <= 0.0f || height <= 0.0f)
            return;

        const auto visual=V2::HeroSettings::Snapshot();
        const auto& radar=visual->display.radar;
        const float radius=(std::min)(radar.radius,(std::min)(width,height)*.5f-2);
        const float centerX = radius + OW::OverlayStatus::Position(OW::Config::radarPosX,width,2*radius);
        const float centerY = radius + OW::OverlayStatus::Position(OW::Config::radarPosY,height,2*radius);
        const OW::Vector2 center(centerX, centerY);
        const Render::Color border(170, 180, 192, 110);
        const Render::Color background(18,20,25,int(radar.backgroundAlpha*255));

        Render::DrawFilledCircle(center, radius, background, 72);
        Render::DrawCircle(center, radius, border, 72, 1.0f);
        Render::DrawLine(OW::Vector2(center.X - radius, center.Y), OW::Vector2(center.X + radius, center.Y), Render::Color(255, 255, 255, 55), 1.0f);
        Render::DrawLine(OW::Vector2(center.X, center.Y - radius), OW::Vector2(center.X, center.Y + radius), Render::Color(255, 255, 255, 55), 1.0f);
        Render::DrawFilledCircle(center, 3.0f, Render::Color(120, 255, 120, 220), 20);

        OW::Matrix renderViewMatrix{}, renderViewMatrixXor{};
        OW::GetViewMatricesSnapshot(renderViewMatrix, renderViewMatrixXor);
        DirectX::XMFLOAT3 forward = renderViewMatrixXor.get_rotation();
        const float yaw = static_cast<float>(radar.rotate?std::atan2(forward.x,forward.z):0.f);
        const float cosYaw = std::cos(-yaw);
        const float sinYaw = std::sin(-yaw);
        const float scale=radius/radar.rangeM;

        for (size_t index = 0; index < entitySnapshot.size(); ++index) {
            const OW::c_entity& entity = entitySnapshot[index];
            if (!entity.team_known || !entity.HasUsableBody(GetTickCount(),OW::EntitySceneGeneration()) ||
                !localSnapshot.HasUsableBody(GetTickCount(),OW::EntitySceneGeneration()) || entity.address == localSnapshot.address)
                continue;

            if(!radar.allies&&!entity.Team)continue;
            const float dx = entity.pos.X - localSnapshot.pos.X;
            const float dz = entity.pos.Z - localSnapshot.pos.Z;
            float rx = (dx * cosYaw - dz * sinYaw) * scale;
            float rz = (dx * sinYaw + dz * cosYaw) * scale;

            const float len = std::sqrt(rx * rx + rz * rz);
            const float maxLen = (std::max)(0.0f, radius-radar.markerSize*.5f-2);
            if (len > maxLen && len > 0.001f) {
                const float k = maxLen / len;
                rx *= k;
                rz *= k;
            }

            const OW::Vector2 point(center.X + rx, center.Y + rz);
            const Render::Color entityColor=ToRenderColor(entity.Team?radar.enemy:radar.ally);
            if (OW::Config::radarline)
                Render::DrawLine(center, point, Render::Color(entityColor.R, entityColor.G, entityColor.B, 90), 1.0f);
            const ImU32 markerColor=ImGui::ColorConvertFloat4ToU32(entity.Team?radar.enemy:radar.ally);
            DrawHeroRadarMarker(entity, point.X, point.Y, markerColor,radar.markerSize/18.f);
        }
    }

    static void DrawWorldObjectIcon(int type, const ImVec2& center, float size, ImU32 color) {
        auto* draw=ImGui::GetForegroundDrawList();
        const auto& objectType=V2::WorldObjects::kObjects[type];
        if(objectType.group==V2::WorldObjects::Group::Health) {
            const float arm=size*.4f,bar=(std::max)(1.f,size*.13f);
            draw->AddRectFilled(ImVec2(center.x-arm,center.y-bar),ImVec2(center.x+arm,center.y+bar),color);
            draw->AddRectFilled(ImVec2(center.x-bar,center.y-arm),ImVec2(center.x+bar,center.y+arm),color);
            return;
        }
        if(objectType.iconHero[0] && objectType.iconName[0]) {
            if(auto* manager=Render::GetIconManager()) {
                const std::string key=std::string(objectType.iconHero)+"/"+objectType.iconName;
                auto* texture=manager->GetIcon(key);
                if(!texture) {
                    manager->LoadAbilityIcons(objectType.iconHero,{objectType.iconName});
                    texture=manager->GetIcon(key);
                }
                if(texture) {
                    Render::DrawIcon(texture,ImVec2(center.x-size*.5f,center.y-size*.5f),ImVec2(size,size),color);
                    return;
                }
            }
        }
        const float radius=(std::max)(3.f,size*.3f);
        const ImVec2 corners[]{ImVec2(center.x,center.y-radius),ImVec2(center.x+radius,center.y),
            ImVec2(center.x,center.y+radius),ImVec2(center.x-radius,center.y)};
        draw->AddPolyline(corners,4,color,ImDrawFlags_Closed,1.5f);
    }

    static void DrawHealthPacks(const std::vector<OW::hpanddy>& objects) {
        const auto settings=V2::HeroSettings::Snapshot();const auto& world=settings->display.world;
        if(!world.enabled)return;
        OW::Matrix view{},camera{};OW::GetViewMatricesSnapshot(view,camera);
        const auto local=OW::SnapshotLocalEntity();const uint32_t now=GetTickCount();
        for(const auto& object:objects) {
            const int type=V2::VisualSettings::ObjectIndex(object.entityid);
            if(type<0||!world.types[type]||object.scene_generation!=OW::EntitySceneGeneration()||
                !object.position_tick_ms||uint32_t(now-object.position_tick_ms)>200)continue;
            const auto& objectType=V2::WorldObjects::kObjects[type];
            using V2::WorldObjects::Group;
            if((objectType.group==Group::Deployable&&!world.deployables)||
                (objectType.group==Group::Projectile&&!world.projectiles))continue;
            const bool statusFresh=object.status_tick_ms&&uint32_t(now-object.status_tick_ms)<=200;
            if(world.hideTeam&&objectType.group!=Group::Health&&statusFresh&&object.team_known&&
                local.team_known&&object.team_key&&object.team_key!=(1u<<0x1B)&&object.team_key==local.team_key)continue;
            const OW::Vector3 position(object.POS.x,object.POS.y,object.POS.z);
            const float distance=local.pos.DistTo(position);
            if(world.maxDistance>0&&distance>world.maxDistance)continue;
            OW::Vector2 screen{};if(!view.WorldToScreen(position,&screen,OW::Vector2(CanvasWidth(),CanvasHeight())))continue;
            const auto color=ImGui::ColorConvertFloat4ToU32(world.colors[type]);auto* draw=ImGui::GetForegroundDrawList();
            if(world.box)draw->AddRect(ImVec2(screen.X-world.iconSize*.5f,screen.Y-world.iconSize*.5f),ImVec2(screen.X+world.iconSize*.5f,screen.Y+world.iconSize*.5f),color,2,0,1.5f);
            if(world.icon)DrawWorldObjectIcon(type,ImVec2(screen.X,screen.Y),world.iconSize,color);
            std::string label=world.name?UI::UiLabel(objectType.name,OW::Config::uiLanguage==1):"";
            if(world.distance)label+=(label.empty()?"":" ")+std::to_string(int(distance))+" m";
            const bool healthFresh=statusFresh&&object.health_known&&std::isfinite(object.health)&&
                std::isfinite(object.health_max)&&object.health>=0&&object.health_max>0;
            if(world.healthText&&healthFresh) {
                char healthText[48]{};std::snprintf(healthText,sizeof(healthText),"%.0f / %.0f",object.health,object.health_max);
                label+=(label.empty()?"":" ")+std::string(healthText);
            }
            if(!label.empty())Render::DrawStrokeText(ImVec2(screen.X+(world.icon||world.box?world.iconSize*.5f+5:0),screen.Y-world.size*.5f),color,label.c_str(),world.size);
            if(world.health&&healthFresh) {
                const float width=(std::max)(24.f,world.iconSize),left=screen.X-width*.5f;
                const float top=screen.Y+(world.icon||world.box?world.iconSize*.5f:world.size*.5f)+4;
                draw->AddRectFilled(ImVec2(left-1,top-1),ImVec2(left+width+1,top+4),IM_COL32(0,0,0,190),1);
                draw->AddRectFilled(ImVec2(left,top),ImVec2(left+width*std::clamp(object.health/object.health_max,0.f,1.f),top+3),color);
            }
        }
    }



} // namespace

void V2::Visuals::DrawFrame()
{
    Diagnostics::RecordFrame();
    const bool boxPerfMode = OW::Config::boxPerfMode;
    Diagnostics::BeginRenderWorkloadFrame(boxPerfMode);

    // The menu is rendered by the separate overlay menu window. This callback
    // only draws the transparent full-screen canvas layer.
    OW::ScopedConsumptionFrame frameScope;
    const auto& frame=frameScope.Get();
    const bool entityListEmpty=frame.entities.empty();
    bool playerInfoCalled=false, skillInfoCalled=false;
    std::vector<OW::c_entity> presented;
    if(OW::PresentUseForRenderEnabled())presented=OW::TargetingDetail::SnapshotPresentRenderEntities();
    auto frameEntitySnapshot = [&]() -> const std::vector<OW::c_entity>& { return presented.empty()?frame.entities:presented; };
    auto frameDynamicEntitySnapshot = [&]() -> const std::vector<OW::hpanddy>& { return frame.dynamic; };
    auto frameLocalSnapshot = [&]() -> const OW::c_entity& { return frame.local; };

    if (boxPerfMode) {
        if (!entityListEmpty) {
            playerInfoCalled = true;
            PlayerInfoFromSnapshot(frameEntitySnapshot(), frameLocalSnapshot(), true);
        } else {
            Diagnostics::PlayerInfoStats emptyPlayerInfoStats{};
            emptyPlayerInfoStats.boxPerfMode = true;
            emptyPlayerInfoStats.fastBoxPath = OW::Config::boxPerfFastRect;
            Diagnostics::SetPlayerInfoStats(emptyPlayerInfoStats);
        }

        Diagnostics::SetRenderPipelineStatus(false, playerInfoCalled, false, entityListEmpty);
        return;
    }

    const bool viewMatricesFresh = OW::HasFreshViewMatrices();
    if (!entityListEmpty && viewMatricesFresh && OW::Config::radar)
        DrawRadar(frameEntitySnapshot(), frameLocalSnapshot());

    if (!entityListEmpty) {
        playerInfoCalled = true;
        PlayerInfoFromSnapshot(frameEntitySnapshot(), frameLocalSnapshot(), false);
        if (OW::Config::ult || (OW::Config::skillinfo && OW::Config::skillDisplayMode != 0)) {
            skillInfoCalled = true;
            skillinfo(frameEntitySnapshot());
        }
    } else {
        Diagnostics::PlayerInfoStats emptyPlayerInfoStats{};
        Diagnostics::SetPlayerInfoStats(emptyPlayerInfoStats);
    }

    DrawAimTriggerStatusPanel();
    DrawOverlayInfo(frameEntitySnapshot(), frameDynamicEntitySnapshot().size());
    if (viewMatricesFresh && V2::HeroSettings::Snapshot()->display.world.enabled)
        DrawHealthPacks(frameDynamicEntitySnapshot());
    DrawFovCircle(frameLocalSnapshot());
    if (OW::Config::drawTrackingDeadzones)
        DrawTrackingDeadzoneCircles(frameLocalSnapshot());
    DrawCrosshair();
    Diagnostics::SetRenderPipelineStatus(true, playerInfoCalled, skillInfoCalled, entityListEmpty);

    static DWORD lastRenderPipelineLogTick = 0;
    if (OW::Config::outputDebugLog) {
        const DWORD now = GetTickCount();
        if (lastRenderPipelineLogTick == 0 || now - lastRenderPipelineLogTick >= 1000) {
            Diagnostics::Info("[PIPELINE] Stage 5 render callback mode=normal DrawRadar=1 PlayerInfo=%d skillinfo=%d entities_empty=%d.",
                playerInfoCalled ? 1 : 0,
                skillInfoCalled ? 1 : 0,
                entityListEmpty ? 1 : 0);
            lastRenderPipelineLogTick = now;
        }
    }
    DrawPipelineDiagnostics();
    DrawDiagnosticLogOverlay();
}
