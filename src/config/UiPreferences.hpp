#pragma once
#include <string>
#include <imgui.h>

namespace UI::Preferences {
struct Values {
    int language = 0;
    int skin = 0;
    bool reducedMotion = false;
    bool loginGlass = true;
    bool consoleLayers = true;
    bool sidebarCollapsed = false;
    int windowWidth = 1160, windowHeight = 800;
    ImVec4 accent{228.f / 255, 17.f / 255, 67.f / 255, 1};
    bool customAccent = false;
};
extern Values values;
bool Load(int legacyLanguage);
bool Save();
const std::string& Error();
bool Loaded();
}
