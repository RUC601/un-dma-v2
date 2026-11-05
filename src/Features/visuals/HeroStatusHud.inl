// Included after the shared icon/text helpers; the render thread owns all caches.
namespace OverlayRenderDetail {
    inline void HudCenteredText(float x, float y, const std::string& text, float size, ImU32 color) {
        const ImVec2 extent = ImGui::GetFont()->CalcTextSizeA(size, FLT_MAX, 0, text.c_str());
        Render::DrawText(ImVec2(x - extent.x * .5f, y - extent.y * .5f), color, text.c_str(), size);
    }

    float DrawHeroStatus(const OW::c_entity& entity, float x, float y, int style, bool live) {
        const auto layout = OW::OverlayStatus::GetLayout(style);
        const bool custom = style != 0;
        const DWORD now = GetTickCount();
        const bool dead = entity.roster_state == OW::EntityRosterState::Dead;
        const float opacity = live ? 1.0f : .42f;
        const ImU32 white = ImU32WithAlpha(237, 240, 245, opacity);
        const std::string name = EntityHeroName(entity);
        const auto* icons = OW::GetAbilityIcons(name);
        IconManager* manager = Render::GetIconManager();
        if (icons) EnsureAbilityIconsLoaded(manager, *icons);

        if (auto* avatar = FindEntityHeroIcon(entity)) {
            Render::DrawIcon(avatar, ImVec2(x, y), ImVec2(layout.avatar, layout.avatar), white);
        } else {
            Render::DrawFilledRect(Vector2(x, y), layout.avatar, layout.avatar, ImColor(32, 36, 44));
            HudCenteredText(x + layout.avatar * .5f, y + layout.avatar * .5f, "?", 22, white);
        }

        const char* abilities[] = {icons ? icons->ability1Icon : nullptr,
            icons ? icons->ability2Icon : nullptr, icons ? icons->ability3Icon : nullptr};
        for (size_t slot = 0; slot < 3; ++slot) {
            if (slot == 2 && !abilities[slot]) continue;
            const float sx = x + layout.skillX, sy = y + slot * layout.skill;
            // shortcut: third-ability icon is mapped, but its per-hero CD source remains unknown.
            const bool known = !dead && slot < entity.display_cooldowns.size() &&
                entity.display_cooldowns[slot].Fresh(now);
            const float cooldown = known ? entity.display_cooldowns[slot].Remaining() : 0;
            const bool cooling = known && cooldown > .05f;
            ID3D11ShaderResourceView* texture = icons && manager && abilities[slot]
                ? manager->GetIcon(std::string(icons->heroSlug) + "/" + abilities[slot]) : nullptr;
            if (texture) {
                Render::DrawIcon(texture, ImVec2(sx + 1, sy + 1),
                    ImVec2(layout.skill - 2, layout.skill - 2),
                    ImU32WithAlpha(255, 255, 255, opacity * (known && !cooling ? 1.0f : .24f)));
            }
            if (!known || cooling || !texture) {
                const std::string text = !known ? "--" : cooling ? FormatIconCooldown(cooldown) : "?";
                HudCenteredText(sx + layout.skill * .5f, sy + layout.skill * .5f, text,
                    custom ? 13.0f : 11.0f, white);
            }
        }

        const float maxHealth = OW::OverlayStatus::Positive(entity.PlayerHealthMax) > 0
            ? entity.PlayerHealthMax : entity.MaxHealth + entity.MaxArmorHealth + entity.MaxBarrierHealth;
        const auto health = OW::OverlayStatus::ComposeHealth(entity.MinHealth, entity.MinArmorHealth,
            entity.MinBarrierHealth, maxHealth, entity.PlayerHealth);
        const float hy = y + layout.healthY;
        Render::DrawFilledRect(Vector2(x, hy), layout.barWidth, layout.barHeight, ImColor(37, 41, 51));
        float hx = x;
        const ImColor colors[] = {ImColor(237, 240, 245), ImColor(237, 170, 85), ImColor(103, 204, 229)};
        if (live) for (size_t i = 0; i < health.fractions.size(); ++i) {
            const float width = health.fractions[i] * layout.barWidth;
            if (width > 0) Render::DrawFilledRect(Vector2(hx, hy), width, layout.barHeight,
                custom ? colors[i] : ImColor(105, 204, 53));
            hx += width;
        }

        const bool ultimateKnown = !dead && entity.display_ultimate_tick != 0 &&
            DWORD(now - entity.display_ultimate_tick) <= 1500 && std::isfinite(entity.ultimate);
        struct AnimationEntry {
            OW::OverlayStatus::UltimatePresentation charge;
            double lastSeen = 0;
        };
        static std::unordered_map<uint64_t,AnimationEntry> animations;
        const double frameTime = ImGui::GetTime();
        // Keep charge across the same hero's body replacement; only a valid lower reading spends it.
        auto& animation = animations[entity.roster_key ? entity.roster_key : entity.address];
        const std::array<uint64_t,3> identity{entity.HeroID,
            entity.connection_epoch,entity.scene_generation};
        animation.lastSeen = frameTime;
        const float ultimate = animation.charge.Update(identity,ultimateKnown,entity.ultimate,frameTime);
        const bool showUltimate = animation.charge.known;
        const ImU32 ultimateColor = ImU32WithAlpha(181,212,245,ultimateKnown ? opacity : opacity*.6f);
        static double lastPrune = 0;
        if (frameTime-lastPrune > 30) {
            lastPrune = frameTime;
            for (auto it=animations.begin();it!=animations.end();)
                if (frameTime-it->second.lastSeen > 30) it=animations.erase(it); else ++it;
        }
        if (custom) {
            const ImVec2 center(x + layout.ultimateX + layout.ultimate * .5f, y + layout.avatar * .5f);
            auto* draw = ImGui::GetForegroundDrawList();
            const float radius = layout.ultimate * .5f - 4;
            draw->AddCircle(center, radius, IM_COL32(54, 59, 71, 255), 64, 3.5f);
            if (ultimate > 0) {
                constexpr float pi = 3.14159265f;
                draw->PathArcTo(center, radius, -pi * .5f, -pi * .5f + 2 * pi * ultimate / 100, 64);
                draw->PathStroke(ultimateColor, 0, 3.5f);
            }
            HudCenteredText(center.x, center.y, showUltimate ? std::to_string(int(std::lround(animation.charge.percent))) : "--", 25,
                ultimateKnown ? white : ImU32WithAlpha(237,240,245,opacity*.6f));
            HudCenteredText(center.x, hy + layout.barHeight * .5f,
                live ? std::to_string(int(std::lround(health.total))) : dead ? "0" : "--", 22, white);
        } else {
            Render::DrawFilledRect(Vector2(x, hy + 8), layout.barWidth, 4, ImColor(37, 41, 51));
            if (showUltimate) Render::DrawFilledRect(Vector2(x, hy + 8), layout.barWidth * ultimate / 100, 4,
                ImColor(164, 49, 239));
        }
        if (dead) HudCenteredText(x + layout.avatar * .5f, y + layout.avatar * .5f, "DEAD", 13, white);
        return y + layout.height;
    }
}

void DrawHeroRadarMarker(const OW::c_entity& entity, float x, float y, ImU32 border, float scale) {
    const float radius = 9 * scale;
    if (auto* avatar = OverlayRenderDetail::FindEntityHeroIcon(entity)) {
        auto* draw = ImGui::GetForegroundDrawList();
        draw->AddImageRounded((ImTextureID)avatar, ImVec2(x-radius, y-radius), ImVec2(x+radius, y+radius),
            ImVec2(0,0), ImVec2(1,1), IM_COL32_WHITE, radius);
        draw->AddCircle(ImVec2(x,y), radius, border, 24, 1.2f);
    } else {
        const ImVec4 c = ImGui::ColorConvertU32ToFloat4(border);
        Render::DrawFilledCircle(Vector2(x,y), 3.5f,
            Render::Color(int(c.x*255), int(c.y*255), int(c.z*255), int(c.w*255)), 16);
    }
}

void skillinfo(const std::vector<OW::c_entity>& entities) {
    if (!OW::Config::ult) return;
    const auto layout = OW::OverlayStatus::GetLayout(OW::Config::heroStatusStyle);
    const float height = OW::WY > 0 ? OW::WY : OW::ResolveScreenHeight();
    const float width = OW::WX > 0 ? OW::WX : OW::ResolveScreenWidth();
    std::vector<const OW::c_entity*> visible;
    for (const auto& entity : entities) {
        if (!entity.team_known || entity.scene_generation != OW::EntitySceneGeneration() ||
            OW::EntityRosterPolicy::Expired(entity.last_discovered_tick_ms, GetTickCount()) ||
            !OW::EntityRosterPolicy::ShouldRenderUltimateRosterEntry(OW::Config::ultimateRosterFilter, entity.Team) ||
            !OW::GetAbilityIcons(OverlayRenderDetail::EntityHeroName(entity))) continue;
        visible.push_back(&entity);
    }
    if (visible.empty()) return;
    const float scale = OW::Config::heroStatusScale;
    const int rows = (std::max)(1, int(height / (layout.height * scale)));
    const int columns = (int(visible.size()) + rows - 1) / rows;
    const float usedHeight = (std::min)(int(visible.size()),rows)*layout.height*scale;
    const float usedWidth = (columns*(layout.width+16)-16)*scale;
    const float x = OW::OverlayStatus::Position(OW::Config::heroStatusPosX,width,usedWidth);
    const float y = OW::OverlayStatus::Position(OW::Config::heroStatusPosY,height,usedHeight);
    auto* draw = ImGui::GetForegroundDrawList();
    draw->PushClipRect(ImVec2(0,0),ImVec2(width/scale+layout.width,height/scale+layout.height),false);
    const int start = draw->VtxBuffer.Size;
    for (int i=0; i<int(visible.size()); ++i) {
        const auto& entity = *visible[i];
        const bool live = entity.roster_state == OW::EntityRosterState::Fresh &&
            entity.HasUsableBody(GetTickCount(), OW::EntitySceneGeneration());
        OverlayRenderDetail::DrawHeroStatus(entity, (i/rows)*(layout.width+16),
            (i%rows)*layout.height, OW::Config::heroStatusStyle, live);
    }
    draw->PopClipRect();
    for (int i=start;i<draw->VtxBuffer.Size;++i) {
        auto& p=draw->VtxBuffer[i].pos;
        p=ImVec2(x+p.x*scale,y+p.y*scale);
    }
}

void skillinfo() { skillinfo(OW::TargetingDetail::SnapshotEntities()); }
