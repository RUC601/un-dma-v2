#pragma once
#include <imgui.h>
#include "Game/WorldObjectIds.hpp"
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
using WorldObjects::ObjectType;
using WorldObjects::kObjects;
using WorldObjects::ObjectIndex;
struct World {
    bool enabled=false,box=false,name=true,icon=true,distance=true;
    bool hideTeam=false,health=false,healthText=false,deployables=true,projectiles=true;
    float maxDistance=150,size=18,iconSize=24;
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
    int healthBarStyle=1;
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
