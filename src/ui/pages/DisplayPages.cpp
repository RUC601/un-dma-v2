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
#include "config/ContourSettings.hpp"
#include "vision/ContourService.hpp"
using UI::Text::UiText;
using UI::Text::kUiLanguageOptions;
#include "output/DeviceCapabilities.hpp"
#include "game/profiles/ProfileState.hpp"
#include "Features/UiLabels.hpp"
#include "Features/UiValueFormat.hpp"
using namespace UI::ValueFormat;
#include "output/MockDiagnostics.hpp"
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
#include "Renderer/Crosshair.hpp"
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
using V2::VisualSettings::CrosshairSettings;
using V2::VisualSettings::CrosshairPrimitiveKind;
bool StyleDetails(Style& style,Element element) {
    const bool size=element==Element::Name||element==Element::BattleTag||element==Element::Health||element==Element::Resources||element==Element::Distance||element==Element::Avatar||element==Element::Skills||element==Element::Ultimate||element==Element::Prediction||element==Element::Offscreen||element==Element::Crosshair||element==Element::CenterArrow;
    const bool line=element==Element::Box||element==Element::Health||element==Element::Resources||element==Element::Skeleton||element==Element::Prediction||element==Element::Hitbox||element==Element::Snapline||element==Element::EyeRay||element==Element::Crosshair;
    bool changed=false;
    if(size){ImGui::SetNextItemWidth(150);changed|=ImGui::SliderFloat(L("Size"),&style.size,6,80,"%.1f");}
    if(element==Element::CenterArrow){ImGui::SetNextItemWidth(150);changed|=ImGui::SliderFloat(L("Ring Radius"),&style.radius,40,600,"%.0f px");}
    if(line){ImGui::SetNextItemWidth(150);changed|=ImGui::SliderFloat(L("Thickness"),&style.thickness,.5f,8,"%.1f px");}
    ImGui::SetNextItemWidth(150);changed|=ImGui::SliderFloat("X",&style.offsetX,-300,300,"%.0f px");
    ImGui::SetNextItemWidth(150);changed|=ImGui::SliderFloat("Y",&style.offsetY,-300,300,"%.0f px");
    return changed;
}
constexpr float kEspDetailsWidth = 26.0f;

void EspSettingRow(const char* label) {
    const float scale = (std::max)(1.0f, ImGui::GetFontSize() / 15.0f);
    const float controls = ToggleWidth() + 4.0f * UI::kColorSwatchWidth + kEspDetailsWidth
        + ImGui::GetStyle().ItemSpacing.x * 5.0f;
    const float width = ImGui::GetContentRegionAvail().x;
    const float labelWidth = (std::max)(64.0f, std::floor((width - controls) / scale));
    SettingRow(label, labelWidth, controls);
}

bool ElementRow(Settings& settings,Element element,bool& enabled) {
    const int index=int(element);ImGui::PushID(index);
    EspSettingRow(V2::VisualSettings::kElementNames[index]);
    bool changed=UICheckbox("##enabled",&enabled);
    auto& style=settings.elements[index];
    ImGui::SameLine();changed|=UI::ColorPicker("visible",style.visible);
    ImGui::SetItemTooltip("%s",L("Enemy"));
    ImGui::SameLine();changed|=UI::ColorPicker("hidden",style.hidden);
    ImGui::SetItemTooltip("%s",L("Invisible Enemy"));
    ImGui::SameLine();changed|=UI::ColorPicker("target",style.target);
    ImGui::SetItemTooltip("%s",L("Target"));
    ImGui::SameLine();changed|=UI::ColorPicker("ally",style.ally);
    ImGui::SetItemTooltip("%s",L("Ally"));
    const std::string detailsLabel=std::string("...##")+V2::VisualSettings::kElementNames[index];
    ImGui::SameLine();if(ImGui::Button(detailsLabel.c_str(),ImVec2(kEspDetailsWidth,ControlHeight())))ImGui::OpenPopup("##details");
    ImGui::SetItemTooltip("%s",OW::Config::uiLanguage==1?"更多设置":"More settings");
    const char* typeLabel=element==Element::Box?"Box Type":element==Element::Name?"Name Type":nullptr;
    if(typeLabel&&UI::Search::Pending()&&UI::Search::requested==typeLabel)ImGui::OpenPopup("##details");
    if(ImGui::BeginPopup("##details")){
        ImGui::TextUnformatted(L(V2::VisualSettings::kElementNames[index]));
        ImGui::Spacing();
        if(typeLabel){
            UI::Search::Anchor(typeLabel);
            ImGui::TextDisabled("%s",L(typeLabel));
            const char* boxes[]={"Corner","Rectangle","Filled"};
            const char* names[]={"Hero Name","BattleTag","Hero / BattleTag"};
            ImGui::SetNextItemWidth(220);
            changed|=UISelect(element==Element::Box?"##boxType":"##nameType",
                element==Element::Box?&settings.boxType:&settings.nameType,element==Element::Box?boxes:names,3);
        }
        if(element==Element::Health){
            const bool zh=OW::Config::uiLanguage==1;
            const char* styles[]={zh?"1 · ED 侧边细条":"1 · ED side line",zh?"2 · 框上方横条":"2 · Above box",zh?"3 · 游戏分类血条":"3 · Game resource pools"};
            int healthStyle=settings.healthBarStyle-1;
            ImGui::TextDisabled("%s",zh?"血条样式":"Health Bar Style");
            ImGui::SetNextItemWidth(220);
            if(UISelect("##healthBarStyle",&healthStyle,styles,3)){settings.healthBarStyle=healthStyle+1;changed=true;}
        }
        changed|=StyleDetails(style,element);
        ImGui::EndPopup();
    }
    ImGui::PopID();return changed;
}
bool CustomStyleRow(const char* label,const char* id,Style& style,bool& enabled) {
    ImGui::PushID(id);
    EspSettingRow(label);
    bool changed=UICheckbox("##enabled",&enabled);
    ImGui::SameLine();changed|=UI::ColorPicker("visible",style.visible);
    ImGui::SetItemTooltip("%s",L("Enemy"));
    ImGui::SameLine();changed|=UI::ColorPicker("hidden",style.hidden);
    ImGui::SetItemTooltip("%s",L("Invisible Enemy"));
    ImGui::SameLine();changed|=UI::ColorPicker("target",style.target);
    ImGui::SetItemTooltip("%s",L("Target"));
    ImGui::SameLine();changed|=UI::ColorPicker("ally",style.ally);
    ImGui::SetItemTooltip("%s",L("Ally"));
    ImGui::SameLine();if(ImGui::Button("...##details",ImVec2(kEspDetailsWidth,ControlHeight())))ImGui::OpenPopup("##details");
    ImGui::SetItemTooltip("%s",OW::Config::uiLanguage==1?"更多设置":"More settings");
    if(ImGui::BeginPopup("##details")) {
        ImGui::TextUnformatted(L(label));ImGui::Spacing();
        changed|=StyleDetails(style,Element::Hitbox);
        ImGui::EndPopup();
    }
    ImGui::PopID();return changed;
}
bool CrosshairRow(Settings& settings,bool& enabled) {
    auto& crosshair=settings.crosshair;
    const bool zh=OW::Config::uiLanguage==1;
    const char* stylesZh[]={"圆圈","十字","圆圈 + 十字","实心点","方框","四点","T 形"};
    const char* stylesEn[]={"Circle","Cross","Circle + Cross","Dot","Square","Four Dots","T"};
    const char* const* styles=zh?stylesZh:stylesEn;
    bool changed=false;
    ImGui::PushID("crosshair");
    EspSettingRow("Crosshair");
    changed|=UICheckbox("##enabled",&enabled);
    ImGui::SameLine();changed|=UI::ColorPicker("##color",crosshair.color);
    ImGui::SetItemTooltip("%s",zh?"颜色":"Color");
    ImGui::SameLine();
    ImGui::SetNextItemWidth(-kEspDetailsWidth - ImGui::GetStyle().ItemSpacing.x);
    changed |= UISelect("##style", &crosshair.style, styles, int(V2::VisualSettings::CrosshairStyle::Count), false);
    ImGui::SameLine();if(ImGui::Button("...##details",ImVec2(kEspDetailsWidth,ControlHeight())))ImGui::OpenPopup("##details");
    ImGui::SetItemTooltip("%s",zh?"调整样式、间隙、粗细和轮廓":"Adjust style, gap, thickness and outline");
    if(ImGui::BeginPopup("##details")) {
        ImGui::TextUnformatted(zh?"准星样式":"Crosshair style");ImGui::Spacing();
        SettingRow(zh?"样式":"Style");ImGui::SetNextItemWidth(-FLT_MIN);changed|=UISelect("##popupStyle",&crosshair.style,styles,int(V2::VisualSettings::CrosshairStyle::Count),false);
        SettingRow(zh?"颜色":"Color");changed|=UI::ColorPicker("##popupColor",crosshair.color);
        SettingRow(zh?"轮廓颜色":"Outline color");changed|=UI::ColorPicker("##outlineColor",crosshair.outlineColor);
        SettingRow(zh?"显示轮廓":"Show outline");changed|=UICheckbox("##outline",&crosshair.outline);
        SettingRow(zh?"整体尺寸":"Size");ImGui::SetNextItemWidth(-FLT_MIN);changed|=UISlider("##size",&crosshair.size,2,80,Number(0," px"));
        SettingRow(zh?"线条粗细":"Thickness");ImGui::SetNextItemWidth(-FLT_MIN);changed|=UISlider("##thickness",&crosshair.thickness,.5f,8,Number(1," px"));
        SettingRow(zh?"中心间隙":"Center gap");ImGui::SetNextItemWidth(-FLT_MIN);changed|=UISlider("##gap",&crosshair.gap,0,40,Number(0," px"));
        SettingRow(zh?"点大小":"Dot size");ImGui::SetNextItemWidth(-FLT_MIN);changed|=UISlider("##dotSize",&crosshair.dotSize,1,24,Number(1," px"));
        SettingRow(zh?"轮廓粗细":"Outline thickness");ImGui::SetNextItemWidth(-FLT_MIN);changed|=UISlider("##outlineThickness",&crosshair.outlineThickness,0,6,Number(1," px"));
        SettingRow("X");ImGui::SetNextItemWidth(-FLT_MIN);changed|=UISlider("##offsetX",&crosshair.offsetX,-300,300,Number(0," px"));
        SettingRow("Y");ImGui::SetNextItemWidth(-FLT_MIN);changed|=UISlider("##offsetY",&crosshair.offsetY,-300,300,Number(0," px"));
        ImGui::EndPopup();
    }
    ImGui::PopID();
    return changed;
}
void DrawCrosshairPreview(ImDrawList* draw,const ImVec2& center,const CrosshairSettings& settings) {
    auto pass=[&](ImU32 color,float thickness,float expand) {
        V2::VisualSettings::ForEachCrosshairPrimitive(settings,center.x,center.y,[&](const V2::VisualSettings::CrosshairPrimitive& primitive) {
            if(primitive.kind==CrosshairPrimitiveKind::Line)draw->AddLine(ImVec2(primitive.x1,primitive.y1),ImVec2(primitive.x2,primitive.y2),color,thickness);
            else if(primitive.filled)draw->AddCircleFilled(ImVec2(primitive.x1,primitive.y1),primitive.radius+expand,color,48);
            else draw->AddCircle(ImVec2(primitive.x1,primitive.y1),primitive.radius,color,48,thickness);
        });
    };
    if(settings.outline&&settings.outlineThickness>0)pass(ImGui::ColorConvertFloat4ToU32(settings.outlineColor),settings.thickness+2*settings.outlineThickness,settings.outlineThickness);
    pass(ImGui::ColorConvertFloat4ToU32(settings.color),settings.thickness,0);
}
void OutlineControls() {
    bool paletteChanged=false;
    SettingRow("Game Outlines");UICheckbox("##gameOutlines",&OW::Config::gameOutlines);
    SettingTooltip(OW::Config::uiLanguage == 1 ? "仅更改游戏原生轮廓颜色，遮挡由游戏处理。" : "Changes native outline colors only; the game handles occlusion.");
    ImGui::SameLine();paletteChanged|=UI::ColorPicker("outlineVisible",OW::Config::enargb);
    ImGui::SetItemTooltip("%s",L("Visible"));
    ImGui::SameLine();paletteChanged|=UI::ColorPicker("outlineHidden",OW::Config::invisnenargb);
    ImGui::SetItemTooltip("%s",L("Hidden"));
    SettingRow(OW::Config::uiLanguage==1?"选中目标改色":"Selected target color");
    UICheckbox("##outlineTargetEnabled",&OW::Config::gameOutlineTargetColor);
    ImGui::SameLine();paletteChanged|=UI::ColorPicker("outlineTarget",OW::Config::outlineTargetColor);
    ImGui::SetItemTooltip("%s",L("Target"));
    const bool contourCaptureEnabled=V2::Vision::GetService().GetSettings().enabled;
    ImGui::BeginDisabled(contourCaptureEnabled);
    SettingRow("Rainbow Color");UICheckbox("##outlineRainbow",&OW::Config::gameOutlineRainbow);
    ImGui::EndDisabled();
    if(contourCaptureEnabled)SettingTooltip(OW::Config::uiLanguage==1?"轮廓采集开启时使用固定三色，彩虹暂停。":"Capture uses three fixed colors; rainbow is paused while capture is enabled.");
    if(paletteChanged)V2::Vision::SyncOutlinePalette();
#ifndef UNLEASHED_OBSERVATION_ONLY
    namespace Brightness = OW::OutlineBrightness;
    static float brightnessDraft = 100.f;
    const auto brightnessCommand = Brightness::Snapshot();
    SettingRow("Native Outline Brightness (%)");
    const float buttonWidth = ImGui::CalcTextSize(L("Apply")).x + ImGui::GetStyle().FramePadding.x * 2;
    ImGui::SetNextItemWidth((std::max)(80.f, ImGui::GetContentRegionAvail().x - buttonWidth - ImGui::GetStyle().ItemSpacing.x));
    ImGui::InputFloat("##outlineBrightnessPercentDraft", &brightnessDraft, 0.f, 0.f, "%.7g");
    if (BeginSettingTooltip()) {
        ImGui::TextUnformatted(L("Uses the game's original outline brightness setting, independently of Game Outlines. Values above 100% are allowed."));
        ImGui::TextUnformatted(L("New objects use the new brightness; existing objects may not update immediately. Reopening game settings limits the value to the game's range."));
        EndSettingTooltip();
    }
    ImGui::SameLine();
    ImGui::BeginDisabled(!Brightness::Valid(brightnessDraft));
    if (ImGui::Button(L("Apply"))) Brightness::Submit(brightnessDraft);
    ImGui::EndDisabled();
    if (!Brightness::Valid(brightnessDraft)) SettingTooltip(L("Enter a finite percentage of zero or greater."));
    if (brightnessCommand.enabled || Brightness::RestoreCount.load()) {
        if (ImGui::Button(L("Restore Original Setting"))) Brightness::Disable();
        ImGui::SameLine();
        ImGui::Text(L("Submitted Brightness: %.7g%%"), brightnessCommand.value);
        ImGui::Text(L("Applied: %u / Failed: %u / Restore pending: %u"), Brightness::AppliedCount.load(),
            Brightness::FailureCount.load(), Brightness::RestoreCount.load());
    }
    const char* brightnessStatus = nullptr;
    switch (Brightness::LastResult.load()) {
    case Brightness::Result::Restored:
        brightnessStatus = "Original brightness setting restored."; break;
    case Brightness::Result::Changed:
        brightnessStatus = "The game changed this setting. Click Apply to use your value again."; break;
    case Brightness::Result::InvalidValue:
        brightnessStatus = "Enter a finite percentage of zero or greater."; break;
    case Brightness::Result::UnsupportedProfile:
    case Brightness::Result::IdentityMismatch:
    case Brightness::Result::SignatureMismatch:
        brightnessStatus = "This game version is not supported. Use the game's brightness setting until support is available."; break;
    case Brightness::Result::ReadFailed:
        brightnessStatus = "Could not read brightness. Check the game connection, then click Apply again."; break;
    case Brightness::Result::SettingMissing:
    case Brightness::Result::AmbiguousSetting:
    case Brightness::Result::InvalidTarget:
        brightnessStatus = "Could not locate the brightness setting. Reconnect to the game, then click Apply again."; break;
    case Brightness::Result::TargetChanged:
        brightnessStatus = "The game setting changed while applying. Click Apply again."; break;
    case Brightness::Result::WriteFailed:
        brightnessStatus = "Could not apply brightness. Check the game connection, then click Apply again."; break;
    case Brightness::Result::WriteReadbackFailed:
    case Brightness::Result::WriteMismatch:
        brightnessStatus = "Brightness could not be confirmed after applying. Check the game connection before trying again."; break;
    case Brightness::Result::RestorePending:
        brightnessStatus = "Waiting to restore the original setting. Keep the game connected."; break;
    case Brightness::Result::RestoreReadFailed:
    case Brightness::Result::RestoreWriteFailed:
    case Brightness::Result::RestoreReadbackFailed:
    case Brightness::Result::RestoreMismatch:
        brightnessStatus = "Could not confirm the original setting was restored. Check the game connection, then try Restore Original Setting again."; break;
    default: break;
    }
    if (brightnessStatus) ImGui::TextWrapped("%s", L(brightnessStatus));
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
    if(OW::Config::healthbar || OW::Config::healthbar2) {
        const auto& st=style(Element::Health);
        if(settings.healthBarStyle==1) {
            const auto a=position(Element::Health,left-5,top);
            draw->AddRectFilled(a,ImVec2(a.x+st.thickness,a.y+bottom-top),kColControl);
            draw->AddRectFilled(ImVec2(a.x,a.y+40),ImVec2(a.x+st.thickness,a.y+bottom-top),color(Element::Health));
        } else {
            const float w=settings.healthBarStyle==2?right-left:st.size*5,h=st.thickness*(settings.healthBarStyle==2?1:3);
            const auto a=position(Element::Health,cx-w*.5f,top-h-4);
            draw->AddRectFilled(a,ImVec2(a.x+w,a.y+h),kColControl);
            if(settings.healthBarStyle==2)draw->AddRectFilled(a,ImVec2(a.x+w*.8f,a.y+h),color(Element::Health));
            else {
                draw->AddRectFilled(a,ImVec2(a.x+w*.5f,a.y+h),IM_COL32(245,245,245,255));
                draw->AddRectFilled(ImVec2(a.x+w*.5f,a.y),ImVec2(a.x+w*.7f,a.y+h),IM_COL32(255,150,35,255));
                draw->AddRectFilled(ImVec2(a.x+w*.7f,a.y),ImVec2(a.x+w*.8f,a.y+h),IM_COL32(70,170,255,255));
                for(int i=1;i<10;++i)draw->AddLine(ImVec2(a.x+w*i/10,a.y),ImVec2(a.x+w*i/10,a.y+h),IM_COL32(0,0,0,150));
            }
        }
    }
    if(settings.worldUltimate)text(Element::Ultimate,right+18,top,"67%");
    if(OW::Config::skillinfo){const float size=style(Element::Skills).size;for(int i=0;i<2;++i){const auto a=position(Element::Skills,right+8+i*(size+3),top+20);draw->AddRect(a,ImVec2(a.x+size,a.y+size),color(Element::Skills),3,0,style(Element::Skills).thickness);draw->AddText(ImGui::GetFont(),size*.67f,ImVec2(a.x+2,a.y+2),color(Element::Skills),i?"--":"4");}}
    if(OW::Config::dist)text(Element::Distance,cx,bottom+4,"14 m");
    if(settings.prediction)draw->AddCircle(position(Element::Prediction,cx+25,top+75),style(Element::Prediction).size*.25f,color(Element::Prediction),20,style(Element::Prediction).thickness);
    if(OW::Config::draw_hitbox)draw->AddCircle(position(Element::Hitbox,cx,top+30),16,color(Element::Hitbox),24,style(Element::Hitbox).thickness);
    if(OW::Config::drawline)draw->AddLine(ImVec2(cx,p.y+315),position(Element::Snapline,cx,bottom),color(Element::Snapline),style(Element::Snapline).thickness);
    if(OW::Config::eyeray)draw->AddLine(position(Element::EyeRay,cx,top+30),position(Element::EyeRay,right+30,top+40),color(Element::EyeRay),style(Element::EyeRay).thickness);
    if(settings.offscreen){const auto tip=position(Element::Offscreen,p.x+width-18,top+110);const float size=style(Element::Offscreen).size;draw->AddTriangleFilled(tip,ImVec2(tip.x-size,tip.y-size*.5f),ImVec2(tip.x-size,tip.y+size*.5f),color(Element::Offscreen));}
    if(settings.centerArrow){
        const auto& st=style(Element::CenterArrow);const ImVec2 center(cx+st.offsetX,top+100+st.offsetY);
        const float radius=(std::min)(st.radius,(std::min)(width*.5f-8.f,150.f));
        const float size=st.size;const ImU32 c=color(Element::CenterArrow);
        for(int i=0;i<6;++i){const float a=IM_PI*2.f*float(i)/6.f;const ImVec2 dir(std::cos(a),std::sin(a));const ImVec2 tip(center.x+dir.x*radius,center.y+dir.y*radius);const ImVec2 back(tip.x-dir.x*size,tip.y-dir.y*size);const ImVec2 side(-dir.y*size*.5f,dir.x*size*.5f);draw->AddTriangleFilled(tip,ImVec2(back.x+side.x,back.y+side.y),ImVec2(back.x-side.x,back.y-side.y),c);}
    }
    if(OW::Config::crosscircle){const auto& crosshair=settings.crosshair;DrawCrosshairPreview(draw,ImVec2(cx+crosshair.offsetX,top+100+crosshair.offsetY),crosshair);}
}

}
void UI::VisualsPage() {
    auto settings=V2::HeroSettings::Snapshot()->display;
    bool changed=false;
    const float width=ImGui::GetContentRegionAvail().x;
    const bool columns=width>=650*(std::max)(1.f,ImGui::GetFontSize()/15.f);
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
        EspSettingRow("Dynamic Font Scaling");changed|=UICheckbox("##dynamicFont",&settings.dynamicFont);
        changed|=ElementRow(settings,Element::Box,OW::Config::draw_info);
        changed|=ElementRow(settings,Element::Name,OW::Config::name);
        changed|=ElementRow(settings,Element::BattleTag,OW::Config::drawbattletag);
        bool healthEnabled=OW::Config::healthbar || OW::Config::healthbar2;
        changed|=ElementRow(settings,Element::Health,healthEnabled);
        OW::Config::healthbar=healthEnabled;OW::Config::healthbar2=false;
        changed|=ElementRow(settings,Element::Distance,OW::Config::dist);
        changed|=ElementRow(settings,Element::Avatar,OW::Config::draw_avatar);
        changed|=ElementRow(settings,Element::Ultimate,settings.worldUltimate);
        changed|=ElementRow(settings,Element::Skeleton,OW::Config::draw_skel);
        changed|=ElementRow(settings,Element::Skills,OW::Config::skillinfo);
        changed|=ElementRow(settings,Element::Prediction,settings.prediction);
        changed|=ElementRow(settings,Element::Hitbox,OW::Config::draw_hitbox);
        changed|=CustomStyleRow(OW::Config::uiLanguage==1?"碰撞体轮廓":"Hitbox contour","hitboxContour",settings.hitboxContourStyle,settings.hitboxContour);
        changed|=CustomStyleRow(OW::Config::uiLanguage==1?"碰撞体填色":"Hitbox fill","hitboxFill",settings.hitboxFillStyle,settings.hitboxFill);
        changed|=ElementRow(settings,Element::Offscreen,settings.offscreen);
        changed|=ElementRow(settings,Element::CenterArrow,settings.centerArrow);
        changed|=ElementRow(settings,Element::Snapline,OW::Config::drawline);
        changed|=ElementRow(settings,Element::EyeRay,OW::Config::eyeray);
        changed|=CrosshairRow(settings,OW::Config::crosscircle);
    });
    Card("Game Outlines",[]{OutlineControls();});
    Card("World",[&]{
        auto& world=settings.world;
        SettingRow("Enabled");changed|=UICheckbox("##worldEnabled",&world.enabled);
        SettingTooltip(OW::Config::uiLanguage == 1 ? "物品识别与字段待现场核验；轮胎缺少可靠身份" : "Object capture pending live validation; tire identity unavailable");
        SettingRow("Box");changed|=UICheckbox("##worldBox",&world.box);
        SettingRow("Name");changed|=UICheckbox("##worldName",&world.name);
        SettingRow("Icon");changed|=UICheckbox("##worldIcon",&world.icon);
        SettingRow("Distance");changed|=UICheckbox("##worldDistance",&world.distance);
        SettingRow("Hide Friendly Objects");changed|=UICheckbox("##worldHideTeam",&world.hideTeam);
        SettingRow("Health Bar");changed|=UICheckbox("##worldHealth",&world.health);
        SettingRow("Health Text");changed|=UICheckbox("##worldHealthText",&world.healthText);
        SettingRow("Max Distance");PushControlWidth();changed|=UISlider("##worldMaxDistance",&world.maxDistance,0,1000,Number(0," m"));ImGui::PopItemWidth();
        SettingRow("Font Size");PushControlWidth();changed|=UISlider("##worldSize",&world.size,6,80,Number(0," px"));ImGui::PopItemWidth();
        SettingRow("Icon Size");PushControlWidth();changed|=UISlider("##worldIconSize",&world.iconSize,6,80,Number(0," px"));ImGui::PopItemWidth();
        const auto group=[&](const char* title,V2::WorldObjects::Group category,bool* enabled) {
            ImGui::Spacing();
            if(!ImGui::CollapsingHeader(L(title),ImGuiTreeNodeFlags_DefaultOpen))return;
            ImGui::PushID(title);
            if(enabled){SettingRow("Enabled");changed|=UICheckbox("##groupEnabled",enabled);}
            for(int i=0;i<int(V2::VisualSettings::kObjects.size());++i) {
                const auto& objectType=V2::VisualSettings::kObjects[i];
                if(objectType.group!=category)continue;
                ImGui::PushID(i);SettingRow(L(objectType.name));
                ImGui::BeginDisabled(objectType.id==0||(enabled&&!*enabled));
                changed|=UICheckbox("##object",&world.types[i]);ImGui::SameLine();changed|=UI::ColorPicker("color",world.colors[i]);ImGui::EndDisabled();
                ImGui::SetItemTooltip("%s",objectType.id==0?L("Identity unavailable"):L("Candidate identification; live verification required"));
                ImGui::PopID();
            }
            ImGui::PopID();
        };
        group("Health Packs",V2::WorldObjects::Group::Health,nullptr);
        group("Deployables",V2::WorldObjects::Group::Deployable,&world.deployables);
        group("Projectiles",V2::WorldObjects::Group::Projectile,&world.projectiles);
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
static void DrawFovRingCard(OW::Config::FovRingSlotKind kind,
                              int slotIndex,
                              bool slotEnabled,
                              float slotFovDeg) {
    OW::Config::FovRingSlotStyle& style = OW::Config::FovRingStyleFor(kind, slotIndex);
    style = OW::Config::ClampFovRingStyle(style, kind, slotIndex);

    bool changed = false;
    ImGui::PushID(static_cast<int>(kind) * 100 + slotIndex);

    char slotLabel[32] = {};
    std::snprintf(slotLabel, sizeof(slotLabel), L("Slot %d"), slotIndex + 1);
    ImGui::PushStyleVar(ImGuiStyleVar_ChildRounding,10.f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding,ImVec2(12,12));
    ImGui::PushStyleColor(ImGuiCol_ChildBg,kColPanelSoft);
    ImGui::BeginChild("RingCard",ImVec2(0,0),
        ImGuiChildFlags_Borders|ImGuiChildFlags_AutoResizeY|ImGuiChildFlags_AlwaysAutoResize|ImGuiChildFlags_AlwaysUseWindowPadding,
        ImGuiWindowFlags_NoScrollbar|ImGuiWindowFlags_NoScrollWithMouse);
    ImGui::TextUnformatted(slotLabel);
    ImGui::TextColored(ImGui::ColorConvertU32ToFloat4(slotEnabled ? kColTextMuted : kColTextDim),
                       "%s %.0f deg",
                       slotEnabled ? L("On") : L("Off"),
                       OW::Config::ClampFovDeg(slotFovDeg));

    ImGui::Spacing();
    changed |= UICheckbox(L("Show"), &style.visible);
    ImGui::SameLine();
    changed |= UI::ColorPicker("##color",style.color);
    ImGui::SetItemTooltip("%s",L("Color"));
    ImGui::TextDisabled("%s",L("Thickness"));
    ImGui::SetNextItemWidth(-FLT_MIN);
    changed |= UISlider("##thickness", &style.thickness, 0.5f, 6.0f, Number(1, " px"));

    static const char* kLineStyles[] = { "Solid", "Dashed" };
    ImGui::TextDisabled("%s",OW::Config::uiLanguage==1?"线型":"Line style");
    ImGui::SetNextItemWidth(-FLT_MIN);
    changed |= UISelect("##lineStyle", &style.lineStyle, kLineStyles, IM_ARRAYSIZE(kLineStyles));

    changed |= UICheckbox(L("Label"), &style.showLabel);

    if (changed)
        style = OW::Config::ClampFovRingStyle(style, kind, slotIndex);

    ImGui::EndChild();
    ImGui::PopStyleColor();
    ImGui::PopStyleVar(2);
    ImGui::PopID();
}

static void DrawFovRingStyleGroup(OW::Config::FovRingSlotKind kind,
                                  const HeroOption& selectedHero) {
    ImGui::TextUnformatted(L("Aim Slot Rings"));
    ImGui::Spacing();
    const float scale=(std::max)(1.f,ImGui::GetFontSize()/15.f);
    const float gap=12.f*scale;
    const int columns=std::clamp(int((ImGui::GetContentRegionAvail().x+gap)/(190.f*scale+gap)),1,4);
    ImGui::PushStyleVar(ImGuiStyleVar_CellPadding,ImVec2(gap*.5f,gap*.5f));
    if(ImGui::BeginTable("FovRingCards",columns,ImGuiTableFlags_SizingStretchSame|ImGuiTableFlags_NoPadOuterX)) {
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

            ImGui::TableNextColumn();
            DrawFovRingCard(kind, slotIndex, selectedHero.heroId == 0 || slotEnabled, fovDeg);
        }
        ImGui::EndTable();
    }
    ImGui::PopStyleVar();
}

void DrawThemeFovRingsPage() {
    const HeroOption& selectedHero = CurrentHeroOption();
    DrawFovRingStyleGroup(OW::Config::FovRingSlotKind::Aim, selectedHero);
}

void UI::ThemePage() {
    ImGui::PushID("ThemePage");

    state.themeSubTab = ImClamp(state.themeSubTab, 0, kThemeSubTabCount - 1);
    if (state.themeSubTab == 0)
        VisualsPage();
    else
        DrawThemeFovRingsPage();

    ImGui::PopID();
}

// =====================================================================
// UI::MiscPage
// =====================================================================
