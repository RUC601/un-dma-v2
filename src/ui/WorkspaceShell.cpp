#include "WorkspaceShell.hpp"
#include "Theme.hpp"
#include <algorithm>
#include <cfloat>
#include <cstring>

namespace UI::Workspace {
void Render(Model& model, const std::function<void()>& save,
            const std::function<void(int)>& body,
            const std::function<void(ImDrawList*, ImVec2, float)>& footer,
            const std::function<void(float, float)>& header) {
    const auto* viewport = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(viewport->Pos);
    ImGui::SetNextWindowSize(viewport->Size);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(16, 12));
    if (ImGui::Begin("UNDMA V2##workspace", nullptr,
                     ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove |
                     ImGuiWindowFlags_NoSavedSettings)) {
        const float width = ImGui::GetContentRegionAvail().x;
        const float toolbarHeight = (std::max)(44.0f, ImGui::GetFontSize() * 2.0f + 12.0f);
        const float titleSize = ImGui::GetFontSize() * 2.0f;
        ImFont* titleFont = Theme::s_titleFont ? Theme::s_titleFont : ImGui::GetFont();
        const ImVec2 titleExtent = titleFont->CalcTextSizeA(titleSize, FLT_MAX, 0, "Unleashed");
        const float iconSize = toolbarHeight - 8.0f;
        const float iconWidth = model.brandIcon ? iconSize + 12.0f : 0.0f;
        const ImVec2 brandPos = ImGui::GetCursorScreenPos();
        ImGui::Dummy(ImVec2(iconWidth + titleExtent.x, toolbarHeight));
        auto* draw = ImGui::GetWindowDrawList();
        if (model.brandIcon)
            draw->AddImage(model.brandIcon, ImVec2(brandPos.x, brandPos.y + 4),
                ImVec2(brandPos.x + iconSize, brandPos.y + 4 + iconSize));
        draw->AddText(titleFont, titleSize,
            ImVec2(brandPos.x + iconWidth, brandPos.y + (toolbarHeight - titleExtent.y) * .5f),
            ImGui::GetColorU32(ImGuiCol_Text), "Unleashed");
        const float saveWidth=(std::max)(148.0f,ImGui::CalcTextSize(model.saveLabel).x+32.0f);
        if (header) header(saveWidth, toolbarHeight);
        const float saveX = ImGui::GetWindowContentRegionMax().x - saveWidth;
        if (ImGui::GetItemRectMax().x + ImGui::GetStyle().ItemSpacing.x <= ImGui::GetWindowPos().x + saveX)
            ImGui::SameLine(saveX);
        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(.53f,.10f,.18f,1));
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(.66f,.12f,.23f,1));
        ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(.43f,.08f,.15f,1));
        ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 6.0f);
        if (ImGui::Button(model.saveLabel, ImVec2(saveWidth, toolbarHeight))) save();
        ImGui::PopStyleVar();
        ImGui::PopStyleColor(3);
        if (!model.status.empty()) {
            if (model.saveFailed) ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1,.65f,.5f,1));
            ImGui::TextWrapped("%s", model.status.c_str());
            if (model.saveFailed) ImGui::PopStyleColor();
        }
        ImGui::Spacing();
        ImGui::Dummy(ImVec2(0, 6));
        const bool stacked = width < 600;
        if(stacked) {
            ImGui::SetNextItemWidth(-1);
            if(ImGui::BeginCombo("##PageNavigation", model.pages[model.selected].label)) {
                for(int i=0;i<static_cast<int>(model.pages.size());++i)
                    if(ImGui::Selectable(model.pages[i].label, model.selected==i)) model.selected=i;
                ImGui::EndCombo();
            }
        }
        const float footerHeight = (std::max)(30.0f, ImGui::GetFontSize()+16.0f);
        const float height = (std::max)(100.0f, ImGui::GetContentRegionAvail().y - footerHeight - 8);
        const bool compact = width < 760;
        float navWidth = compact ? 112.0f : 164.0f;
        for(const auto& page:model.pages)navWidth=(std::max)(navWidth,ImGui::CalcTextSize(page.label).x+20);
        navWidth=(std::min)(navWidth,width*.35f);
        if(!stacked) {
        ImGui::BeginChild("BusinessNavigation", ImVec2(navWidth, height));
        const char* group = nullptr;
        for (int i = 0; i < static_cast<int>(model.pages.size()); ++i) {
            const auto& page = model.pages[i];
            if (!group || std::strcmp(group, page.group) != 0) {
                if (group) ImGui::Dummy(ImVec2(0, 6));
                ImGui::TextDisabled("%s", page.group);
                ImGui::Spacing();
                group = page.group;
            }
            ImGui::PushID(i);
            const bool selected = model.selected == i;
            if (selected) ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(.36f,.10f,.16f,1));
            if (ImGui::Button(page.label, ImVec2(-1, (std::max)(24.0f,ImGui::GetFontSize()+8)))) model.selected = i;
            if (selected) {
                const auto min = ImGui::GetItemRectMin(), max = ImGui::GetItemRectMax();
                ImGui::GetWindowDrawList()->AddRectFilled(min, ImVec2(min.x+3,max.y), IM_COL32(229,108,126,255));
                ImGui::PopStyleColor();
            }
            ImGui::PopID();
        }
        ImGui::EndChild();
        ImGui::SameLine(0,16);
        }
        ImGui::BeginChild("PageBody", ImVec2(0,height));
        body(model.selected);
        ImGui::EndChild();
        const auto pos = ImGui::GetCursorScreenPos();
        footer(ImGui::GetWindowDrawList(), pos, width);
    }
    ImGui::End();
    ImGui::PopStyleVar();
}
}
