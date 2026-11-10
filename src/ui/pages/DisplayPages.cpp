#include "ui/Search.hpp"
#include "Game/OutlineBrightness.hpp"
#include <Windows.h>
#include <shellapi.h>

#include "Features/UI.hpp"
#include "ui/WorkspaceShell.hpp"
#include "ui/SlotSelector.hpp"
#include "ui/Text.hpp"
#include "ui/Theme.hpp"
#include "ui/ColorPicker.hpp"
using UI::Text::UiText;
using UI::Text::kUiLanguageOptions;
#include "output/DeviceCapabilities.hpp"
#include "game/profiles/ProfileState.hpp"
#include "Features/UiLabels.hpp"
#include "Features/UiValueFormat.hpp"
using namespace UI::ValueFormat;
#include "output/MockBackend.hpp"
#include "output/OutputRuntime.hpp"
#include "Utils/Config.hpp"
#include "Game/HeroSkills.hpp"
#include "Game/HeroSettings.hpp"
#include "Game/Offsets.hpp"
#include "Game/Overwatch.hpp"
#include "Game/Target.hpp"
#include "Game/Structs.hpp"
#include "Game/WeaponSpec.hpp"
#include "Renderer/Overlay.hpp"
#include "Renderer/Renderer.hpp"
#include "Utils/Diagnostics.hpp"
#include "Utils/InputLabels.hpp"
#include "Utils/ProcessConnection.hpp"
#include "resource.h"
#include <imgui.h>
#include <imgui_internal.h>
#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cctype>
#include <cfloat>
#include <cstdint>
#include <cstring>
#include <cstdio>
#include <functional>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include "ui/EditorSupport.hpp"
using namespace UI::Theme;

namespace {
using V2::VisualSettings::Element;
using V2::VisualSettings::Settings;
using V2::VisualSettings::Style;
bool StyleDetails(Style& style,Element element) {
    const bool size=element==Element::Name||element==Element::BattleTag||element==Element::Resources||element==Element::Distance||element==Element::Avatar||element==Element::Skills||element==Element::Ultimate||element==Element::Prediction||element==Element::Offscreen||element==Element::Crosshair;
    const bool line=element==Element::Box||element==Element::Health||element==Element::Resources||element==Element::Skeleton||element==Element::Prediction||element==Element::Hitbox||element==Element::Snapline||element==Element::EyeRay||element==Element::Crosshair;
    bool changed=false;
    if(size){ImGui::SetNextItemWidth(150);changed|=ImGui::SliderFloat(L("Size"),&style.size,6,80,"%.1f");}
    if(line){ImGui::SetNextItemWidth(150);changed|=ImGui::SliderFloat(L("Thickness"),&style.thickness,.5f,8,"%.1f px");}
    ImGui::SetNextItemWidth(150);changed|=ImGui::SliderFloat("X",&style.offsetX,-300,300,"%.0f px");
    ImGui::SetNextItemWidth(150);changed|=ImGui::SliderFloat("Y",&style.offsetY,-300,300,"%.0f px");
    ImGui::TextDisabled("%s",L("Ally"));ImGui::SameLine();changed|=UI::ColorPicker("ally",style.ally);
    ImGui::TextDisabled("%s",L("Target"));ImGui::SameLine();changed|=UI::ColorPicker("target",style.target);
    return changed;
}
bool ElementRow(Settings& settings,Element element,bool& enabled) {
    const int index=int(element);ImGui::PushID(index);
    const float scale=(std::max)(1.f,ImGui::GetFontSize()/15.f);
    const float controls=(std::max)(30.f,ImGui::GetFontSize()*2)+78+ImGui::GetStyle().ItemSpacing.x*3;
    const float labelWidth=(std::max)(40.f,(ImGui::GetContentRegionAvail().x-controls)/scale);
    SettingRow(V2::VisualSettings::kElementNames[index],labelWidth);
    bool changed=UICheckbox("##enabled",&enabled);
    auto& style=settings.elements[index];
    ImGui::SameLine();changed|=UI::ColorPicker("visible",style.visible,[&]{changed|=StyleDetails(style,element);});
    ImGui::SetItemTooltip("%s",L("Visible"));
    ImGui::SameLine();changed|=UI::ColorPicker("hidden",style.hidden);
    ImGui::SetItemTooltip("%s",L("Hidden"));
    ImGui::SameLine();if(ImGui::Button("...",ImVec2(26,ControlHeight())))ImGui::OpenPopup("##details");
    if(ImGui::BeginPopup("##details")){changed|=StyleDetails(style,element);ImGui::EndPopup();}
    ImGui::PopID();return changed;
}
void OutlineControls() {
    SettingRow("Game Outlines");UICheckbox("##gameOutlines",&OW::Config::gameOutlines);
    ImGui::SameLine();UI::ColorPicker("outlineVisible",OW::Config::enargb);
    ImGui::SetItemTooltip("%s",L("Visible"));
    ImGui::SameLine();UI::ColorPicker("outlineHidden",OW::Config::invisnenargb);
    ImGui::SetItemTooltip("%s",L("Hidden"));
    SettingRow("Visible Only");UICheckbox("##outlineVisibleOnly",&OW::Config::gameOutlineVisibleOnly);
    SettingRow("Rainbow Color");UICheckbox("##outlineRainbow",&OW::Config::gameOutlineRainbow);
    SettingRow("Color Gain");PushControlWidth();UISlider("##outlineGain",&OW::Config::gameOutlineColorGain,.1f,3.f,Number(2,"x"));ImGui::PopItemWidth();
    ImGui::TextWrapped("%s",L("RGB gain clips at 255 per channel; this is not HDR glow intensity."));
#ifndef UNLEASHED_OBSERVATION_ONLY
    namespace Brightness = OW::OutlineBrightness;
    static float brightnessDraft = 2.f;
    const auto brightnessCommand = Brightness::Snapshot();
    SettingRow("Outline Brightness (alpha multiplier)");
    const float buttonWidth = ImGui::CalcTextSize(L("Apply")).x + ImGui::GetStyle().FramePadding.x * 2;
    ImGui::SetNextItemWidth((std::max)(80.f, ImGui::GetContentRegionAvail().x - buttonWidth - ImGui::GetStyle().ItemSpacing.x));
    ImGui::InputFloat("##outlineAlphaDraft", &brightnessDraft, 0.f, 0.f, "%.7g");
    ImGui::SameLine();
    ImGui::BeginDisabled(!Brightness::Valid(brightnessDraft) || !OW::Config::gameOutlines);
    if (ImGui::Button(L("Apply"))) Brightness::Submit(brightnessDraft);
    ImGui::EndDisabled();
    if (!Brightness::Valid(brightnessDraft)) ImGui::TextWrapped("%s", L("Enter a finite multiplier of zero or greater."));
    if (brightnessCommand.enabled || Brightness::RestoreCount.load()) {
        if (ImGui::Button(L("Restore Brightness"))) Brightness::Disable();
        ImGui::SameLine();
        ImGui::Text("%s %.7g", L("Committed Multiplier"), brightnessCommand.value);
        ImGui::Text("%s %u / %s %u / %s %u", L("Applied Targets"), Brightness::AppliedCount.load(),
            L("Failed Targets"), Brightness::FailureCount.load(), L("Restore Pending"), Brightness::RestoreCount.load());
    } else ImGui::TextWrapped("%s", L("Edit the number, then click Apply to write."));
    ImGui::TextWrapped("%s", L("Alpha multiplier; supports scientific notation. Fully opaque outlines stop brightening at alpha 255."));
#endif
}
void Card(const char* title,const std::function<void()>& content) {
    ImGui::PushID(title);
    ImGui::PushStyleVar(ImGuiStyleVar_ChildRounding,10);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding,ImVec2(12,10));
    ImGui::PushStyleColor(ImGuiCol_ChildBg,kColPanelSoft);
    ImGui::BeginChild("Card",ImVec2(0,0),ImGuiChildFlags_Borders|ImGuiChildFlags_AutoResizeY|ImGuiChildFlags_AlwaysAutoResize|ImGuiChildFlags_AlwaysUseWindowPadding,
        ImGuiWindowFlags_NoScrollbar|ImGuiWindowFlags_NoScrollWithMouse);
    if(UI::Search::Pending())ImGui::SetNextItemOpen(true);
    UI::Search::Anchor(title);
    if(ImGui::CollapsingHeader(L(title),ImGuiTreeNodeFlags_DefaultOpen))content();
    ImGui::EndChild();ImGui::PopStyleColor();ImGui::PopStyleVar(2);ImGui::Spacing();ImGui::PopID();
}
void Preview(const Settings& settings) {
    static bool visible=true;
    UICheckbox(L("Visible"),&visible);ImGui::SameLine();ImGui::TextDisabled("%s",L("Example"));
    const ImVec2 p=ImGui::GetCursorScreenPos();
    const float width=ImGui::GetContentRegionAvail().x;
    ImGui::Dummy(ImVec2(width,320));
    auto* draw=ImGui::GetWindowDrawList();
    const float cx=p.x+width*.5f,top=p.y+62,bottom=p.y+260,left=cx-48,right=cx+48;
    auto style=[&](Element e)->const Style&{return settings.elements[int(e)];};
    auto position=[&](Element e,float x,float y){return ImVec2(x+style(e).offsetX,y+style(e).offsetY);};
    auto color=[&](Element e){return ImGui::ColorConvertFloat4ToU32(V2::VisualSettings::Color(style(e),true,visible));};
    const float scale=settings.dynamicFont?1.6f:1.f;
    auto text=[&](Element e,float x,float y,const char* value){auto* font=ImGui::GetFont();const float size=style(e).size*scale;const float w=font->CalcTextSizeA(size,FLT_MAX,0,value).x;draw->AddText(font,size,position(e,x-w*.5f,y),color(e),value);};
    const ImU32 silhouette=IM_COL32(111,116,132,55);
    draw->AddCircleFilled(ImVec2(cx,top+30),12,silhouette,24);
    const std::pair<ImVec2,ImVec2> bones[]={{{cx,top+46},{cx,bottom-65}},{{cx,top+65},{left+4,top+115}},{{cx,top+65},{right-4,top+115}},{{cx,bottom-65},{left+8,bottom}},{{cx,bottom-65},{right-8,bottom}}};
    for(const auto& bone:bones)draw->AddLine(bone.first,bone.second,silhouette,8);
    if(OW::Config::draw_skel){const auto c=color(Element::Skeleton);draw->AddCircle(position(Element::Skeleton,cx,top+30),12,c,24,style(Element::Skeleton).thickness);
        for(const auto& bone:bones)draw->AddLine(position(Element::Skeleton,bone.first.x,bone.first.y),position(Element::Skeleton,bone.second.x,bone.second.y),c,style(Element::Skeleton).thickness);}
    if(OW::Config::draw_info){const auto c=color(Element::Box);const float thickness=style(Element::Box).thickness;
        const auto a=position(Element::Box,left,top),b=position(Element::Box,right,bottom);
        if(settings.boxType==2)draw->AddRectFilled(a,b,(c&~IM_COL32_A_MASK)|(((((c>>IM_COL32_A_SHIFT)&255u)*45u)/255u)<<IM_COL32_A_SHIFT));
        if(settings.boxType!=0)draw->AddRect(a,b,c,0,0,thickness);
        else for(int side=0;side<2;++side)for(int row=0;row<2;++row){const auto point=position(Element::Box,side?right:left,row?bottom:top);
            draw->AddLine(point,ImVec2(point.x+(side?-20:20),point.y),c,thickness);draw->AddLine(point,ImVec2(point.x,point.y+(row?-20:20)),c,thickness);}}
    if(OW::Config::name){const char* label=settings.nameType==1?"Player":settings.nameType==2?"Ana / Player":"Ana";text(Element::Name,cx,top-22,label);}
    if(OW::Config::drawbattletag)text(Element::BattleTag,cx,top-40,"Player");
    if(OW::Config::draw_avatar){auto* icons=Render::GetIconManager();auto* image=icons?icons->LoadHeroAvatar("ana"):nullptr;const float size=style(Element::Avatar).size;
        if(image){const auto a=position(Element::Avatar,cx-size*.5f,top-size-10);draw->AddImageRounded(reinterpret_cast<ImTextureID>(image),a,ImVec2(a.x+size,a.y+size),ImVec2(0,0),ImVec2(1,1),color(Element::Avatar),4);}}
    if(OW::Config::healthbar){const auto a=position(Element::Health,left-7,top);const float w=style(Element::Health).thickness;draw->AddRectFilled(a,ImVec2(a.x+w,a.y+bottom-top),kColControl);draw->AddRectFilled(ImVec2(a.x,a.y+40),ImVec2(a.x+w,a.y+bottom-top),color(Element::Health));}
    if(OW::Config::healthbar2){const auto a=position(Element::Resources,cx-style(Element::Resources).size*2.5f,top-9);draw->AddRectFilled(a,ImVec2(a.x+style(Element::Resources).size*5,a.y+style(Element::Resources).thickness*3),kColControl);draw->AddRectFilled(a,ImVec2(a.x+style(Element::Resources).size*4,a.y+style(Element::Resources).thickness*3),color(Element::Resources));}
    if(settings.worldUltimate)text(Element::Ultimate,right+18,top,"67%");
    if(OW::Config::skillinfo){const float size=style(Element::Skills).size;for(int i=0;i<2;++i){const auto a=position(Element::Skills,right+8+i*(size+3),top+20);draw->AddRect(a,ImVec2(a.x+size,a.y+size),color(Element::Skills),3,0,style(Element::Skills).thickness);draw->AddText(ImGui::GetFont(),size*.67f,ImVec2(a.x+2,a.y+2),color(Element::Skills),i?"--":"4");}}
    if(OW::Config::dist)text(Element::Distance,cx,bottom+4,"14 m");
    if(settings.prediction)draw->AddCircle(position(Element::Prediction,cx+25,top+75),style(Element::Prediction).size*.25f,color(Element::Prediction),20,style(Element::Prediction).thickness);
    if(OW::Config::draw_hitbox)draw->AddCircle(position(Element::Hitbox,cx,top+30),16,color(Element::Hitbox),24,style(Element::Hitbox).thickness);
    if(OW::Config::drawline)draw->AddLine(ImVec2(cx,p.y+315),position(Element::Snapline,cx,bottom),color(Element::Snapline),style(Element::Snapline).thickness);
    if(OW::Config::eyeray)draw->AddLine(position(Element::EyeRay,cx,top+30),position(Element::EyeRay,right+30,top+40),color(Element::EyeRay),style(Element::EyeRay).thickness);
    if(settings.offscreen){const auto tip=position(Element::Offscreen,p.x+width-18,top+110);const float size=style(Element::Offscreen).size;draw->AddTriangleFilled(tip,ImVec2(tip.x-size,tip.y-size*.5f),ImVec2(tip.x-size,tip.y+size*.5f),color(Element::Offscreen));}
    if(OW::Config::crosscircle){const auto center=position(Element::Crosshair,cx,top+100);draw->AddCircle(center,style(Element::Crosshair).size*.5f,color(Element::Crosshair),24,style(Element::Crosshair).thickness);}
}

}
void UI::VisualsPage() {
    auto settings=V2::HeroSettings::Snapshot()->display;
    bool changed=false;
    const float width=ImGui::GetContentRegionAvail().x;
    const bool columns=width>=650;
    const float height=(std::max)(200.f,ImGui::GetContentRegionAvail().y);
    ImGui::BeginChild("VisualsLeft",ImVec2(columns?(width-16)*.5f:0,columns?height:0),columns?ImGuiChildFlags_None:ImGuiChildFlags_AutoResizeY);
    Card("Players",[&]{
        SettingRow(L("Enabled"));changed|=UICheckbox("##playersEnabled",&settings.enabled);
        SettingRow(L("Draw Enemy"));changed|=UICheckbox("##enemies",&settings.enemies);
        SettingRow(L("Draw Ally"));changed|=UICheckbox("##allies",&settings.allies);
        SettingRow(L("Visible Only"));changed|=UICheckbox("##visibleOnly",&settings.visibleOnly);
        SettingRow("Max Distance");PushControlWidth();UISlider("##maxDistance",&OW::Config::visualMaxDist,0,1000,Number(0," m"));ImGui::PopItemWidth();
    });
    Card("Player ESP",[&]{
        SettingRow("Dynamic Font Scaling");changed|=UICheckbox("##dynamicFont",&settings.dynamicFont);
        changed|=ElementRow(settings,Element::Box,OW::Config::draw_info);
        const char* boxes[]={"Corner","Rectangle","Filled"};SettingRow("Box Type");PushControlWidth();changed|=UISelect("##boxType",&settings.boxType,boxes,3);ImGui::PopItemWidth();
        changed|=ElementRow(settings,Element::Name,OW::Config::name);
        const char* names[]={"Hero Name","BattleTag","Hero / BattleTag"};SettingRow("Name Type");PushControlWidth();changed|=UISelect("##nameType",&settings.nameType,names,3);ImGui::PopItemWidth();
        changed|=ElementRow(settings,Element::BattleTag,OW::Config::drawbattletag);
        changed|=ElementRow(settings,Element::Health,OW::Config::healthbar);
        changed|=ElementRow(settings,Element::Resources,OW::Config::healthbar2);
        changed|=ElementRow(settings,Element::Distance,OW::Config::dist);
        changed|=ElementRow(settings,Element::Avatar,OW::Config::draw_avatar);
        changed|=ElementRow(settings,Element::Ultimate,settings.worldUltimate);
        changed|=ElementRow(settings,Element::Skeleton,OW::Config::draw_skel);
        changed|=ElementRow(settings,Element::Skills,OW::Config::skillinfo);
        changed|=ElementRow(settings,Element::Prediction,settings.prediction);
        changed|=ElementRow(settings,Element::Hitbox,OW::Config::draw_hitbox);
        changed|=ElementRow(settings,Element::Offscreen,settings.offscreen);
        changed|=ElementRow(settings,Element::Snapline,OW::Config::drawline);
        changed|=ElementRow(settings,Element::EyeRay,OW::Config::eyeray);
        changed|=ElementRow(settings,Element::Crosshair,OW::Config::crosscircle);
        OutlineControls();
    });
    Card("World",[&]{
        auto& world=settings.world;
        ImGui::TextDisabled("%s",OW::Config::uiLanguage==1?"物品识别与字段待现场核验；轮胎缺少可靠身份":"Object capture pending live validation; tire identity unavailable");
        SettingRow("Enabled");changed|=UICheckbox("##worldEnabled",&world.enabled);
        SettingRow("Box");changed|=UICheckbox("##worldBox",&world.box);
        SettingRow("Name");changed|=UICheckbox("##worldName",&world.name);
        SettingRow("Icon");changed|=UICheckbox("##worldIcon",&world.icon);
        SettingRow("Distance");changed|=UICheckbox("##worldDistance",&world.distance);
        SettingRow("Max Distance");PushControlWidth();changed|=UISlider("##worldMaxDistance",&world.maxDistance,0,1000,Number(0," m"));ImGui::PopItemWidth();
        SettingRow("Size");PushControlWidth();changed|=UISlider("##worldSize",&world.size,6,80,Number(0," px"));ImGui::PopItemWidth();
        for(int i=0;i<int(V2::VisualSettings::kObjects.size());++i){ImGui::PushID(i);const auto label=std::string(L(V2::VisualSettings::kObjects[i].name))+(i==0?"":" *");SettingRow(label.c_str());
            ImGui::BeginDisabled(V2::VisualSettings::kObjects[i].id==0);
            changed|=UICheckbox("##object",&world.types[i]);ImGui::SameLine();changed|=UI::ColorPicker("color",world.colors[i]);ImGui::EndDisabled();
            ImGui::SetItemTooltip("%s",i==0?L("Existing capture; live verification required"):L("Candidate identification; live verification required"));ImGui::PopID();}
    });
    ImGui::EndChild();
    if(columns)ImGui::SameLine(0,16);
    ImGui::BeginChild("VisualsRight",ImVec2(0,columns?height:0),columns?ImGuiChildFlags_None:ImGuiChildFlags_AutoResizeY);
    Card("Appearance preview",[&]{Preview(settings);});
    Card("Radar",[&]{auto& radar=settings.radar;
        SettingRow("Enabled");UICheckbox("##radarEnabled",&OW::Config::radar);
        auto slider=[&](const char* label,const char* id,float& value,float low,float high,const char* unit){SettingRow(label);PushControlWidth();changed|=UISlider(id,&value,low,high,Number(1,unit));ImGui::PopItemWidth();};
        slider("Size","##radarRadius",radar.radius,40,600," px");slider("Display distance","##radarRange",radar.rangeM,5,500," m");
        slider("Marker Size","##markerSize",radar.markerSize,3,48," px");slider("Background Opacity","##radarAlpha",radar.backgroundAlpha,0,1,"");
        SettingRow("Draw Ally");changed|=UICheckbox("##radarAllies",&radar.allies);
        SettingRow("Rotate with view");changed|=UICheckbox("##radarRotate",&radar.rotate);
        SettingRow("Radar Lines");UICheckbox("##radarLines",&OW::Config::radarline);
        changed|=UIColorEdit("Enemy",&radar.enemy);changed|=UIColorEdit("Ally",&radar.ally);
        slider("Horizontal position","##radarX",OW::Config::radarPosX,0,1,"");slider("Vertical position","##radarY",OW::Config::radarPosY,0,1,"");
    });
    Card("HUD",[&]{
        auto placement=[](const char* id,float& x,float& y,float* scale){ImGui::PushID(id);bool edited=false;
            ImGui::SetNextItemWidth(140);edited|=ImGui::SliderFloat("X",&x,0,1,"%.3f");ImGui::SetNextItemWidth(140);edited|=ImGui::SliderFloat("Y",&y,0,1,"%.3f");
            if(scale){ImGui::SetNextItemWidth(140);edited|=ImGui::SliderFloat(L("Scale"),scale,.35f,2.5f,"%.2fx");}ImGui::PopID();return edited;};
        SettingRow("Top-left Info");UICheckbox("##info",&OW::Config::showOverlayInfo);
        changed|=placement("info",settings.infoX,settings.infoY,&settings.infoScale);
        SettingRow("Hero Status");UICheckbox("##heroStatus",&OW::Config::ult);
        const char* styles[]={"ED Compact","Custom Status"};SettingRow("Style");PushControlWidth();UISelect("##heroStyle",&OW::Config::heroStatusStyle,styles,2);ImGui::PopItemWidth();
        placement("hero",OW::Config::heroStatusPosX,OW::Config::heroStatusPosY,&OW::Config::heroStatusScale);
        const char* teams[]={"All","Enemy","Ally"};SettingRow("Hero Status Team");PushControlWidth();UISelect("##roster",&OW::Config::ultimateRosterFilter,teams,3);ImGui::PopItemWidth();
        SettingRow("Behavior and Slots");UICheckbox("##slotStatus",&OW::Config::showSlotStatus);
        placement("slots",OW::Config::slotPosX,OW::Config::slotPosY,nullptr);
    });
    ImGui::EndChild();
    if(changed)V2::HeroSettings::SetDisplay(std::move(settings));
}
void DrawThemeGeneralPage() {
    auto settings=V2::HeroSettings::Snapshot()->display;
    bool changed=false;
    for(int i=0;i<int(Element::Count);++i) {
        ImGui::PushID(i);
        Card(V2::VisualSettings::kElementNames[i],[&]{
            auto& style=settings.elements[i];
            changed|=UIColorEdit("Visible",&style.visible);changed|=UIColorEdit("Hidden",&style.hidden);
            changed|=UIColorEdit("Ally",&style.ally);changed|=UIColorEdit("Target",&style.target);
            changed|=StyleDetails(style,static_cast<Element>(i));
        });
        ImGui::PopID();
    }
    Card("Game Outlines",[]{OutlineControls();});
    if(changed)V2::HeroSettings::SetDisplay(std::move(settings));
}

static bool UIFovRingCompactColorEdit(const char* label, ImVec4* value) {
    return UI::ColorPicker(label,*value);
}

static bool UIFovRingStyleRow(OW::Config::FovRingSlotKind kind,
                              int slotIndex,
                              bool slotEnabled,
                              float slotFovDeg) {
    OW::Config::FovRingSlotStyle& style = OW::Config::FovRingStyleFor(kind, slotIndex);
    style = OW::Config::ClampFovRingStyle(style, kind, slotIndex);

    bool changed = false;
    ImGui::PushID(static_cast<int>(kind) * 100 + slotIndex);

    char slotLabel[32] = {};
    std::snprintf(slotLabel, sizeof(slotLabel), L("Slot %d"), slotIndex + 1);
    ImGui::AlignTextToFramePadding();
    ImGui::TextUnformatted(slotLabel);

    ImGui::SameLine(74.0f);
    ImGui::TextColored(ImGui::ColorConvertU32ToFloat4(slotEnabled ? kColTextMuted : kColTextDim),
                       "%s %.0f deg",
                       slotEnabled ? L("On") : L("Off"),
                       OW::Config::ClampFovDeg(slotFovDeg));

    ImGui::SameLine(160.0f);
    changed |= UICheckbox("##visible", &style.visible);
    ImGui::SameLine(181.0f);
    ImGui::AlignTextToFramePadding();
    ImGui::TextUnformatted(L("Show"));

    ImGui::SameLine(232.0f);
    changed |= UIFovRingCompactColorEdit("##color", &style.color);

    ImGui::SameLine(334.0f);
    ImGui::SetNextItemWidth(102.0f);
    changed |= UISlider("##thickness", &style.thickness, 0.5f, 6.0f, Number(1, " px"));

    ImGui::SameLine(448.0f);
    static const char* kLineStyles[] = { "Solid", "Dashed" };
    ImGui::SetNextItemWidth(88.0f);
    changed |= UISelect("##lineStyle", &style.lineStyle, kLineStyles, IM_ARRAYSIZE(kLineStyles));

    ImGui::SameLine(548.0f);
    changed |= UICheckbox("##label", &style.showLabel);
    ImGui::SameLine(569.0f);
    ImGui::AlignTextToFramePadding();
    ImGui::TextUnformatted(L("Label"));

    if (changed)
        style = OW::Config::ClampFovRingStyle(style, kind, slotIndex);

    ImGui::PopID();
    return changed;
}

static void DrawFovRingStyleGroup(OW::Config::FovRingSlotKind kind,
                                  const HeroOption& selectedHero) {
    UIGroupBox("Aim Slot Rings");
    {
        const int slotCount = selectedHero.heroId == 0
            ? OW::Config::kMaxHeroPresetSlots
            : ImClamp(OW::Config::GetHeroAimSlotCount(selectedHero.heroId),
                1,
                OW::Config::kMaxHeroPresetSlots);

        for (int slotIndex = 0; slotIndex < slotCount; ++slotIndex) {
            OW::Config::HeroSlotPreset slot{};
            const bool hasSlot = selectedHero.heroId != 0 &&
                OW::Config::TryGetHeroAimSlot(selectedHero.heroId, slotIndex, slot);
            const bool slotEnabled = hasSlot && slot.enabled;
            const float fovDeg = hasSlot ? slot.preset.fov : OW::Config::kDefaultFovDeg;

            UIFovRingStyleRow(kind, slotIndex, selectedHero.heroId == 0 || slotEnabled, fovDeg);
        }
    }
    CloseGroupBox();
}

void DrawThemeFovRingsPage() {
    const HeroOption& selectedHero = CurrentHeroOption();
    DrawFovRingStyleGroup(OW::Config::FovRingSlotKind::Aim, selectedHero);
}

void UI::ThemePage() {
    ImGui::PushID("ThemePage");

    state.themeSubTab = ImClamp(state.themeSubTab, 0, kThemeSubTabCount - 1);
    if (state.themeSubTab == 0)
        DrawThemeGeneralPage();
    else
        DrawThemeFovRingsPage();

    ImGui::PopID();
}

// =====================================================================
// UI::MiscPage
// =====================================================================
