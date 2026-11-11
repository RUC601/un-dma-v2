#pragma once
#include "Utils/Config.hpp"
#include "ui/EditorSupport.hpp"
#include "ui/Theme.hpp"
#include <algorithm>
#include <cmath>

namespace UI {
inline bool DynamicFovEditor(OW::Config::DynamicFovPreset& preset) {
    using namespace OW::Config;
    preset=ValidateDynamicFovPresetValue(preset);
    bool changed=false;
    ImGui::PushID(preset.id);
    auto* storage=ImGui::GetStateStorage();
    const auto selectedId=ImGui::GetID("selectedPoint");
    int selected=std::clamp(storage->GetInt(selectedId,0),0,preset.pointCount-1);
    float view=storage->GetFloat(ImGui::GetID("viewDistance"),100);
    const bool zh=uiLanguage==1;
    ImGui::SetNextItemWidth(150);
    if(ImGui::SliderFloat(zh?"距离视野 (m)":"Distance view (m)",&view,10,500,"%.0f"))
        storage->SetFloat(ImGui::GetID("viewDistance"),view);
    const ImVec2 start=ImGui::GetCursorScreenPos();
    const ImVec2 area((std::max)(150.f,ImGui::GetContentRegionAvail().x),220);
    const ImVec2 origin(start.x+38,start.y+12),size(area.x-50,area.y-40);
    ImGui::BeginDisabled(preset.worldDiameterM > 0);
    ImGui::InvisibleButton("##dynamicFovGraph",area);
    const auto graph=ImGui::GetItemID();
    auto* draw=ImGui::GetWindowDrawList();
    draw->AddRectFilled(start,ImVec2(start.x+area.x,start.y+area.y),Theme::kColControl,6);
    auto screen=[&](float distance,float fov) {
        return ImVec2(origin.x+distance/view*size.x,origin.y+(1-fov/kMaxFovDeg)*size.y);
    };
    for(int i=0;i<=4;++i) {
        const float fraction=i*.25f;
        draw->AddLine(screen(view*fraction,0),screen(view*fraction,kMaxFovDeg),Theme::kColStroke);
        draw->AddLine(screen(0,kMaxFovDeg*fraction),screen(view,kMaxFovDeg*fraction),Theme::kColStroke);
        char text[24];std::snprintf(text,sizeof(text),"%.0f",view*fraction);
        draw->AddText(ImVec2(origin.x+size.x*fraction-8,origin.y+size.y+5),Theme::kColTextDim,text);
        std::snprintf(text,sizeof(text),"%.0f",kMaxFovDeg*fraction);
        draw->AddText(ImVec2(start.x+2,screen(0,kMaxFovDeg*fraction).y-6),Theme::kColTextDim,text);
    }
    const auto mouse=ImGui::GetIO().MousePos;
    auto addPoint=[&](float distance,float fov) {
        if(preset.pointCount>=kMaxDynamicFovPoints)return false;
        distance=std::clamp(distance,0.f,kMaxDynamicFovDistanceM);
        int index=0;
        while(index<preset.pointCount && preset.points[index].distanceM<distance)++index;
        if((index>0 && distance-preset.points[index-1].distanceM<.01f)||
            (index<preset.pointCount && preset.points[index].distanceM-distance<.01f))return false;
        for(int i=preset.pointCount;i>index;--i)preset.points[i]=preset.points[i-1];
        preset.points[index]={distance,ClampFovDeg(fov)};
        ++preset.pointCount;selected=index;return true;
    };
    if(ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(0))
        changed|=addPoint((mouse.x-origin.x)/size.x*view,(1-(mouse.y-origin.y)/size.y)*kMaxFovDeg);
    else if(ImGui::IsItemClicked()) {
        float nearest=144;
        for(int i=0;i<preset.pointCount;++i) {
            const auto p=screen(preset.points[i].distanceM,preset.points[i].fovDeg);
            const float distance=(p.x-mouse.x)*(p.x-mouse.x)+(p.y-mouse.y)*(p.y-mouse.y);
            if(distance<nearest){nearest=distance;selected=i;}
        }
        storage->SetInt(graph,nearest<144?selected:-1);
    }
    const int dragging=storage->GetInt(graph,-1);
    if(ImGui::IsItemActive() && ImGui::IsMouseDragging(0,1) && dragging>=0 && dragging<preset.pointCount) {
        auto& p=preset.points[dragging];
        const float low=dragging?preset.points[dragging-1].distanceM+.01f:0;
        const float high=dragging+1<preset.pointCount?preset.points[dragging+1].distanceM-.01f:500;
        p.distanceM=std::clamp((mouse.x-origin.x)/size.x*view,low,high);
        p.fovDeg=ClampFovDeg((1-(mouse.y-origin.y)/size.y)*kMaxFovDeg);
        selected=dragging;changed=true;
    }
    draw->PushClipRect(origin,ImVec2(origin.x+size.x,origin.y+size.y),true);
    auto previous=screen(0,EvaluateDynamicFovPreset(preset,0,kDefaultFovDeg));
    for(int i=1;i<=160;++i) {
        const float distance=view*i/160;
        const auto next=screen(distance,EvaluateDynamicFovPreset(preset,distance,kDefaultFovDeg));
        draw->AddLine(previous,next,Theme::kColAccent,2);previous=next;
    }
    for(int i=0;preset.worldDiameterM<=0 && i<preset.pointCount;++i) {
        const auto p=screen(preset.points[i].distanceM,preset.points[i].fovDeg);
        draw->AddCircleFilled(p,i==selected?6.f:4.f,Theme::kColText,24);
        if(i==selected)draw->AddCircle(p,9,Theme::kColAccent,24,2);
    }
    draw->PopClipRect();
    ImGui::SetItemTooltip("%s",zh?"双击添加节点；拖动节点调整距离和 FOV":"Double-click to add; drag a point to edit distance and FOV");
    ImGui::BeginDisabled(preset.pointCount>=kMaxDynamicFovPoints);
    if(ImGui::Button(zh?"添加节点":"Add point")) {
        int gap=0;
        for(int i=1;i<preset.pointCount-1;++i)
            if(preset.points[i+1].distanceM-preset.points[i].distanceM>preset.points[gap+1].distanceM-preset.points[gap].distanceM)gap=i;
        const float distance=(preset.points[gap].distanceM+preset.points[gap+1].distanceM)*.5f;
        changed|=addPoint(distance,EvaluateDynamicFovPreset(preset,distance,kDefaultFovDeg));
    }
    ImGui::EndDisabled();ImGui::SameLine();
    ImGui::BeginDisabled(preset.pointCount<=2);
    if(ImGui::Button(zh?"删除节点":"Delete point")) {
        for(int i=selected;i<preset.pointCount-1;++i)preset.points[i]=preset.points[i+1];
        --preset.pointCount;selected=(std::min)(selected,preset.pointCount-1);changed=true;
    }
    ImGui::EndDisabled();
    for(int i=0;i<preset.pointCount;++i) {
        ImGui::SameLine();ImGui::PushID(i);
        char label[16];std::snprintf(label,sizeof(label),"%02d",i+1);
        if(ImGui::Selectable(label,i==selected,0,ImVec2(26,ImGui::GetFrameHeight())))selected=i;
        ImGui::PopID();
    }
    auto& point=preset.points[selected];
    const float low=selected?preset.points[selected-1].distanceM+.01f:0;
    const float high=selected+1<preset.pointCount?preset.points[selected+1].distanceM-.01f:500;
    ImGui::SetNextItemWidth(150);
    changed|=ImGui::DragFloat(zh?"距离 (m)":"Distance (m)",&point.distanceM,.1f,low,high,"%.2f",ImGuiSliderFlags_AlwaysClamp);
    ImGui::SetNextItemWidth(150);
    changed|=ImGui::DragFloat("FOV (deg)",&point.fovDeg,.1f,kMinFovDeg,kMaxFovDeg,"%.2f",ImGuiSliderFlags_AlwaysClamp);
    point.distanceM=std::clamp(point.distanceM,low,high);point.fovDeg=ClampFovDeg(point.fovDeg);
    ImGui::EndDisabled();
    float preview=storage->GetFloat(ImGui::GetID("previewDistance"),10);
    ImGui::SetNextItemWidth(150);
    if(ImGui::SliderFloat(zh?"预览距离 (m)":"Preview distance (m)",&preview,0,500,"%.1f"))storage->SetFloat(ImGui::GetID("previewDistance"),preview);
    ImGui::SameLine();ImGui::Text("FOV %.2f deg",EvaluateDynamicFovPreset(preset,preview,kDefaultFovDeg));
    storage->SetInt(selectedId,selected);
    ImGui::PopID();return changed;
}
}
