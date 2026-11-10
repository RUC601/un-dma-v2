#include "Search.hpp"
#include "runtime/LauncherBridge.hpp"
#include "WorkspaceShell.hpp"
#include "Theme.hpp"
#include "config/UiPreferences.hpp"
#include "Utils/Config.hpp"
#include <imgui_internal.h>
#include <algorithm>
#include <cstring>

namespace UI::Workspace {
namespace {
void StatusToast(const Model& model, float scale) {
    if (model.status.empty()) return;
    const float fadeIn = std::clamp(model.statusAge / .20f, 0.f, 1.f);
    const float fadeOut = model.saveFailed ? 1.f : std::clamp((4.f - model.statusAge) / .15f, 0.f, 1.f);
    const float alpha = (std::min)(fadeIn, fadeOut);
    if (alpha <= 0.f) return;

    const auto* viewport = ImGui::GetMainViewport();
    const float margin = 16.f * scale;
    const float width = (std::min)(300.f * scale, viewport->WorkSize.x - margin * 2);
    const float textWidth = (std::max)(1.f, width - 58.f * scale);
    const auto textSize = ImGui::CalcTextSize(model.status.c_str(), nullptr, false, textWidth);
    const float height = (std::max)(48.f * scale, textSize.y + 28.f * scale);
    const ImVec2 pos(viewport->WorkPos.x + viewport->WorkSize.x - margin - width,
                     viewport->WorkPos.y + viewport->WorkSize.y - margin - height);
    const ImVec2 end(pos.x + width, pos.y + height);
    auto color = [alpha](ImU32 value) {
        auto rgba = ImGui::ColorConvertU32ToFloat4(value);
        rgba.w *= alpha;
        return ImGui::GetColorU32(rgba);
    };
    // Foreground drawing keeps save feedback outside the layout and mouse hit testing.
    auto* draw = ImGui::GetForegroundDrawList();
    const ImU32 accent = model.saveFailed ? IM_COL32(255, 145, 110, 255) : Theme::kColAccent;
    draw->AddRectFilled(ImVec2(pos.x, pos.y + 3.f * scale), ImVec2(end.x, end.y + 3.f * scale),
                        color(IM_COL32(0, 0, 0, 55)), 8.f * scale);
    draw->AddRectFilled(pos, end, color(Theme::kColPanel), 8.f * scale);
    draw->AddRect(pos, end, color(Theme::kColStroke), 8.f * scale);
    const ImVec2 badge(pos.x + 23.f * scale, pos.y + height * .5f);
    draw->AddCircleFilled(badge, 11.f * scale, color(Theme::kColControl), 24);
    if (model.saveFailed) {
        draw->AddLine(ImVec2(badge.x, badge.y - 5.f * scale), ImVec2(badge.x, badge.y + scale), color(accent), 1.5f * scale);
        draw->AddCircleFilled(ImVec2(badge.x, badge.y + 5.f * scale), scale, color(accent));
    } else {
        draw->AddLine(ImVec2(badge.x - 5.f * scale, badge.y), ImVec2(badge.x - scale, badge.y + 4.f * scale), color(accent), 1.5f * scale);
        draw->AddLine(ImVec2(badge.x - scale, badge.y + 4.f * scale), ImVec2(badge.x + 6.f * scale, badge.y - 4.f * scale), color(accent), 1.5f * scale);
    }
    draw->AddText(ImGui::GetFont(), ImGui::GetFontSize(), ImVec2(pos.x + 44.f * scale, pos.y + (height - textSize.y) * .5f),
                  color(Theme::kColTextMuted), model.status.c_str(), nullptr, textWidth);
    if (!model.saveFailed) {
        const float remaining = std::clamp(1.f - model.statusAge / 4.f, 0.f, 1.f);
        draw->AddRectFilled(ImVec2(pos.x + 8.f * scale, end.y - 3.f * scale),
                            ImVec2(pos.x + 8.f * scale + (width - 16.f * scale) * remaining, end.y - scale), color(accent), scale);
    }
}

void Icon(ImDrawList* draw, ImVec2 p, int index, ImU32 color) {
    const float x=p.x+9, y=p.y+9;
    if(index==0) {
        draw->AddCircle(ImVec2(x,y),6,color,20,1.3f);draw->AddCircleFilled(ImVec2(x,y),1.5f,color);
        draw->AddLine(ImVec2(x-9,y),ImVec2(x+9,y),color,1.2f);draw->AddLine(ImVec2(x,y-9),ImVec2(x,y+9),color,1.2f);
    } else if(index==1) {
        draw->AddRect(ImVec2(x-5,y-8),ImVec2(x+5,y+8),color,5,0,1.3f);draw->AddLine(ImVec2(x,y-7),ImVec2(x,y-1),color,1.3f);
    } else if(index==2) {
        const ImVec2 points[]={{x+2,y-9},{x-6,y+1},{x,y+1},{x-2,y+9},{x+6,y-2},{x,y-2}};draw->AddPolyline(points,6,color,ImDrawFlags_Closed,1.5f);
    } else if(index==3) {
        for(int i=0;i<3;++i){const float xx=x-7+i*7;draw->AddRect(ImVec2(xx-2,y-5),ImVec2(xx+2,y+5),color,1,0,1.3f);if(i<2)draw->AddLine(ImVec2(xx+2,y),ImVec2(xx+5,y),color,1.3f);}
    } else if(index==4) {
        draw->AddBezierCubic(ImVec2(x-7,y+5),ImVec2(x-6,y-10),ImVec2(x+6,y-10),ImVec2(x+7,y+5),color,1.3f);
        draw->AddLine(ImVec2(x-8,y+5),ImVec2(x+8,y+5),color,1.3f);draw->AddCircleFilled(ImVec2(x,y+8),1.5f,color);
    } else if(index==5) {
        draw->AddRect(ImVec2(x-8,y-5),ImVec2(x+8,y+5),color,5,0,1.3f);draw->AddCircle(ImVec2(x,y),3,color,16,1.3f);
    } else if(index==6) {
        draw->AddCircle(ImVec2(x,y),8,color,24,1.3f);draw->AddCircleFilled(ImVec2(x-3,y-3),1.5f,color);draw->AddCircleFilled(ImVec2(x+3,y-3),1.5f,color);draw->AddCircleFilled(ImVec2(x-4,y+2),1.5f,color);
    } else if(index==7||index==10) {
        draw->AddCircle(ImVec2(x,y),8,color,24,1.3f);draw->AddCircle(ImVec2(x,y),4,color,20,1.3f);
    } else if(index==15) {
        draw->AddRect(ImVec2(x-9,y-6),ImVec2(x+9,y+6),color,2,0,1.3f);
        for(int row=0;row<2;++row)for(int col=0;col<4;++col)draw->AddCircleFilled(ImVec2(x-6+col*4,y-3+row*5),.8f,color);
    } else {
        for(int row=0;row<3;++row){const float yy=y-6+row*6;draw->AddLine(ImVec2(x-8,yy),ImVec2(x+8,yy),color,1.3f);draw->AddCircleFilled(ImVec2(x+(row==1?4:-3),yy),2.5f,color);}
    }
}

void AccountCard(float width, float height, float scale, bool collapsed, bool zh) {
    const auto pos = ImGui::GetCursorScreenPos();
    const auto& username = V2::Launcher::username;
    const char* name = username.empty() ? "—" : username.c_str();
    ImGui::PushStyleColor(ImGuiCol_Button, Theme::kColControl);
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, Theme::kColControlHover);
    ImGui::PushStyleColor(ImGuiCol_ButtonActive, Theme::kColControlHot);
    if (ImGui::Button("##AccountCard", ImVec2(width, height))) ImGui::OpenPopup("##AccountPopup");
    ImGui::PopStyleColor(3);
    if (collapsed || ImGui::CalcTextSize(name).x > width - 68.0f * scale)
        ImGui::SetItemTooltip("%s\nTill: Lifetime", name);

    auto* draw = ImGui::GetWindowDrawList();
    const float cx = pos.x + (collapsed ? width * .5f : 21.0f * scale);
    const float cy = pos.y + height * .5f;
    const ImU32 avatar = Theme::kColTextDim;
    draw->AddCircleFilled(ImVec2(cx, cy - 5.5f * scale), 5.0f * scale, avatar, 20);
    draw->PathLineTo(ImVec2(cx - 10.0f * scale, cy + 8.0f * scale));
    draw->PathBezierCubicCurveTo(ImVec2(cx - 9.0f * scale, cy - 1.0f * scale),
        ImVec2(cx + 9.0f * scale, cy - 1.0f * scale), ImVec2(cx + 10.0f * scale, cy + 8.0f * scale));
    draw->PathBezierCubicCurveTo(ImVec2(cx + 5.0f * scale, cy + 11.0f * scale),
        ImVec2(cx - 5.0f * scale, cy + 11.0f * scale), ImVec2(cx - 10.0f * scale, cy + 8.0f * scale));
    draw->PathFillConvex(avatar);
    if (!collapsed) {
        const float fontSize = ImGui::GetFontSize();
        const float smallSize = fontSize * .82f;
        const float textY = pos.y + (height - fontSize - smallSize - 2.0f) * .5f;
        const float textX = pos.x + 40.0f * scale;
        draw->PushClipRect(ImVec2(textX, pos.y), ImVec2(pos.x + width - 19.0f * scale, pos.y + height), true);
        draw->AddText(ImVec2(textX, textY), Theme::kColTextMuted, name);
        draw->AddText(ImGui::GetFont(), smallSize, ImVec2(textX, textY + fontSize + 2.0f), Theme::kColTextDim, "Till: Lifetime");
        draw->PopClipRect();
        const float x = pos.x + width - 12.0f * scale;
        draw->AddLine(ImVec2(x - 3.0f * scale, cy - 4.0f * scale), ImVec2(x + scale, cy), Theme::kColTextDim, 1.5f * scale);
        draw->AddLine(ImVec2(x + scale, cy), ImVec2(x - 3.0f * scale, cy + 4.0f * scale), Theme::kColTextDim, 1.5f * scale);
    }

    ImGui::SetNextWindowPos(ImVec2(pos.x, pos.y - 6.0f * scale), ImGuiCond_Appearing, ImVec2(0, 1));
    ImGui::SetNextWindowSize(ImVec2(250.0f * scale, 0), ImGuiCond_Always);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(14.0f * scale, 12.0f * scale));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 6.0f * scale);
    ImGui::PushStyleVar(ImGuiStyleVar_PopupBorderSize, 1.0f);
    ImGui::PushStyleColor(ImGuiCol_PopupBg, Theme::kColPanel);
    ImGui::PushStyleColor(ImGuiCol_Border, Theme::kColStroke);
    if (ImGui::BeginPopup("##AccountPopup")) {
        ImGui::TextDisabled("UNLEASHED");
        ImGui::Spacing();
        ImGui::PushTextWrapPos(0);
        ImGui::TextWrapped("%s", name);
        ImGui::PopTextWrapPos();
        ImGui::TextColored(ImGui::ColorConvertU32ToFloat4(Theme::kColAccent), "Till: Lifetime");
        ImGui::Spacing();
        ImGui::Separator();
        ImGui::BeginDisabled(username.empty());
        if (ImGui::Selectable(zh ? "复制 ID" : "Copy ID")) {
            ImGui::SetClipboardText(username.c_str());
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndDisabled();
        ImGui::EndPopup();
    }
    ImGui::PopStyleColor(2);
    ImGui::PopStyleVar(3);
}
}
void Render(Model& model, const std::function<void()>& save,
            const std::function<void(int)>& body,
            const std::function<void(ImDrawList*, ImVec2, float)>& footer,
            const std::function<void(float, bool)>& header) {
    const bool zh=OW::Config::uiLanguage==1;
    const float scale=(std::max)(1.f,ImGui::GetFontSize()/15.f);
    auto& preferences=Preferences::values;
    const auto* viewport=ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(viewport->Pos);
    ImGui::SetNextWindowSize(viewport->Size);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding,ImVec2(20,16));
    if(ImGui::Begin("UNDMA V2##workspace",nullptr,ImGuiWindowFlags_NoDecoration|ImGuiWindowFlags_NoMove|ImGuiWindowFlags_NoSavedSettings)) {
        auto* draw=ImGui::GetWindowDrawList();
        const auto origin=ImGui::GetWindowPos();
        const auto size=ImGui::GetWindowSize();
        if(preferences.consoleLayers) {
            draw->AddRectFilledMultiColor(origin,ImVec2(origin.x+size.x,origin.y+size.y),
                Theme::kColShell0,Theme::kColShell1,Theme::kColShell2,Theme::kColShell0);
            draw->AddLine(ImVec2(origin.x+20,origin.y+1),ImVec2(origin.x+size.x-20,origin.y+1),IM_COL32(255,255,255,26));
        }
        const float width=ImGui::GetContentRegionAvail().x;
        const auto brand=ImGui::GetCursorScreenPos();
        if(model.brandIcon) draw->AddImage(model.brandIcon,brand,ImVec2(brand.x+34,brand.y+34));
        draw->AddText(Theme::s_titleFont?Theme::s_titleFont:ImGui::GetFont(),27,
            ImVec2(brand.x+46,brand.y+2),IM_COL32(239,36,89,255),"UN");
        auto* font=Theme::s_titleFont?Theme::s_titleFont:ImGui::GetFont();
        const float unWidth=font->CalcTextSizeA(27,1000,0,"UN").x;
        draw->AddText(font,27,ImVec2(brand.x+46+unWidth,brand.y+2),Theme::kColText,"LEASHED");
        const float saveWidth=(std::max)(148.f,ImGui::CalcTextSize(model.saveLabel).x+28);
        const float frameHeight=ImGui::GetFrameHeight();
        const float toolbarHeight=(std::max)(30.f,frameHeight);
        const float rowHeight=(std::max)(40.f,toolbarHeight);
        const float gap=ImGui::GetStyle().ItemSpacing.x;
        const float rowGap=ImGui::GetStyle().ItemSpacing.y;
        const float brandWidth=46.f+font->CalcTextSizeA(27,1000,0,"UNLEASHED").x+8.f;
        const bool stackedHeader=model.headerWidth>width;
        const float headerWidth=stackedHeader?model.headerColumnWidth:model.headerWidth;
        const float headerHeight=stackedHeader?toolbarHeight*2+rowGap:toolbarHeight;
        const float searchMin=120.f*scale, searchMax=220.f*scale;
        const float singleSearch=width-brandWidth-headerWidth-saveWidth-gap*3;
        const bool singleRow=!stackedHeader && singleSearch>=searchMin;
        float searchWidth=singleRow?(std::min)(searchMax,singleSearch):(std::min)(searchMax,(std::max)(1.f,width-brandWidth-gap));
        float searchX=0, searchY=0, headerX=0, headerY=0, saveX=0, saveY=0;
        if(singleRow) {
            searchX=width-searchWidth-headerWidth-saveWidth-gap*2;
            headerX=searchX+searchWidth+gap;
            saveX=headerX+headerWidth+gap;
        } else {
            if(searchWidth>=searchMin) searchX=width-searchWidth;
            else {searchWidth=(std::min)(searchMax,width);searchY=rowHeight+rowGap;}
            headerY=searchY+rowHeight+rowGap;
            if(headerWidth+gap+saveWidth<=width) {
                headerX=width-headerWidth-saveWidth-gap;
                saveX=headerX+headerWidth+gap;
                saveY=headerY;
            } else {
                saveX=(std::max)(0.f,width-saveWidth);
                saveY=headerY+headerHeight+rowGap;
            }
        }
        ImGui::Dummy(ImVec2(brandWidth,rowHeight));
        static char query[128]{};
        ImGui::SetCursorScreenPos(ImVec2(brand.x+searchX,brand.y+searchY+(rowHeight-frameHeight)*.5f));
        ImGui::SetNextItemWidth(searchWidth);
        ImGui::InputTextWithHint("##GlobalSearch",zh?"搜索页面和设置…":"Search pages and settings...",query,sizeof(query));
        if(ImGui::IsItemActivated()||ImGui::IsItemEdited())ImGui::OpenPopup("##SearchResults");
        if(ImGui::BeginPopup("##SearchResults")) {
            ImGui::SetNextItemWidth(300);
            ImGui::InputTextWithHint("##searchQuery",zh?"搜索设置…":"Search settings...",query,sizeof(query));
            if(ImGui::IsWindowAppearing())ImGui::SetKeyboardFocusHere(-1);
            bool matched=false;
            for(const auto& entry:Search::entries) {
                const auto* localized=UI::Text::Label(entry.label,zh);
                if(!query[0]||(!ImStristr(entry.label,nullptr,query,nullptr)&&!ImStristr(localized,nullptr,query,nullptr)))continue;
                matched=true;ImGui::PushID(&entry);
                const auto label=std::string(model.pages[entry.page].label)+" / "+localized;
                if(ImGui::Selectable(label.c_str())){model.selected=entry.page;Search::Go(entry.label,entry.page);query[0]=0;ImGui::CloseCurrentPopup();}
                ImGui::PopID();
            }
            for(int i=0;i<int(model.pages.size());++i) {
                if(i==9) continue;
                const auto& page=model.pages[i];
                if(!ImStristr(page.label,nullptr,query,nullptr) && !ImStristr(page.group,nullptr,query,nullptr)) continue;
                matched=true;
                const auto label=std::string(page.group)+" / "+page.label;
                if(ImGui::Selectable(label.c_str())) {model.selected=i;query[0]=0;ImGui::CloseCurrentPopup();}
            }
            if(ImStristr("Methods 运动方法",nullptr,query,nullptr)) {
                matched=true;
                if(ImGui::Selectable(zh?"共享参数 / 行为模式 / 兼容方法库":"Shared settings / Behavior modes / Legacy methods")) {
                    model.selected=8;query[0]=0;ImGui::CloseCurrentPopup();
                }
            }
            if(!matched) ImGui::TextDisabled("%s",zh?"没有匹配项":"No matching settings");
            ImGui::EndPopup();
        }
        if(header) {
            ImGui::SetCursorScreenPos(ImVec2(brand.x+headerX,brand.y+headerY+(stackedHeader?0.f:(rowHeight-toolbarHeight)*.5f)));
            header(toolbarHeight,stackedHeader);
        }
        ImGui::SetCursorScreenPos(ImVec2(brand.x+saveX,brand.y+saveY+(rowHeight-toolbarHeight)*.5f));
        ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding,6.f);
        ImGui::PushStyleColor(ImGuiCol_Button,Theme::kColAccentDark);
        if(ImGui::Button(model.saveLabel,ImVec2(saveWidth,toolbarHeight)))save();
        ImGui::PopStyleColor();
        ImGui::PopStyleVar();
        const float headerBottom=headerY+(stackedHeader?headerHeight:rowHeight);
        const float bottom=(std::max)({rowHeight,searchY+rowHeight,headerBottom,saveY+rowHeight});
        ImGui::SetCursorScreenPos(ImVec2(brand.x,brand.y+bottom));
        ImGui::Dummy(ImVec2(0,0));
        ImGui::Spacing();
        ImGui::Separator();
        ImGui::Spacing();
        const float height=(std::max)(100.f,ImGui::GetContentRegionAvail().y-38);
        const bool collapsed=preferences.sidebarCollapsed;
        const float navWidth=collapsed?48.f*scale:(std::min)(190.f*scale,width*.27f);
        const float cardScale=collapsed?scale:(std::min)(scale,(std::max)(1.f,navWidth/190.f));
        const float cardHeight=(std::max)(52.f*cardScale,ImGui::GetFontSize()*2.f+14.f);
        ImGui::BeginGroup();
        const ImVec2 navStart=ImGui::GetCursorScreenPos();
        const float navPadX=collapsed?5.f*scale:20.f;
        const float toggleHeight=(std::max)(28.f,ImGui::GetFrameHeight());
        const float navBodyY=navStart.y+(collapsed?6.f*scale:16.f)+toggleHeight+rowGap;
        ImGui::SetCursorScreenPos(ImVec2(navStart.x+navPadX,navStart.y+(collapsed?6.f*scale:16.f)));
        if(ImGui::Button(collapsed?">##Expand":"<##Collapse",ImVec2((std::max)(1.f,navWidth-navPadX*2),toggleHeight))) {
            preferences.sidebarCollapsed=!preferences.sidebarCollapsed;
            Preferences::Save();
        }
        ImGui::SetItemTooltip("%s",collapsed?(zh?"展开导航":"Expand navigation"):(zh?"收起导航":"Collapse navigation"));
        ImGui::SetCursorScreenPos(ImVec2(navStart.x,navBodyY));
        ImGui::PushStyleColor(ImGuiCol_ChildBg,Theme::kColShell1);
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding,ImVec2(navPadX,0));
        ImGui::BeginChild("BusinessNavigation",ImVec2(navWidth,(std::max)(1.f,height-cardHeight-(navBodyY-navStart.y)-rowGap)),ImGuiChildFlags_AlwaysUseWindowPadding,
            collapsed?ImGuiWindowFlags_NoScrollbar:ImGuiWindowFlags_None);
        const char* group=nullptr;
        for(int order=0;order<int(model.pages.size());++order) {
            const int i=model.pages.size()==16?(order==12?15:order>12?order-1:order):order;
            if(i==9)continue;
            const auto& page=model.pages[i];
            if(!group||std::strcmp(group,page.group)!=0) {
                if(group)ImGui::Dummy(ImVec2(0,10));
                if(!collapsed)ImGui::TextDisabled("%s",page.group);
                group=page.group;
            }
            ImGui::PushID(i);
            const bool selected=model.selected==i;
            const auto p=ImGui::GetCursorScreenPos();
            const float rowWidth=ImGui::GetContentRegionAvail().x;
            ImGui::PushStyleColor(ImGuiCol_Button,selected?Theme::kColAccentDark:Theme::kColShell1);
            if(ImGui::Button("##Page",ImVec2(rowWidth,(std::max)(32.f,ImGui::GetFrameHeight()+4))))model.selected=i;
            ImGui::PopStyleColor();
            auto* nav=ImGui::GetWindowDrawList();
            const auto color=selected?Theme::kColAccent:Theme::kColTextDim;
            const float rowHeight=(std::max)(32.f,ImGui::GetFrameHeight()+4);
            Icon(nav,ImVec2(p.x+(collapsed?(rowWidth-18.f)*.5f:8.f),p.y+(rowHeight-18.f)*.5f),i,color);
            if(!collapsed)nav->AddText(ImVec2(p.x+34,p.y+(rowHeight-ImGui::GetFontSize())*.5f),selected?Theme::kColText:Theme::kColTextDim,page.label);
            if(selected)nav->AddRectFilled(p,ImVec2(p.x+2,p.y+(std::max)(32.f,ImGui::GetFrameHeight()+4)),Theme::kColAccent,1);
            ImGui::SetItemTooltip("%s / %s",page.group,page.label);
            ImGui::PopID();
        }
        ImGui::EndChild();
        ImGui::PopStyleVar();
        ImGui::PopStyleColor();
        AccountCard(navWidth,cardHeight,cardScale,collapsed,zh);
        ImGui::EndGroup();
        ImGui::SameLine(0,16);
        ImGui::BeginChild("PageBody",ImVec2(0,height));
        Search::currentPage=model.selected;
        body(model.selected);
        ImGui::EndChild();
        footer(ImGui::GetWindowDrawList(),ImGui::GetCursorScreenPos(),width);
    }
    ImGui::End();
    ImGui::PopStyleVar();
    StatusToast(model, scale);
}
}
