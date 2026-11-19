#include "SlotSelector.hpp"
#include "ui/EditorSupport.hpp"
#include <cstdio>
#include <imgui.h>
#include <algorithm>

namespace UI::Slots {
Command Render(std::span<const Item> items, int selected, int limit, bool chinese) {
    Command command;
    ImGui::AlignTextToFramePadding();
    ImGui::TextDisabled("%s", chinese ? "方案" : "Scheme");
    ImGui::SameLine(0, 10);
    const float available=ImGui::GetContentRegionAvail().x;
    const float buttonWidth=(std::max)(38.0f,ImGui::CalcTextSize("12 *").x+14.0f);
    const float height=ControlHeight();
    const int perRow=(std::max)(1, static_cast<int>((available-76)/(buttonWidth+6)));
    for(int i=0;i<static_cast<int>(items.size());++i) {
        if(i%perRow)ImGui::SameLine(0,6);
        const auto& item=items[i];
        ImGui::PushID(std::to_string(item.id).c_str());
        if(i==selected)ImGui::PushStyleColor(ImGuiCol_Button,ImVec4(.53f,.04f,.14f,1));
        const auto label=std::to_string(i+1)+(item.enabled?" *":"");
        if(ImGui::Button(label.c_str(),ImVec2(buttonWidth,height)))command={Action::Select,i};
        ImGui::SetItemTooltip("%s · %s",item.name.c_str(),
            chinese?(item.enabled?"已启用":"已停用"):(item.enabled?"Enabled":"Disabled"));
        if(i==selected)ImGui::PopStyleColor();
        ImGui::PopID();
    }
    // 极窄可用区域时另起一行，保证增删按钮仍在滚动区域内。
    if(!items.empty() && ImGui::GetContentRegionAvail().x>=76)ImGui::SameLine(0,8);
    const bool atLimit = static_cast<int>(items.size()) >= limit;
    ImGui::BeginDisabled(atLimit);
    if(ImGui::Button("+##AddScheme",ImVec2(30,height)))command={Action::Add,selected};
    ImGui::EndDisabled();
    if (atLimit) {
        char hint[96]{};
        std::snprintf(hint, sizeof(hint), chinese ? "已达到 %d 个方案上限" : "Maximum of %d schemes", limit);
        SettingTooltip(hint);
    } else {
        SettingTooltip(chinese ? "新增方案：复制参数，清空绑定并停用" : "Add scheme: copy settings, clear bindings and disable");
    }
    ImGui::SameLine(0,6);
    const bool canDelete = items.size() > 1;
    ImGui::BeginDisabled(!canDelete);
    if (canDelete) ImGui::PushStyleColor(ImGuiCol_Border,ImVec4(1.0f,.55f,.45f,1));
    if(ImGui::Button("-##DeleteScheme",ImVec2(30,height)))command={Action::Remove,selected};
    if (canDelete) ImGui::PopStyleColor();
    ImGui::EndDisabled();
    SettingTooltip(items.size() <= 1
        ? (chinese ? "至少保留一个方案" : "Keep at least one scheme")
        : (chinese ? "删除当前方案" : "Delete selected scheme"));
    ImGui::Spacing();
    return command;
}
}
