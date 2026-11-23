// Included after the shared icon/text helpers; the render thread owns all caches.
namespace OverlayRenderDetail {
    // Hero status layout is in game/source pixels. Map every primitive once so
    // the avatar, resources, skills, text and ultimate ring share one space.
    struct HudTransform {
        float originCanvasX = 0.0f;
        float originCanvasY = 0.0f;
        float sourceToCanvasX = 1.0f;
        float sourceToCanvasY = 1.0f;
        float sourceToCanvasUniform = 1.0f;
        float itemScale = 1.0f;

        ImVec2 Point(float x, float y) const {
            return ImVec2(
                originCanvasX + x * itemScale * sourceToCanvasX,
                originCanvasY + y * itemScale * sourceToCanvasY);
        }

        float Uniform(float value) const { return value * itemScale * sourceToCanvasUniform; }
    };

    inline void HudCenteredText(const HudTransform& transform, float x, float y,
                                const std::string& text, float size, ImU32 color) {
        const float fontSize = transform.Uniform(size);
        const ImVec2 extent = ImGui::GetFont()->CalcTextSizeA(fontSize, FLT_MAX, 0, text.c_str());
        const ImVec2 center = transform.Point(x, y);
        ImGui::GetForegroundDrawList()->AddText(
            ImGui::GetFont(), fontSize,
            ImVec2(center.x - extent.x * .5f, center.y - extent.y * .5f),
            color, text.c_str());
        Diagnostics::RecordRenderPrimitive(Diagnostics::RenderPrimitiveKind::Text);
    }

    float DrawHeroStatus(const OW::c_entity& entity, float x, float y, int style, bool live,
                         const HudTransform& transform) {
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
        auto* draw = ImGui::GetForegroundDrawList();

        if (auto* avatar = FindEntityHeroIcon(entity)) {
            draw->AddImage(reinterpret_cast<ImTextureID>(avatar),
                transform.Point(x, y), transform.Point(x + layout.avatar, y + layout.avatar),
                ImVec2(0, 0), ImVec2(1, 1), white);
            Diagnostics::RecordRenderPrimitive(Diagnostics::RenderPrimitiveKind::Icon);
        } else {
            draw->AddRectFilled(transform.Point(x, y),
                transform.Point(x + layout.avatar, y + layout.avatar), ImColor(32, 36, 44));
            Diagnostics::RecordRenderPrimitive(Diagnostics::RenderPrimitiveKind::FilledRect);
            HudCenteredText(transform, x + layout.avatar * .5f, y + layout.avatar * .5f, "?", 22, white);
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
                draw->AddImage(reinterpret_cast<ImTextureID>(texture),
                    transform.Point(sx + 1, sy + 1), transform.Point(sx + layout.skill - 1, sy + layout.skill - 1),
                    ImVec2(0, 0), ImVec2(1, 1),
                    ImU32WithAlpha(255, 255, 255, opacity * (known && !cooling ? 1.0f : .24f)));
                Diagnostics::RecordRenderPrimitive(Diagnostics::RenderPrimitiveKind::Icon);
            }
            if (!known || cooling || !texture) {
                const std::string text = !known ? "--" : cooling ? FormatIconCooldown(cooldown) : "?";
                HudCenteredText(transform, sx + layout.skill * .5f, sy + layout.skill * .5f, text,
                    custom ? 13.0f : 11.0f, white);
            }
        }

        const float maxHealth = OW::OverlayStatus::Positive(entity.PlayerHealthMax) > 0
            ? entity.PlayerHealthMax : entity.MaxHealth + entity.MaxArmorHealth + entity.MaxBarrierHealth;
        const auto health = OW::OverlayStatus::ComposeHealth(entity.MinHealth, entity.MinArmorHealth,
            entity.MinBarrierHealth, maxHealth, entity.PlayerHealth);
        const float hy = y + layout.healthY;
        draw->AddRectFilled(transform.Point(x, hy),
            transform.Point(x + layout.barWidth, hy + layout.barHeight), ImColor(37, 41, 51));
        Diagnostics::RecordRenderPrimitive(Diagnostics::RenderPrimitiveKind::FilledRect);
        float hx = x;
        const ImColor colors[] = {ImColor(237, 240, 245), ImColor(237, 170, 85), ImColor(103, 204, 229)};
        if (live) for (size_t i = 0; i < health.fractions.size(); ++i) {
            const float width = health.fractions[i] * layout.barWidth;
            if (width > 0) draw->AddRectFilled(transform.Point(hx, hy),
                transform.Point(hx + width, hy + layout.barHeight),
                custom ? colors[i] : ImColor(105, 204, 53));
            if (width > 0) Diagnostics::RecordRenderPrimitive(Diagnostics::RenderPrimitiveKind::FilledRect);
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
            const ImVec2 center = transform.Point(
                x + layout.ultimateX + layout.ultimate * .5f,
                y + layout.avatar * .5f);
            const float radius = transform.Uniform(layout.ultimate * .5f - 4);
            const float stroke = transform.Uniform(3.5f);
            draw->AddCircle(center, radius, IM_COL32(54, 59, 71, 255), 64, stroke);
            if (ultimate > 0) {
                constexpr float pi = 3.14159265f;
                draw->PathArcTo(center, radius, -pi * .5f, -pi * .5f + 2 * pi * ultimate / 100, 64);
                draw->PathStroke(ultimateColor, 0, stroke);
            }
            HudCenteredText(transform,
                x + layout.ultimateX + layout.ultimate * .5f,
                y + layout.avatar * .5f,
                showUltimate ? std::to_string(int(std::lround(animation.charge.percent))) : "--", 25,
                ultimateKnown ? white : ImU32WithAlpha(237,240,245,opacity*.6f));
            HudCenteredText(transform, x + layout.ultimateX + layout.ultimate * .5f,
                hy + layout.barHeight * .5f,
                live ? std::to_string(int(std::lround(health.total))) : dead ? "0" : "--", 22, white);
        } else {
            draw->AddRectFilled(transform.Point(x, hy + 8),
                transform.Point(x + layout.barWidth, hy + 12), ImColor(37, 41, 51));
            Diagnostics::RecordRenderPrimitive(Diagnostics::RenderPrimitiveKind::FilledRect);
            if (showUltimate) {
                draw->AddRectFilled(transform.Point(x, hy + 8),
                    transform.Point(x + layout.barWidth * ultimate / 100, hy + 12), ImColor(164, 49, 239));
                Diagnostics::RecordRenderPrimitive(Diagnostics::RenderPrimitiveKind::FilledRect);
            }
        }
        if (dead) HudCenteredText(transform, x + layout.avatar * .5f, y + layout.avatar * .5f, "DEAD", 13, white);
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
    const float height = OW::ScreenHeight() > 0 ? OW::ScreenHeight() : OW::ResolveScreenHeight();
    const float width = OW::ScreenWidth() > 0 ? OW::ScreenWidth() : OW::ResolveScreenWidth();
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
    const ImVec2 canvasSize = ImGui::GetIO().DisplaySize;
    const float canvasWidth = canvasSize.x > 0.0f ? canvasSize.x : width;
    const float canvasHeight = canvasSize.y > 0.0f ? canvasSize.y : height;
    const float sourceToCanvasX = canvasWidth / width;
    const float sourceToCanvasY = canvasHeight / height;
    const OverlayRenderDetail::HudTransform transform{
        x * sourceToCanvasX,
        y * sourceToCanvasY,
        sourceToCanvasX,
        sourceToCanvasY,
        (sourceToCanvasX + sourceToCanvasY) * .5f,
        scale};
    draw->PushClipRect(ImVec2(0, 0), ImVec2(canvasWidth, canvasHeight), false);
    for (int i=0; i<int(visible.size()); ++i) {
        const auto& entity = *visible[i];
        const bool live = entity.roster_state == OW::EntityRosterState::Fresh &&
            entity.HasUsableBody(GetTickCount(), OW::EntitySceneGeneration());
        OverlayRenderDetail::DrawHeroStatus(entity, (i/rows)*(layout.width+16),
            (i%rows)*layout.height, OW::Config::heroStatusStyle, live, transform);
    }
    draw->PopClipRect();
}

void skillinfo() { skillinfo(OW::TargetingDetail::SnapshotEntities()); }
