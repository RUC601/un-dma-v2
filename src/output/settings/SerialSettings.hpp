#pragma once

namespace V2::Output::SerialConfig {

// Compatibility projection for the selected device. OutputProfiles owns each
// family's saved port and identity; the legacy INI key remains readable.
struct Settings {
    char port[16] = "COM3";
    char instanceId[256] = {};
};

inline Settings current;

template<class ReadText>
void Load(Settings& settings, ReadText readText)
{
    readText("kmboxComPort", settings.port);
}

template<class WriteText>
void Save(const Settings& settings, WriteText writeText)
{
    writeText("kmboxComPort", settings.port);
}

}
