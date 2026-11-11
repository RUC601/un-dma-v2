#pragma once
#include "Renderer/VisualSettings.hpp"
#include <rapidjson/document.h>
#include <algorithm>
#include <cmath>
namespace V2::VisualSettings {
template<class Allocator> inline rapidjson::Value Encode(const Settings& settings,Allocator& allocator) {
    rapidjson::Value out(rapidjson::kObjectType);
    auto boolean=[&](const char* key,bool value){out.AddMember(rapidjson::StringRef(key),value,allocator);};
    boolean("enabled",settings.enabled);boolean("enemies",settings.enemies);boolean("allies",settings.allies);boolean("visibleOnly",settings.visibleOnly);
    boolean("dynamicFont",settings.dynamicFont);boolean("worldUltimate",settings.worldUltimate);boolean("prediction",settings.prediction);boolean("offscreen",settings.offscreen);
    boolean("pauseOnChat",settings.pauseOnChat);boolean("pauseOnFocusLoss",settings.pauseOnFocusLoss);
    out.AddMember("healthBarStyle",settings.healthBarStyle,allocator);
    out.AddMember("boxType",settings.boxType,allocator);out.AddMember("nameType",settings.nameType,allocator);
    auto text=[&](const char* key,const std::string& value){rapidjson::Value v;v.SetString(value.c_str(),allocator);out.AddMember(rapidjson::StringRef(key),v,allocator);};
    out.AddMember("infoX",settings.infoX,allocator);out.AddMember("infoY",settings.infoY,allocator);out.AddMember("infoScale",settings.infoScale,allocator);
    text("font",settings.font);text("chineseFont",settings.chineseFont);
    auto color=[&](const ImVec4& c){rapidjson::Value value(rapidjson::kArrayType);for(int i=0;i<4;++i)value.PushBack((&c.x)[i],allocator);return value;};
    rapidjson::Value styles(rapidjson::kArrayType);
    for(const auto& style:settings.elements){rapidjson::Value v(rapidjson::kObjectType);
        v.AddMember("visible",color(style.visible),allocator);v.AddMember("hidden",color(style.hidden),allocator);v.AddMember("ally",color(style.ally),allocator);v.AddMember("target",color(style.target),allocator);
        v.AddMember("size",style.size,allocator);v.AddMember("thickness",style.thickness,allocator);v.AddMember("offsetX",style.offsetX,allocator);v.AddMember("offsetY",style.offsetY,allocator);styles.PushBack(v,allocator);}
    out.AddMember("elements",styles,allocator);
    rapidjson::Value radar(rapidjson::kObjectType);
    radar.AddMember("radius",settings.radar.radius,allocator);radar.AddMember("rangeM",settings.radar.rangeM,allocator);radar.AddMember("markerSize",settings.radar.markerSize,allocator);
    radar.AddMember("backgroundAlpha",settings.radar.backgroundAlpha,allocator);radar.AddMember("allies",settings.radar.allies,allocator);radar.AddMember("rotate",settings.radar.rotate,allocator);
    radar.AddMember("enemy",color(settings.radar.enemy),allocator);radar.AddMember("ally",color(settings.radar.ally),allocator);out.AddMember("radar",radar,allocator);
    const auto& w=settings.world;rapidjson::Value world(rapidjson::kObjectType),types(rapidjson::kArrayType),colors(rapidjson::kArrayType);
    world.AddMember("enabled",w.enabled,allocator);world.AddMember("box",w.box,allocator);world.AddMember("name",w.name,allocator);world.AddMember("icon",w.icon,allocator);world.AddMember("distance",w.distance,allocator);
    world.AddMember("hideTeam",w.hideTeam,allocator);world.AddMember("health",w.health,allocator);world.AddMember("healthText",w.healthText,allocator);
    world.AddMember("deployables",w.deployables,allocator);world.AddMember("projectiles",w.projectiles,allocator);
    world.AddMember("maxDistance",w.maxDistance,allocator);world.AddMember("size",w.size,allocator);world.AddMember("iconSize",w.iconSize,allocator);
    for(size_t i=0;i<w.types.size();++i){types.PushBack(bool(w.types[i]),allocator);colors.PushBack(color(w.colors[i]),allocator);}
    world.AddMember("types",types,allocator);world.AddMember("colors",colors,allocator);out.AddMember("world",world,allocator);return out;
}
inline Settings Decode(const rapidjson::Value& root,Settings settings={}) {
    if(!root.IsObject())return settings;
    auto boolean=[](const rapidjson::Value& object,const char* key,bool& value){if(object.HasMember(key)&&object[key].IsBool())value=object[key].GetBool();};
    auto number=[](const rapidjson::Value& object,const char* key,float& value,float low,float high){if(object.HasMember(key)&&object[key].IsNumber()&&std::isfinite(object[key].GetFloat()))value=std::clamp(object[key].GetFloat(),low,high);};
    auto color=[](const rapidjson::Value& object,const char* key,ImVec4& value){if(object.HasMember(key)&&object[key].IsArray()&&object[key].Size()==4)for(unsigned i=0;i<4;++i)if(object[key][i].IsNumber()&&std::isfinite(object[key][i].GetFloat()))(&value.x)[i]=std::clamp(object[key][i].GetFloat(),0.f,1.f);};
    number(root,"infoX",settings.infoX,0,1);number(root,"infoY",settings.infoY,0,1);number(root,"infoScale",settings.infoScale,.35f,2.5f);
    boolean(root,"enabled",settings.enabled);boolean(root,"enemies",settings.enemies);boolean(root,"allies",settings.allies);boolean(root,"visibleOnly",settings.visibleOnly);
    boolean(root,"dynamicFont",settings.dynamicFont);boolean(root,"worldUltimate",settings.worldUltimate);boolean(root,"prediction",settings.prediction);boolean(root,"offscreen",settings.offscreen);
    boolean(root,"pauseOnChat",settings.pauseOnChat);boolean(root,"pauseOnFocusLoss",settings.pauseOnFocusLoss);
    if(root.HasMember("healthBarStyle")&&root["healthBarStyle"].IsInt())settings.healthBarStyle=std::clamp(root["healthBarStyle"].GetInt(),1,3);
    if(root.HasMember("boxType")&&root["boxType"].IsInt())settings.boxType=std::clamp(root["boxType"].GetInt(),0,2);
    if(root.HasMember("nameType")&&root["nameType"].IsInt())settings.nameType=std::clamp(root["nameType"].GetInt(),0,2);
    if(root.HasMember("font")&&root["font"].IsString()&&root["font"].GetStringLength()<1024)settings.font=root["font"].GetString();
    if(root.HasMember("chineseFont")&&root["chineseFont"].IsString()&&root["chineseFont"].GetStringLength()<1024)settings.chineseFont=root["chineseFont"].GetString();
    if(root.HasMember("elements")&&root["elements"].IsArray())for(unsigned i=0;i<root["elements"].Size()&&i<settings.elements.size();++i){const auto& v=root["elements"][i];if(!v.IsObject())continue;auto& s=settings.elements[i];
        color(v,"visible",s.visible);color(v,"hidden",s.hidden);color(v,"ally",s.ally);color(v,"target",s.target);
        number(v,"size",s.size,6,80);number(v,"thickness",s.thickness,.5f,8);number(v,"offsetX",s.offsetX,-300,300);number(v,"offsetY",s.offsetY,-300,300);}
    if(root.HasMember("radar")&&root["radar"].IsObject()){const auto& v=root["radar"];auto& r=settings.radar;
        number(v,"radius",r.radius,40,600);number(v,"rangeM",r.rangeM,5,500);number(v,"markerSize",r.markerSize,3,48);number(v,"backgroundAlpha",r.backgroundAlpha,0,1);
        boolean(v,"allies",r.allies);boolean(v,"rotate",r.rotate);color(v,"enemy",r.enemy);color(v,"ally",r.ally);}
    if(root.HasMember("world")&&root["world"].IsObject()){const auto& v=root["world"];auto& w=settings.world;
        boolean(v,"enabled",w.enabled);boolean(v,"box",w.box);boolean(v,"name",w.name);boolean(v,"icon",w.icon);boolean(v,"distance",w.distance);
        boolean(v,"hideTeam",w.hideTeam);boolean(v,"health",w.health);boolean(v,"healthText",w.healthText);
        boolean(v,"deployables",w.deployables);boolean(v,"projectiles",w.projectiles);
        number(v,"maxDistance",w.maxDistance,0,1000);number(v,"size",w.size,6,80);number(v,"iconSize",w.iconSize,6,80);
        if(v.HasMember("types")&&v["types"].IsArray())for(unsigned i=0;i<v["types"].Size()&&i<w.types.size();++i)if(v["types"][i].IsBool())w.types[i]=v["types"][i].GetBool();
        if(v.HasMember("colors")&&v["colors"].IsArray())for(unsigned i=0;i<v["colors"].Size()&&i<w.colors.size();++i) {
            const auto& a=v["colors"][i];if(!a.IsArray()||a.Size()!=4)continue;
            for(unsigned c=0;c<4;++c)if(a[c].IsNumber()&&std::isfinite(a[c].GetFloat()))(&w.colors[i].x)[c]=std::clamp(a[c].GetFloat(),0.f,1.f);
        }
    }
    return settings;
}
}
