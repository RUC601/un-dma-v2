#define NOMINMAX
#include "config/ContourSettings.hpp"
#include "config/FileTransaction.hpp"
#include "vision/ContourService.hpp"
#include "Utils/Config.hpp"
#include "Game/EdOutline.hpp"
#include <rapidjson/document.h>
#include <rapidjson/writer.h>
#include <rapidjson/stringbuffer.h>
#include <algorithm>
#include <cmath>
#include <fstream>
#include <iterator>
#include <type_traits>

namespace V2::Vision {
namespace {
std::string Utf8(const std::wstring& value) {
    if (value.empty()) return {};
    const int count=WideCharToMultiByte(CP_UTF8,WC_ERR_INVALID_CHARS,value.data(),static_cast<int>(value.size()),nullptr,0,nullptr,nullptr);
    if (!count) return {};
    std::string result(count,'\0');
    WideCharToMultiByte(CP_UTF8,WC_ERR_INVALID_CHARS,value.data(),static_cast<int>(value.size()),result.data(),count,nullptr,nullptr);
    return result;
}
bool Wide(const rapidjson::Value& value,std::wstring& out) {
    if(!value.IsString() || value.GetStringLength()>32768) return false;
    const auto length=static_cast<int>(value.GetStringLength());
    if(!length){out.clear();return true;}
    const int count=MultiByteToWideChar(CP_UTF8,MB_ERR_INVALID_CHARS,value.GetString(),length,nullptr,0);
    if(!count)return false;
    out.resize(count);MultiByteToWideChar(CP_UTF8,MB_ERR_INVALID_CHARS,value.GetString(),length,out.data(),count);
    return out.find(L'\0')==std::wstring::npos;
}
Rgb FromColor(const ImVec4& color) {
    // Use the exact bytes written by the native-outline path, including its
    // truncation rule. Rounding independently breaks zero-tolerance matching.
    const auto packed=OW::EdOutline::Color(color.x,color.y,color.z);
    return {static_cast<std::uint8_t>(packed),static_cast<std::uint8_t>(packed>>8),static_cast<std::uint8_t>(packed>>16)};
}
}
void SyncOutlinePalette() {
    const std::array<Rgb,3> colors{FromColor(OW::Config::invisnenargb),FromColor(OW::Config::enargb),FromColor(OW::Config::outlineTargetColor)};
    GetService().UpdateOutlinePalette(colors);
}
namespace Preferences {
namespace {
bool loaded=false, malformed=false;
std::string error;
std::string Path(){return OW::Config::ConfigDirectoryPath()+"/contour-capture.json";}
}
bool Loaded(){return loaded;}
const std::string& Error(){return error;}
std::string Encode(const Settings& s) {
    rapidjson::StringBuffer buffer;rapidjson::Writer<rapidjson::StringBuffer> w(buffer);
    w.StartObject();w.Key("version");w.Int(1);
    // Running/armed state is deliberately absent: a saved profile always opens in standby.
    w.Key("devicePath");const auto path=Utf8(s.capture.devicePath);w.String(path.data(),static_cast<rapidjson::SizeType>(path.size()));
    w.Key("deviceName");const auto name=Utf8(s.capture.deviceName);w.String(name.data(),static_cast<rapidjson::SizeType>(name.size()));
    w.Key("width");w.Int(s.capture.width);w.Key("height");w.Int(s.capture.height);w.Key("fps");w.Double(s.capture.fps);
    w.Key("useOutlineColors");w.Bool(s.useOutlineColors);
    w.Key("colors");w.StartArray();for(const auto& c:s.colors){w.StartArray();w.Uint(c.r);w.Uint(c.g);w.Uint(c.b);w.EndArray();}w.EndArray();
    w.Key("tolerance");w.Int(s.tolerance);w.Key("roiWidth");w.Int(s.roiWidth);w.Key("roiHeight");w.Int(s.roiHeight);
    w.Key("maxFrameAgeMs");w.Double(s.maxFrameAgeMs);w.Key("associationRadiusPx");w.Double(s.associationRadiusPx);
    w.Key("contactRadiusPx");w.Double(s.contactRadiusPx);w.Key("touchDelayMs");w.Int(s.touchDelayMs);
    w.Key("lostGraceMs");w.Int(s.lostGraceMs);w.Key("minComponentPixels");w.Int(s.minComponentPixels);
    w.EndObject();return {buffer.GetString(),buffer.GetSize()};
}
bool Decode(const std::string& json,Settings& output,std::string& failure) {
    failure.clear();rapidjson::Document d;d.Parse(json.data(),json.size());
    if(d.HasParseError()||!d.IsObject()||!d.HasMember("version")||!d["version"].IsInt()||d["version"].GetInt()!=1){failure="Invalid capture settings JSON or version";return false;}
    Settings s;bool valid=true;
    auto integer=[&](const char* key,int& value){if(d.HasMember(key)){if(d[key].IsInt())value=d[key].GetInt();else valid=false;}};
    auto number=[&](const char* key,auto& value){if(d.HasMember(key)){if(d[key].IsNumber())value=static_cast<std::remove_reference_t<decltype(value)>>(d[key].GetDouble());else valid=false;}};
    auto boolean=[&](const char* key,bool& value){if(d.HasMember(key)){if(d[key].IsBool())value=d[key].GetBool();else valid=false;}};
    if(d.HasMember("devicePath"))valid=Wide(d["devicePath"],s.capture.devicePath)&&valid;
    if(d.HasMember("deviceName"))valid=Wide(d["deviceName"],s.capture.deviceName)&&valid;
    integer("width",s.capture.width);integer("height",s.capture.height);number("fps",s.capture.fps);boolean("useOutlineColors",s.useOutlineColors);
    integer("tolerance",s.tolerance);integer("roiWidth",s.roiWidth);integer("roiHeight",s.roiHeight);number("maxFrameAgeMs",s.maxFrameAgeMs);
    number("associationRadiusPx",s.associationRadiusPx);number("contactRadiusPx",s.contactRadiusPx);integer("touchDelayMs",s.touchDelayMs);
    integer("lostGraceMs",s.lostGraceMs);integer("minComponentPixels",s.minComponentPixels);
    if(d.HasMember("colors")){
        const auto& colors=d["colors"];
        if(!colors.IsArray()||colors.Size()!=3)valid=false;
        else for(rapidjson::SizeType i=0;i<3;++i){
            const auto& c=colors[i];
            if(!c.IsArray()||c.Size()!=3){valid=false;continue;}
            std::uint8_t values[3]{};
            for(rapidjson::SizeType j=0;j<3;++j){if(!c[j].IsUint()||c[j].GetUint()>255)valid=false;else values[j]=static_cast<std::uint8_t>(c[j].GetUint());}
            s.colors[i]={values[0],values[1],values[2]};
        }
    }
    std::wstring detail;
    if(!valid||!ValidateSettings(s,&detail)){failure=detail.empty()?"Invalid capture settings value":Utf8(detail);return false;}
    s.enabled=false;output=std::move(s);return true;
}
bool Load() {
    if(loaded)return error.empty();loaded=true;
    std::ifstream file(Path(),std::ios::binary|std::ios::ate);
    if(!file){SyncOutlinePalette();return true;}
    const auto size=file.tellg();
    if(size<0||size>65536){error="Invalid capture settings file size";malformed=true;return false;}
    file.seekg(0);const std::string data((std::istreambuf_iterator<char>(file)),{});Settings settings;
    if(!Decode(data,settings,error)){malformed=true;return false;}
    std::wstring detail;if(!GetService().Configure(settings,&detail)){error=Utf8(detail);return false;}
    SyncOutlinePalette();return true;
}
bool Save() {
    if(!loaded)Load();
    if(malformed)return false;
    const auto settings=GetService().GetSettings();std::wstring detail;
    if(!ValidateSettings(settings,&detail)){error=Utf8(detail);return false;}
    const auto json=Encode(settings);V2::ConfigStorage::BeginPersistenceOperation();
    const bool ok=V2::ConfigStorage::WriteAtomicBytes(Path(),json.data(),json.size());
    error=ok?"":V2::ConfigStorage::PersistenceError();return ok;
}
}
}
