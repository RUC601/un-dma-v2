#pragma once
#include "Theme.hpp"
#include <imgui.h>
#include <imgui_internal.h>
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <functional>

namespace UI {
// Layout follows the Reloaded color card; no gradient storage is added to flat colors.
inline bool ColorPicker(const char* id, ImVec4& color, const std::function<void()>& details = {}) {
    ImGui::PushID(id);
    bool changed=false;
    const float height=(std::max)(26.f,ImGui::GetFontSize()+10.f);
    if(ImGui::ColorButton("##swatch",color,ImGuiColorEditFlags_AlphaPreviewHalf,ImVec2(26,height)))
        ImGui::OpenPopup("##colorCard");
#ifdef IMGUI_ENABLE_TEST_ENGINE
    ImGuiContext& g=*ImGui::GetCurrentContext();
    IMGUI_TEST_ENGINE_ITEM_INFO(ImGui::GetItemID(),"Color swatch",g.LastItemData.StatusFlags);
#endif
    ImGui::SetNextWindowSizeConstraints(ImVec2(258,0),ImVec2(258,1000));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding,ImVec2(14,14));
    if(ImGui::BeginPopup("##colorCard")) {
        auto* state=ImGui::GetStateStorage();
        float hue=state->GetFloat(ImGui::GetID("hue"),0);
        float saturation=state->GetFloat(ImGui::GetID("sat"),0);
        float value=state->GetFloat(ImGui::GetID("val"),0);
        bool external=false;
        for(int i=0;i<4;++i)external|=state->GetFloat(ImGui::GetID(i==0?"lastR":i==1?"lastG":i==2?"lastB":"lastA"),-1)!=(&color.x)[i];
        if(external) {
            float h,s,v;ImGui::ColorConvertRGBtoHSV(color.x,color.y,color.z,h,s,v);
            if(s>0&&v>0)hue=h;
            if(v>0)saturation=s;
            value=v;
        }
        const float width=ImGui::GetContentRegionAvail().x;
        auto* draw=ImGui::GetWindowDrawList();
        const auto start=ImGui::GetCursorScreenPos();
        const ImVec2 fieldSize(width,width*.82f);
        float r,g,b;ImGui::ColorConvertHSVtoRGB(hue,1,1,r,g,b);
        const auto hueColor=ImGui::ColorConvertFloat4ToU32(ImVec4(r,g,b,1));
        draw->AddRectFilledMultiColor(start,ImVec2(start.x+width,start.y+fieldSize.y),IM_COL32_WHITE,hueColor,hueColor,IM_COL32_WHITE);
        draw->AddRectFilledMultiColor(start,ImVec2(start.x+width,start.y+fieldSize.y),0,0,IM_COL32_BLACK,IM_COL32_BLACK);
        ImGui::InvisibleButton("##sv",fieldSize);
        if(ImGui::IsItemActive() && ImGui::IsMouseDown(0)) {
            const auto mouse=ImGui::GetIO().MousePos;
            saturation=std::clamp((mouse.x-start.x)/width,0.f,1.f);
            value=std::clamp(1-(mouse.y-start.y)/fieldSize.y,0.f,1.f);changed=true;
        }
        if(ImGui::IsItemFocused()) {
            const float step=ImGui::GetIO().KeyShift?.1f:.01f;
            if(ImGui::IsKeyPressed(ImGuiKey_LeftArrow)){saturation=(std::max)(0.f,saturation-step);changed=true;}
            if(ImGui::IsKeyPressed(ImGuiKey_RightArrow)){saturation=(std::min)(1.f,saturation+step);changed=true;}
            if(ImGui::IsKeyPressed(ImGuiKey_UpArrow)){value=(std::min)(1.f,value+step);changed=true;}
            if(ImGui::IsKeyPressed(ImGuiKey_DownArrow)){value=(std::max)(0.f,value-step);changed=true;}
        }
        const ImVec2 knob(start.x+saturation*width,start.y+(1-value)*fieldSize.y);
        draw->AddCircle(knob,6.5f,IM_COL32(0,0,0,100),24,3);
        draw->AddCircle(knob,5.5f,IM_COL32_WHITE,24,1.7f);
        ImGui::Dummy(ImVec2(0,2));
        auto bar=[&](const char* name,float& channel,bool alpha) {
            const auto p=ImGui::GetCursorScreenPos();
            const float height=10;
            if(alpha) {
                const ImVec4 rgb(r,g,b,1);
                const int cells=int(width/5)+1;
                for(int i=0;i<cells;++i)for(int row=0;row<2;++row)
                    draw->AddRectFilled(ImVec2(p.x+i*5,p.y+row*5),ImVec2((std::min)(p.x+width,p.x+(i+1)*5),p.y+(row+1)*5),
                        (i+row)%2?IM_COL32(140,140,146,255):IM_COL32(200,200,205,255));
                draw->AddRectFilledMultiColor(p,ImVec2(p.x+width,p.y+height),0,ImGui::ColorConvertFloat4ToU32(rgb),ImGui::ColorConvertFloat4ToU32(rgb),0);
            } else {
                for(int i=0;i<6;++i) {
                    ImVec4 a,b; a.w=b.w=1;
                    ImGui::ColorConvertHSVtoRGB(i/6.f,1,1,a.x,a.y,a.z);
                    ImGui::ColorConvertHSVtoRGB((i+1)/6.f,1,1,b.x,b.y,b.z);
                    const auto ca=ImGui::ColorConvertFloat4ToU32(a),cb=ImGui::ColorConvertFloat4ToU32(b);
                    draw->AddRectFilledMultiColor(ImVec2(p.x+width*i/6,p.y),ImVec2(p.x+width*(i+1)/6,p.y+height),ca,cb,cb,ca);
                }
            }
            ImGui::InvisibleButton(name,ImVec2(width,height));
            if(ImGui::IsItemActive()&&ImGui::IsMouseDown(0)) {channel=std::clamp((ImGui::GetIO().MousePos.x-p.x)/width,0.f,1.f);changed=true;}
            if(ImGui::IsItemFocused()) {
                if(ImGui::IsKeyPressed(ImGuiKey_LeftArrow)){channel=(std::max)(0.f,channel-.01f);changed=true;}
                if(ImGui::IsKeyPressed(ImGuiKey_RightArrow)){channel=(std::min)(1.f,channel+.01f);changed=true;}
            }
            draw->AddRect(ImVec2(p.x+channel*width-2,p.y-2),ImVec2(p.x+channel*width+2,p.y+height+2),IM_COL32_WHITE,2,0,1.5f);
        };
        bar("##hue",hue,false);
        ImGui::ColorConvertHSVtoRGB(hue,saturation,value,r,g,b);
        bar("##alpha",color.w,true);
        if(changed) {color.x=r;color.y=g;color.z=b;}
        ImGui::Dummy(ImVec2(0,3));
        int mode=state->GetInt(ImGui::GetID("format"),0);
        ImGui::SetNextItemWidth(57);
        const char* formats[]={"HEX","RGB"};
        if(ImGui::Combo("##format",&mode,formats,2))state->SetInt(ImGui::GetID("format"),mode);
        ImGui::SameLine();
        if(mode==0) {
            char hex[12];std::snprintf(hex,sizeof(hex),"#%02X%02X%02X%02X",int(color.x*255+.5f),int(color.y*255+.5f),int(color.z*255+.5f),int(color.w*255+.5f));
            ImGui::SetNextItemWidth(111);
            if(ImGui::InputText("##hex",hex,sizeof(hex),ImGuiInputTextFlags_CharsUppercase)) {
                const char* digits=hex[0]=='#'?hex+1:hex;
                const auto length=std::strlen(digits);
                unsigned parsed=0;bool valid=length==6||length==8;
                for(size_t i=0;i<length;++i)valid&=(digits[i]>='0'&&digits[i]<='9')||(digits[i]>='A'&&digits[i]<='F');
                if(valid&&std::sscanf(digits,"%x",&parsed)==1) {
                    if(length==6)parsed=(parsed<<8)|unsigned(color.w*255+.5f);
                    color=ImVec4(((parsed>>24)&255)/255.f,((parsed>>16)&255)/255.f,((parsed>>8)&255)/255.f,(parsed&255)/255.f);changed=true;
                }
            }
        } else {
            int rgb[3]={int(color.x*255+.5f),int(color.y*255+.5f),int(color.z*255+.5f)};
            ImGui::SetNextItemWidth(111);
            if(ImGui::InputInt3("##rgb",rgb)) {
                color.x=std::clamp(rgb[0],0,255)/255.f;color.y=std::clamp(rgb[1],0,255)/255.f;color.z=std::clamp(rgb[2],0,255)/255.f;changed=true;
            }
        }
        ImGui::SameLine();
        int percent=int(color.w*100+.5f);ImGui::SetNextItemWidth(44);
        if(ImGui::DragInt("##opacity",&percent,1,0,100,"%d%%",ImGuiSliderFlags_AlwaysClamp)) {color.w=std::clamp(percent,0,100)/100.f;changed=true;}
        if(changed) {
            float h,s,v;ImGui::ColorConvertRGBtoHSV(color.x,color.y,color.z,h,s,v);
            if(s>0&&v>0)hue=h;if(v>0)saturation=s;value=v;
        }
        state->SetFloat(ImGui::GetID("hue"),hue);state->SetFloat(ImGui::GetID("sat"),saturation);state->SetFloat(ImGui::GetID("val"),value);
        for(int i=0;i<4;++i)state->SetFloat(ImGui::GetID(i==0?"lastR":i==1?"lastG":i==2?"lastB":"lastA"),(&color.x)[i]);
        if(details){ImGui::Separator();details();}
        ImGui::EndPopup();
    }
    ImGui::PopStyleVar();ImGui::PopID();return changed;
}
}
