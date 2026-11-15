#pragma once
#include "vision/ContourTypes.hpp"
#include <string>

namespace V2::Vision {
// Call from the configuration owner after loading/changing the game palette.
// It changes matcher settings only and never starts capture.
void SyncOutlinePalette();
namespace Preferences {
bool Load();
bool Save();
bool Loaded();
const std::string& Error();
// Pure codec is also used by the isolated Scenario round-trip checks.
std::string Encode(const Settings& settings);
bool Decode(const std::string& json, Settings& settings, std::string& error);
}
}
