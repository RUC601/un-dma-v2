#pragma once

#include "output/DeviceRegistry.hpp"

namespace V2::Output::SelectionConfig {

struct Settings {
    bool enabled = false;
    bool autoDetect = true;
    int legacyDeviceId = static_cast<int>(DeviceId::KmBoxNet);
};

inline Settings current;

template<class ReadInteger, class ReadBoolean>
void Load(Settings& settings, ReadInteger readInteger, ReadBoolean readBoolean)
{
    settings.enabled = readBoolean("kmboxEnabled", settings.enabled);
    settings.autoDetect = readBoolean("outputAutoDetect", settings.autoDetect);
    settings.legacyDeviceId = readInteger("kmboxDeviceType", settings.legacyDeviceId);
}

template<class WriteInteger, class WriteBoolean>
void Save(const Settings& settings, WriteInteger writeInteger, WriteBoolean writeBoolean)
{
    writeBoolean("kmboxEnabled", settings.enabled);
    writeBoolean("outputAutoDetect", settings.autoDetect);
    writeInteger("kmboxDeviceType", settings.legacyDeviceId);
}

}
