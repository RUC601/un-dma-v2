    std::vector<AimTriggerStatusRow> BuildAimTriggerStatusRowsForHero(uint64_t heroId) {
        OW::Config::ScopedConfigRead configRead;
        std::vector<AimTriggerStatusRow> rows{{"Slot", IM_COL32(230,235,245,245), 12.0f}};
        constexpr const char* behaviors[] = {"Tracking", "Flick", "Flick2nd", "Reacquire", "Magnetic Trigger"};
        auto keyName = [](int key) -> const char* {
            switch (OW::Labels::AimActivationKeyVk(key)) {
            case VK_LBUTTON: return "LMB";
            case VK_RBUTTON: return "RMB";
            case VK_XBUTTON1: return "M4";
            case VK_XBUTTON2: return "M5";
            default: return OW::Labels::AimActivationKeyName(key);
            }
        };
        auto append = [&](const char* behavior, int key, int index, bool enabled, bool always = false) {
            const int vk = OW::Labels::AimActivationKeyVk(key);
            const bool held = enabled && (always || (vk > 0 && KeyState::IsKeyDown(vk)));
            char line[160]{};
            std::snprintf(line, sizeof(line), "%s  [%s]  Slot %d", behavior,
                always ? "Always" : keyName(key), index + 1);
            rows.push_back({line, held ? IM_COL32(90,235,105,255) : enabled ?
                IM_COL32(230,235,245,235) : IM_COL32(155,162,176,160), 11.5f});
        };
        for (int i = 0; i < OW::Config::kMaxHeroPresetSlots; ++i) {
            OW::Config::HeroSlotPreset slot{};
            if (OW::Config::TryGetHeroAimSlot(heroId, i, slot)) {
                append(behaviors[OW::Config::ClampAimBehaviorIndex(slot.preset.aimBehavior)],
                    slot.preset.key, i, slot.enabled);
            }
            if (OW::Config::TryGetHeroTriggerSlot(heroId, i, slot)) {
                append("Trigger", slot.preset.trigger.key, i,
                    slot.enabled && slot.preset.trigger.enabled, slot.preset.trigger.mode == 2);
            }
        }
        if (OW::Config::secondaim) append(OW::Config::Flick2 ? "Flick" : "Tracking", OW::Config::aim_key2, 1, true);
        if (OW::Config::View_triggerbot2()) append("Trigger", OW::Config::triggerbotKey2, 1, true, OW::Config::triggerbotMode2 == 2);
        if (rows.size() == 1) rows.push_back({heroId ? "No configured slots" : "Waiting for local player", IM_COL32(176,184,196,200), 11.5f});
        return rows;
    }

    float AimTriggerStatusPanelHeight(const std::vector<AimTriggerStatusRow>& rows) {
        float height = 0;
        for (const auto& row : rows) height += row.fontSize + 3;
        return height;
    }

    float DrawAimTriggerStatusPanel() {
        if (!OW::Config::showSlotStatus) return 0;
        const auto rows = BuildAimTriggerStatusRowsForHero(OW::SnapshotLocalHeroIdForStatus());
        float width = 0;
        for (const auto& row : rows) width = (std::max)(width,
            ImGui::GetFont()->CalcTextSizeA(row.fontSize,FLT_MAX,0,row.text.c_str()).x);
        const float x = OW::OverlayStatus::Position(OW::Config::slotPosX, OW::WX, width);
        float y = OW::OverlayStatus::Position(OW::Config::slotPosY, OW::WY, AimTriggerStatusPanelHeight(rows));
        for (const auto& row : rows) {
            Render::DrawText(ImVec2(x,y), row.color, row.text.c_str(), row.fontSize);
            y += row.fontSize + 3;
        }
        return y;
    }
