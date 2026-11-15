#pragma once

namespace V2::Output::CatBoxConfig {

inline constexpr int kMinimumPort = 0;
inline constexpr int kMaximumPort = 65535;

struct Settings {
    char ip[64] = "";
    int commandPort = 0;
    char uuid[32] = "";
    int monitorPort = 0;
};

inline Settings current;

template<class ReadText, class ReadInteger>
void Load(Settings& settings, ReadText readText, ReadInteger readInteger)
{
    readText("catBoxIp", settings.ip);
    readText("catBoxUuid", settings.uuid);
    settings.commandPort = readInteger("catBoxPort", settings.commandPort);
    settings.monitorPort = readInteger("catBoxMonitorPort", settings.monitorPort);
}

template<class WriteText, class WriteInteger>
void Save(const Settings& settings, WriteText writeText, WriteInteger writeInteger)
{
    writeText("catBoxIp", settings.ip);
    writeText("catBoxUuid", settings.uuid);
    writeInteger("catBoxPort", settings.commandPort);
    writeInteger("catBoxMonitorPort", settings.monitorPort);
}

}
