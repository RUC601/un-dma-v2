#pragma once

// Public settings schemas are the configuration integration surface.
// Protocols, handles, worker state and device queues are not included here.
#include "output/devices/kmbox_net/Settings.hpp"
#include "output/devices/catbox/Settings.hpp"
#include "output/settings/SerialSettings.hpp"
#include "output/settings/SelectionSettings.hpp"
#include "output/settings/OutputProfiles.hpp"

namespace V2::Output::DeviceSettings {

inline constexpr KmBoxNetConfig::Settings& Network() noexcept { return KmBoxNetConfig::current; }
inline constexpr CatBoxConfig::Settings& CatBox() noexcept { return CatBoxConfig::current; }
inline constexpr SerialConfig::Settings& Serial() noexcept { return SerialConfig::current; }
inline constexpr SelectionConfig::Settings& Selection() noexcept { return SelectionConfig::current; }
inline constexpr ProfileConfig::Store& Profiles() noexcept { return ProfileConfig::current; }

inline bool IsValidUdpPort(int port) noexcept
{
    return port >= 1 && port <= 65535;
}

inline void ResetConnections() noexcept
{
    Network() = {};
    CatBox() = {};
    Serial() = {};
    Selection() = {};
    Profiles() = {};
}

template<class ReadText, class ReadInteger, class ReadBoolean>
void LoadConnections(ReadText readText, ReadInteger readInteger, ReadBoolean readBoolean)
{
    CatBoxConfig::Load(CatBox(), readText, readInteger);
    SelectionConfig::Load(Selection(), readInteger, readBoolean);
    KmBoxNetConfig::Load(Network(), readText, readInteger, readBoolean);
    SerialConfig::Load(Serial(), readText);
}

template<class WriteText, class WriteInteger, class WriteBoolean>
void SaveConnections(WriteText writeText, WriteInteger writeInteger, WriteBoolean writeBoolean)
{
    CatBoxConfig::Save(CatBox(), writeText, writeInteger);
    SelectionConfig::Save(Selection(), writeInteger, writeBoolean);
    KmBoxNetConfig::Save(Network(), writeText, writeInteger, writeBoolean);
    SerialConfig::Save(Serial(), writeText);
}

}
