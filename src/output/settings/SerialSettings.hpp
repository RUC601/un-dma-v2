#pragma once

namespace V2::Output::SerialConfig {

// The existing INI format has one shared COM selection for every serial device.
struct Settings {
    char port[16] = "COM3";
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
