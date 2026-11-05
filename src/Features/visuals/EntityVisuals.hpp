#pragma once
#include "Game/Entity.hpp"
#include <imgui.h>
#include <vector>
#include <string>
namespace OverlayRenderDetail {
    struct AimTriggerStatusRow {
        std::string text;
        ImU32 color = IM_COL32_WHITE;
        float fontSize = 11.5f;
    };


std::string CurrentLocalHeroName(uint64_t heroId);
std::vector<AimTriggerStatusRow> BuildAimTriggerStatusRowsForHero(uint64_t heroId);
float AimTriggerStatusPanelHeight(const std::vector<AimTriggerStatusRow>& rows);
// Caller resolves body freshness; drawing never reads the target process.
float DrawHeroStatus(const OW::c_entity& entity, float x, float y, int style, bool live);
}
// 渲染线程独占投影平滑与图标缓存。调用者传入本帧快照，不持有采集锁绘制。
float DrawAimTriggerStatusPanel();
void PlayerInfoFromSnapshot(const std::vector<OW::c_entity>& entities, const OW::c_entity& local, bool boxPerfMode = false);
void PlayerInfo(bool boxPerfMode = false);
void skillinfo(const std::vector<OW::c_entity>& entities);
void skillinfo();
void DrawHeroRadarMarker(const OW::c_entity& entity, float x, float y, ImU32 border, float scale = 1);
