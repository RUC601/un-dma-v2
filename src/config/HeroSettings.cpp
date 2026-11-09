#define NOMINMAX
#define _SILENCE_CXX17_ITERATOR_BASE_CLASS_DEPRECATION_WARNING
#include "Game/HeroSettings.hpp"
#include "config/AlertJson.hpp"
#include "config/VisualSettingsJson.hpp"
#include <atomic>
#include <mutex>
#include <algorithm>
#include <cmath>
#include <cstdlib>

namespace V2::HeroSettings {
namespace {
std::atomic<std::shared_ptr<const Store>> current{std::make_shared<Store>()};
std::mutex edits;
Hero Normalize(Hero hero) {
    for(size_t i=0;i<hero.keys.size();++i)if(!SupportedKey(hero.keys[i]))hero.keys[i]=kDefaultKeys[i];
    auto finite=[](float value,float fallback,float high){return std::isfinite(value)?std::clamp(value,0.f,high):fallback;};
    auto& a=hero.automatic;
    a.altFireHealth=finite(a.altFireHealth,100,500);a.altFireDistance=finite(a.altFireDistance,30,60);
    a.defensiveHealth=finite(a.defensiveHealth,100,500);a.repeatIntervalMs=std::clamp(a.repeatIntervalMs,100,2000);
    hero.alerts=Alerts::Normalize(std::move(hero.alerts));return hero;
}
template<typename Allocator> rapidjson::Value Json(const Hero& h,Allocator& allocator) {
    rapidjson::Value value(rapidjson::kObjectType),keys(rapidjson::kArrayType);
    for(int key:h.keys)keys.PushBack(key,allocator);
    value.AddMember("keys",keys,allocator);
    value.AddMember("altFire",h.automatic.altFire,allocator);value.AddMember("defensive",h.automatic.defensive,allocator);
    value.AddMember("repeatFire",h.automatic.repeatFire,allocator);
    value.AddMember("altFireHealth",h.automatic.altFireHealth,allocator);value.AddMember("altFireDistance",h.automatic.altFireDistance,allocator);
    value.AddMember("defensiveHealth",h.automatic.defensiveHealth,allocator);value.AddMember("repeatIntervalMs",h.automatic.repeatIntervalMs,allocator);
    value.AddMember("alertsEnabled",h.alertsEnabled,allocator);
    rapidjson::Document alerts;alerts.Parse(Alerts::EncodeSettings(h.alerts).c_str());
    rapidjson::Value copy;copy.CopyFrom(alerts,allocator);value.AddMember("alerts",copy,allocator);
    return value;
}
Hero Read(const rapidjson::Value& value,Hero result) {
    if(!value.IsObject())return result;
    auto boolean=[&](const char* name,bool& target){if(value.HasMember(name)&&value[name].IsBool())target=value[name].GetBool();};
    auto number=[&](const char* name,float& target){if(value.HasMember(name)&&value[name].IsNumber())target=value[name].GetFloat();};
    if(value.HasMember("keys")&&value["keys"].IsArray()&&value["keys"].Size()==result.keys.size())
        for(unsigned i=0;i<result.keys.size();++i)if(value["keys"][i].IsInt())result.keys[i]=value["keys"][i].GetInt();
    boolean("altFire",result.automatic.altFire);boolean("defensive",result.automatic.defensive);boolean("repeatFire",result.automatic.repeatFire);
    number("altFireHealth",result.automatic.altFireHealth);number("altFireDistance",result.automatic.altFireDistance);number("defensiveHealth",result.automatic.defensiveHealth);
    if(value.HasMember("repeatIntervalMs")&&value["repeatIntervalMs"].IsInt())result.automatic.repeatIntervalMs=value["repeatIntervalMs"].GetInt();
    boolean("alertsEnabled",result.alertsEnabled);
    if(value.HasMember("alerts")&&value["alerts"].IsObject()) {
        rapidjson::StringBuffer buffer;rapidjson::Writer<rapidjson::StringBuffer> writer(buffer);value["alerts"].Accept(writer);
        result.alerts=Alerts::DecodeSettings(buffer.GetString());
    }
    return Normalize(std::move(result));
}
}
std::shared_ptr<const Store> Snapshot(){return current.load();}
Hero Get(uint64_t hero){const auto state=Snapshot();const auto i=state->heroes.find(hero);return i==state->heroes.end()?state->defaults:i->second;}
AutomaticActions Automatic(uint64_t hero){const auto state=Snapshot();const auto i=state->heroes.find(hero);return (i==state->heroes.end()?state->defaults:i->second).automatic;}
void Set(uint64_t hero,Hero value){if(!hero)return;std::lock_guard lock(edits);auto next=std::make_shared<Store>(*Snapshot());next->heroes[hero]=Normalize(std::move(value));current.store(std::move(next));}
void SetDisplay(VisualSettings::Settings value){std::lock_guard lock(edits);auto next=std::make_shared<Store>(*Snapshot());next->display=std::move(value);current.store(std::move(next));}
int Key(uint64_t hero,Action action){const auto state=Snapshot();const auto i=state->heroes.find(hero);const auto& h=i==state->heroes.end()?state->defaults:i->second;return h.keys.at(int(action));}
unsigned char HidUsage(int vk) {
    if(vk>='A'&&vk<='Z')return static_cast<unsigned char>(4+vk-'A');
    if(vk>='1'&&vk<='9')return static_cast<unsigned char>(30+vk-'1');
    if(vk>=0x70&&vk<=0x7B)return static_cast<unsigned char>(58+vk-0x70);
    switch(vk) {
    case '0':return 39;case 13:return 40;case 27:return 41;case 8:return 42;case 9:return 43;case 32:return 44;
    case 0xBD:return 45;case 0xBB:return 46;case 0xDB:return 47;case 0xDD:return 48;case 0xDC:return 49;
    case 0xBA:return 51;case 0xDE:return 52;case 0xC0:return 53;case 0xBC:return 54;case 0xBE:return 55;case 0xBF:return 56;
    case 0x14:return 57;case 0x2D:return 73;case 0x24:return 74;case 0x21:return 75;case 0x2E:return 76;
    case 0x23:return 77;case 0x22:return 78;case 0x27:return 79;case 0x25:return 80;case 0x28:return 81;case 0x26:return 82;
    case 0x11:case 0xA2:return 0xE0;case 0x10:case 0xA0:return 0xE1;case 0x12:case 0xA4:return 0xE2;
    case 0xA3:return 0xE4;case 0xA1:return 0xE5;case 0xA5:return 0xE6;default:return 0;
    }
}
bool SupportedKey(int vk){return vk==1||vk==2||vk==4||vk==5||vk==6||HidUsage(vk)!=0;}
void Migrate(AutomaticActions legacy,Alerts::Settings alerts) {
    std::lock_guard lock(edits);auto next=std::make_shared<Store>();
    legacy.altFire=legacy.defensive=legacy.repeatFire=false;
    next->defaults.automatic=legacy;next->defaults.alerts=std::move(alerts);next->defaults.alertsEnabled=false;
    current.store(std::move(next));
}
bool Decode(const std::string& json,bool commit) {
    rapidjson::Document document;document.Parse(json.c_str());
    if(document.HasParseError()||!document.IsObject()||!document.HasMember("version")||!document["version"].IsInt()||document["version"].GetInt()!=1)return false;
    auto next=std::make_shared<Store>();
    if(document.HasMember("display"))next->display=VisualSettings::Decode(document["display"]);
    if(document.HasMember("defaults"))next->defaults=Read(document["defaults"],{});
    // Unconfigured heroes always start disabled, even if a malformed default section enables them.
    next->defaults.alertsEnabled=false;
    next->defaults.automatic.altFire=next->defaults.automatic.defensive=next->defaults.automatic.repeatFire=false;
    if(document.HasMember("heroes")&&document["heroes"].IsObject())for(auto i=document["heroes"].MemberBegin();i!=document["heroes"].MemberEnd();++i) {
        char* end=nullptr;const auto hero=std::strtoull(i->name.GetString(),&end,10);
        if(hero&&end&&!*end&&next->heroes.size()<128)next->heroes.emplace(hero,Read(i->value,next->defaults));
    }
    if(commit){std::lock_guard lock(edits);current.store(std::move(next));}return true;
}
std::string Encode() {
    const auto snapshot=Snapshot();rapidjson::Document document;document.SetObject();auto& a=document.GetAllocator();
    document.AddMember("version",1,a);document.AddMember("defaults",Json(snapshot->defaults,a),a);
    document.AddMember("display",VisualSettings::Encode(snapshot->display,a),a);
    rapidjson::Value heroes(rapidjson::kObjectType);
    // Stable order keeps autosave comparisons and diffs independent of hash iteration.
    std::vector<uint64_t> ids;for(const auto& item:snapshot->heroes)ids.push_back(item.first);std::sort(ids.begin(),ids.end());
    for(auto hero:ids){const auto key=std::to_string(hero);rapidjson::Value name;name.SetString(key.c_str(),a);heroes.AddMember(name,Json(snapshot->heroes.at(hero),a),a);}
    document.AddMember("heroes",heroes,a);rapidjson::StringBuffer buffer;rapidjson::Writer<rapidjson::StringBuffer> writer(buffer);document.Accept(writer);
    return {buffer.GetString(),buffer.GetSize()};
}
}
