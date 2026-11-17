#pragma once
#include <imgui.h>
#include "Game/WorldObjectIds.hpp"
#include <array>
#include <cstdint>
#include <string>

namespace V2::VisualSettings {
enum class Element:int {Box,Name,BattleTag,Health,Resources,Distance,Skeleton,Avatar,Skills,Ultimate,Prediction,Hitbox,Offscreen,Snapline,EyeRay,Crosshair,CenterArrow,Count};
inline constexpr std::array<const char*,int(Element::Count)> kElementNames{
    "Box","Hero Name","BattleTag","Health Bar","Health Bar 2","Distance","Skeleton","Hero Avatar",
    "World Skill Cooldowns","Ultimate Charge","Prediction point","Hitbox wireframe","Out of FOV arrows","Snapline","Eye Ray","Crosshair","On-screen arrows"};
struct Style {
    ImVec4 visible{1,1,1,1},hidden{1,.25f,.3f,1},target{1,.85f,.2f,1},ally{.3f,1,.6f,1};
    float size=14,thickness=1.5f,radius=160,offsetX=0,offsetY=0;
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
enum class CrosshairStyle:int {Circle,Cross,CircleAndCross,Dot,Square,FourDots,T,Count};
inline constexpr std::array<const char*,int(CrosshairStyle::Count)> kCrosshairStyleNames{
    "Circle","Cross","Circle + Cross","Dot","Square","Four Dots","T"};
struct CrosshairSettings {
    int style=int(CrosshairStyle::CircleAndCross);
    ImVec4 color{1,1,1,1},outlineColor{0,0,0,.9f};
    bool outline=true;
    float size=16,thickness=1.5f,gap=4,dotSize=4,outlineThickness=1,offsetX=0,offsetY=0;
};
struct Settings {
    bool enabled=true,enemies=true,allies=true,visibleOnly=false,dynamicFont=false;
    bool worldUltimate=false,prediction=false,offscreen=false,centerArrow=false;
    int boxType=0,nameType=0;
    int healthBarStyle=1;
    // Independent native hitbox layers. Legacy hitboxMode is read only during migration.
    bool hitboxContour=false;
    bool hitboxFill=false;
    Style hitboxContourStyle=[] { Style value; value.visible=ImVec4(1.f,.85f,.2f,1.f); value.hidden=ImVec4(1.f,.25f,.3f,1.f); return value; }();
    Style hitboxFillStyle=[] { Style value; value.visible=ImVec4(1.f,.15f,.2f,.18f); value.hidden=ImVec4(.65f,.1f,.2f,.18f); return value; }();
    CrosshairSettings crosshair;
    std::array<Style,int(Element::Count)> elements=[] {std::array<Style,int(Element::Count)> values{};
        values[int(Element::Avatar)].size=24;values[int(Element::Skills)].size=18;
        values[int(Element::CenterArrow)].size=12;values[int(Element::CenterArrow)].radius=160;
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
