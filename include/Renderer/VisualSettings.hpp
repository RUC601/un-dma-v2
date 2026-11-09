#pragma once
#include <imgui.h>
#include <array>
#include <cstdint>
#include <string>

namespace V2::VisualSettings {
enum class Element:int {Box,Name,BattleTag,Health,Resources,Distance,Skeleton,Avatar,Skills,Ultimate,Prediction,Hitbox,Offscreen,Snapline,EyeRay,Crosshair,Count};
inline constexpr std::array<const char*,int(Element::Count)> kElementNames{
    "Box","Hero Name","BattleTag","Health Bar","Health Bar 2","Distance","Skeleton","Hero Avatar",
    "World Skill Cooldowns","Ultimate Charge","Prediction point","Model Shapes (research)","Out of FOV arrows","Snapline","Eye Ray","Crosshair"};
struct Style {
    ImVec4 visible{1,1,1,1},hidden{1,.25f,.3f,1},target{1,.85f,.2f,1},ally{.3f,1,.6f,1};
    float size=14,thickness=1.5f,offsetX=0,offsetY=0;
};
struct ObjectType {const char* name;uint64_t id;};
inline constexpr std::array kObjects{
    ObjectType{"Health Packs",0x40000000000005Full},ObjectType{"Ashe Dynamite",0x400000000002533ull},
    ObjectType{"Torb Turret",0x4000000000028B7ull},ObjectType{"Sym Turret",0x400000000002658ull},
    ObjectType{"Junkrat Trap",0x40000000000103Bull},ObjectType{"Junkrat Mine",0x400000000000F9Bull},
    ObjectType{"Widow Mine",0x4000000000000AEull},ObjectType{"Healing Pylon",0x400000000004BCAull},
    ObjectType{"Immortality Field",0x4000000000029A9ull},ObjectType{"B.O.B.",0x4000000000025C3ull},
    ObjectType{"Junkrat Tire",0},ObjectType{"Bastion Ultimate",0x400000000003EFDull}};
inline int ObjectIndex(uint64_t id) {
    if(id==0x400000000000060ull||id==0x40000000000480Aull)return 0;
    for(int i=0;i<int(kObjects.size());++i)if(kObjects[i].id&&kObjects[i].id==id)return i;
    return -1;
}
struct World {
    bool enabled=false,box=false,name=true,icon=true,distance=true;
    float maxDistance=150,size=18;
    std::array<bool,kObjects.size()> types{};
    std::array<ImVec4,kObjects.size()> colors=[] {std::array<ImVec4,kObjects.size()> values;values.fill(ImVec4(.8f,.9f,1,1));return values;}();
};
struct Radar {
    float radius=164,rangeM=91,markerSize=16,backgroundAlpha=.7f;
    bool allies=true,rotate=true;
    ImVec4 enemy{.94f,.35f,.37f,1},ally{.41f,.88f,.55f,1};
};
struct Settings {
    bool enabled=true,enemies=true,allies=true,visibleOnly=false,dynamicFont=false;
    bool worldUltimate=false,prediction=false,offscreen=false;
    int boxType=0,nameType=0;
    std::array<Style,int(Element::Count)> elements=[] {std::array<Style,int(Element::Count)> values{};
        values[int(Element::Avatar)].size=24;values[int(Element::Skills)].size=18;
        values[int(Element::Health)].visible=ImVec4(.2f,1,.35f,1);return values;}();
    float infoX=0,infoY=0,infoScale=1;
    World world;
    Radar radar;
    std::string font="C:/Windows/Fonts/consola.ttf",chineseFont="C:/Windows/Fonts/msyh.ttc";
    bool pauseOnChat=false,pauseOnFocusLoss=false;
};
inline ImVec4 Color(const Style& style,bool enemy,bool visible,bool selected=false) {
    return selected?style.target:!enemy?style.ally:visible?style.visible:style.hidden;
}
}
