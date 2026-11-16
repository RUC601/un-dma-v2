#include <Windows.h>
#include "Session.hpp"
#include "Protocol.hpp"
#include <setupapi.h>
#include <devguid.h>
#include <algorithm>

namespace V2::Output {
namespace {

std::string FindPort() {
    HDEVINFO devices = SetupDiGetClassDevsA(&GUID_DEVCLASS_PORTS, nullptr, nullptr, DIGCF_PRESENT);
    if (devices == INVALID_HANDLE_VALUE) return {};
    SP_DEVINFO_DATA info{};
    info.cbSize = sizeof(info);
    for (DWORD index = 0; SetupDiEnumDeviceInfo(devices, index, &info); ++index) {
        char name[512];
        DWORD size = 0;
        if (!SetupDiGetDeviceRegistryPropertyA(devices, &info, SPDRP_FRIENDLYNAME,
            nullptr, reinterpret_cast<PBYTE>(name), sizeof(name), &size) || size == 0)
            continue;
        name[(std::min)(size, static_cast<DWORD>(sizeof(name) - 1))] = '\0';
        const std::string description = name;
        const auto port = description.find("COM");
        const auto end = description.find(')', port);
        const bool likelyKmbox = description.find("USB-SERIAL CH340") != std::string::npos ||
            description.find("KMBOX") != std::string::npos ||
            description.find("KMBox") != std::string::npos ||
            description.find("BPRO") != std::string::npos ||
            description.find("B+") != std::string::npos;
        if (port != std::string::npos && end != std::string::npos && likelyKmbox) {
            SetupDiDestroyDeviceInfoList(devices);
            return description.substr(port, end - port);
        }
    }
    SetupDiDestroyDeviceInfoList(devices);
    return {};
}

}

std::string KmBoxSerialSession::ResolvePort(const std::string& requested) const {
    return requested.empty() ? FindPort() : requested;
}

bool KmBoxSerialSession::EncodeMove(int x, int y, int duration, std::string& command) const {
    command = KmBoxSerialProtocol::Move(x, y, duration);
    return true;
}

std::string KmBoxSerialSession::EncodeButton(int button, bool down) const {
    return KmBoxSerialProtocol::Button(button, down);
}

int KmBoxSerialSession::ButtonDomain(std::string_view command) const {
    return KmTextCommands::ButtonDomain(command);
}

bool KmBoxSerialSession::IsButtonRelease(std::string_view command) const {
    return KmTextCommands::IsButtonRelease(command);
}

int KmBoxSerialSession::FlushInterval(OutputCommandType type) const {
    return KmTextCommands::FlushInterval(type);
}

}
