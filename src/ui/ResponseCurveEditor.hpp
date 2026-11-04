#pragma once
#include "Game/AimResponseCurve.hpp"
#include "ui/EditorSupport.hpp"
#include <imgui.h>

namespace UI {
inline bool ResponseCurveEditor(OW::AimResponseCurve::Points& points) {
    using namespace OW::AimResponseCurve;
    bool changed=false;
    int preset=3;
    for(int i=0;i<3;++i) if(points==Preset(i)) preset=i;
    const char* labels[]={"Linear","Smooth","Responsive","Custom"};
    SettingRow("Response preset");PushControlWidth();
    if(UISelect("##responsePreset",&preset,labels,4) && preset<3) {points=Preset(preset);changed=true;}
    ImGui::PopItemWidth();
    ImGui::TextWrapped("%s",L("Error / effective FOV -> correction / effective FOV"));
    const ImVec2 origin=ImGui::GetCursorScreenPos();
    const ImVec2 size((std::max)(120.f,ImGui::GetContentRegionAvail().x),160.f);
    ImGui::InvisibleButton("Response curve graph",size);
    auto* draw=ImGui::GetWindowDrawList();
    auto screen=[&](float x,float y){return ImVec2(origin.x+x*size.x,origin.y+(1-y)*size.y);};
    draw->AddRectFilled(origin,ImVec2(origin.x+size.x,origin.y+size.y),ImGui::GetColorU32(ImGuiCol_FrameBg),4);
    for(int i=1;i<4;++i) {
        draw->AddLine(screen(i*.25f,0),screen(i*.25f,1),ImGui::GetColorU32(ImGuiCol_Border));
        draw->AddLine(screen(0,i*.25f),screen(1,i*.25f),ImGui::GetColorU32(ImGuiCol_Border));
    }
    static ImGuiID dragGraph=0;static int dragPoint=-1;
    const ImGuiID graph=ImGui::GetItemID();
    if(ImGui::IsItemClicked()) {
        const auto mouse=ImGui::GetIO().MousePos;
        auto distance=[&](float x,float y){auto p=screen(x,y);return (p.x-mouse.x)*(p.x-mouse.x)+(p.y-mouse.y)*(p.y-mouse.y);};
        dragPoint=distance(points.x1,points.y1)<=distance(points.x2,points.y2)?0:1;dragGraph=graph;
    }
    if(dragGraph==graph && ImGui::IsMouseDown(0) && ImGui::IsItemActive()) {
        const auto mouse=ImGui::GetIO().MousePos;
        float& x=dragPoint==0?points.x1:points.x2;float& y=dragPoint==0?points.y1:points.y2;
        x=(std::clamp)((mouse.x-origin.x)/size.x,0.f,1.f);
        y=(std::clamp)(1-(mouse.y-origin.y)/size.y,0.f,1.f);changed=true;
    }
    auto previous=screen(0,0);
    for(int i=1;i<=128;++i) {
        const float x=i/128.f;auto next=screen(x,Evaluate(points,x));
        draw->AddLine(previous,next,ImGui::GetColorU32(ImGuiCol_PlotLines),2);previous=next;
    }
    draw->AddLine(screen(0,0),screen(points.x1,points.y1),ImGui::GetColorU32(ImGuiCol_TextDisabled));
    draw->AddLine(screen(1,1),screen(points.x2,points.y2),ImGui::GetColorU32(ImGuiCol_TextDisabled));
    draw->AddCircleFilled(screen(points.x1,points.y1),6,ImGui::GetColorU32(ImGuiCol_SliderGrabActive));
    draw->AddCircleFilled(screen(points.x2,points.y2),6,ImGui::GetColorU32(ImGuiCol_SliderGrabActive));
    // Numeric controls provide the same operation without dragging.
    float* values[]={&points.x1,&points.y1,&points.x2,&points.y2};
    const char* names[]={"Control 1 X","Control 1 Y","Control 2 X","Control 2 Y"};
    for(int i=0;i<4;++i) {
        SettingRow(names[i]);PushControlWidth();ImGui::PushID(i);
        changed|=ImGui::DragFloat("##coordinate",values[i],.005f,0,1,"%.3f",ImGuiSliderFlags_AlwaysClamp);
        ImGui::PopID();ImGui::PopItemWidth();
    }
    if(ImGui::Button(L("Reset curve"))) {points={};changed=true;}
    Normalize(points);return changed;
}
}
