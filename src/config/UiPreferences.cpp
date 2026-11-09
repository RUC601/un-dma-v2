#define NOMINMAX
#define _SILENCE_CXX17_ITERATOR_BASE_CLASS_DEPRECATION_WARNING
#include "UiPreferences.hpp"
#include "FileTransaction.hpp"
#include "Utils/Config.hpp"
#ifdef min
#undef min
#endif
#ifdef max
#undef max
#endif
#include <rapidjson/document.h>
#include <rapidjson/writer.h>
#include <rapidjson/stringbuffer.h>
#include <algorithm>
#include <cmath>
#include <fstream>
#include <iterator>

namespace UI::Preferences {
Values values;
namespace {
bool loaded = false;
std::string error;
std::string Path() { return OW::Config::ConfigDirectoryPath() + "/ui-preferences.json"; }
}
bool Loaded() { return loaded; }
const std::string& Error() { return error; }
bool Load(int legacyLanguage) {
    if (loaded) return error.empty();
    loaded = true;
    values.language = std::clamp(legacyLanguage, 0, 1);
    std::ifstream file(Path(), std::ios::binary | std::ios::ate);
    if (!file) return true;
    const auto size = file.tellg();
    if (size < 0 || size > 65536) { error = "Invalid UI preferences file size"; return false; }
    file.seekg(0);
    const std::string data((std::istreambuf_iterator<char>(file)), {});
    rapidjson::Document doc;
    doc.Parse(data.data(), data.size());
    if (doc.HasParseError() || !doc.IsObject()) { error = "Invalid UI preferences JSON"; return false; }
    if(doc.HasMember("version")&&(!doc["version"].IsInt()||doc["version"].GetInt()!=1)){error="Invalid UI preferences version";return false;}
    auto number = [&](const char* name, int& value, int low, int high) {
        if (doc.HasMember(name) && doc[name].IsInt()) value = std::clamp(doc[name].GetInt(), low, high);
    };
    auto boolean = [&](const char* name, bool& value) {
        if (doc.HasMember(name) && doc[name].IsBool()) value = doc[name].GetBool();
    };
    number("language", values.language, 0, 1);
    number("skin", values.skin, 0, 2);
    number("windowWidth", values.windowWidth, 720, 3840);
    number("windowHeight", values.windowHeight, 480, 2160);
    boolean("reducedMotion", values.reducedMotion);
    boolean("loginGlass", values.loginGlass);
    boolean("consoleLayers", values.consoleLayers);
    boolean("sidebarCollapsed", values.sidebarCollapsed);
    boolean("customAccent", values.customAccent);
    if (doc.HasMember("accent") && doc["accent"].IsArray() && doc["accent"].Size() == 4) {
        for (unsigned i = 0; i != 4; ++i)
            if (doc["accent"][i].IsNumber() && std::isfinite(doc["accent"][i].GetFloat()))
                (&values.accent.x)[i] = std::clamp(doc["accent"][i].GetFloat(), 0.f, 1.f);
    }
    return true;
}
bool Save() {
    if (!loaded) return true;
    // Preserve a malformed original for recovery instead of overwriting it with defaults.
    if (!error.empty() && error.rfind("Invalid UI preferences", 0) == 0) return false;
    rapidjson::StringBuffer buffer;
    rapidjson::Writer<rapidjson::StringBuffer> w(buffer);
    w.StartObject();
    w.Key("version"); w.Int(1);
    w.Key("language"); w.Int(std::clamp(values.language, 0, 1));
    w.Key("skin"); w.Int(std::clamp(values.skin, 0, 2));
    w.Key("reducedMotion"); w.Bool(values.reducedMotion);
    w.Key("loginGlass"); w.Bool(values.loginGlass);
    w.Key("consoleLayers"); w.Bool(values.consoleLayers);
    w.Key("sidebarCollapsed"); w.Bool(values.sidebarCollapsed);
    w.Key("windowWidth"); w.Int(values.windowWidth);
    w.Key("windowHeight"); w.Int(values.windowHeight);
    w.Key("customAccent"); w.Bool(values.customAccent);
    w.Key("accent"); w.StartArray();
    for (unsigned i = 0; i != 4; ++i) w.Double((&values.accent.x)[i]);
    w.EndArray(); w.EndObject();
    const bool ok = V2::ConfigStorage::WriteAtomicBytes(Path(), buffer.GetString(), buffer.GetSize());
    error = ok ? "" : V2::ConfigStorage::PersistenceError();
    return ok;
}
}
