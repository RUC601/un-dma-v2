#pragma once

namespace V2::Output::DhzBoxConfig {
inline constexpr int kMinimumPort = 0;
inline constexpr int kMaximumPort = 65535;
inline constexpr int kMinimumShift = 0;
inline constexpr int kMaximumShift = 25;

struct Settings {
    char ip[64] = "";
    int commandPort = 8888;
    int randomShift = 0;
    int monitorPort = 0;
};
inline Settings current;

template<class ReadText, class ReadInteger>
void Load(Settings& s, ReadText readText, ReadInteger readInteger) {
    readText("dhzBoxIp", s.ip);
    s.commandPort = readInteger("dhzBoxPort", s.commandPort);
    s.randomShift = readInteger("dhzBoxRandomShift", s.randomShift);
    s.monitorPort = readInteger("dhzBoxMonitorPort", s.monitorPort);
}
template<class WriteText, class WriteInteger>
void Save(const Settings& s, WriteText writeText, WriteInteger writeInteger) {
    writeText("dhzBoxIp", s.ip);
    writeInteger("dhzBoxPort", s.commandPort);
    writeInteger("dhzBoxRandomShift", s.randomShift);
    writeInteger("dhzBoxMonitorPort", s.monitorPort);
}
}
