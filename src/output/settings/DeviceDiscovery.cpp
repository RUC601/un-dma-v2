#include <WinSock2.h>
#include <WS2tcpip.h>
#include <Windows.h>
#include <setupapi.h>
#include <devguid.h>
#include "output/DeviceDiscovery.hpp"
#include <algorithm>
#include <array>
#include <cctype>
#include <chrono>
#include <cstring>
#include <string_view>
#include <thread>

namespace V2::Output {
namespace {
bool IsMakcu(DeviceId id) {
    return id == DeviceId::Makcu4M || id == DeviceId::Makcu115200;
}

bool SameFamily(DeviceId first, DeviceId second) {
    return first == second || (IsMakcu(first) && IsMakcu(second));
}

std::string Upper(std::string value) {
    for (char& character : value)
        character = static_cast<char>(std::toupper(static_cast<unsigned char>(character)));
    return value;
}

std::string Utf8(std::wstring_view value) {
    if (value.empty()) return {};
    const int length = WideCharToMultiByte(CP_UTF8, 0, value.data(), static_cast<int>(value.size()),
        nullptr, 0, nullptr, nullptr);
    if (length <= 0) return {};
    std::string result(length, '\0');
    WideCharToMultiByte(CP_UTF8, 0, value.data(), static_cast<int>(value.size()),
        result.data(), length, nullptr, nullptr);
    return result;
}

std::string NormalizePort(std::string port) {
    port = Upper(std::move(port));
    if (port.starts_with("\\\\.\\")) port.erase(0, 4);
    if (!port.starts_with("COM") || port.size() < 4 || port.size() > 15) return {};
    if (!std::all_of(port.begin() + 3, port.end(), [](char c) { return c >= '0' && c <= '9'; }))
        return {};
    return port;
}

bool Cancelled(const DeviceDiscoveryRequest& request) {
    return request.cancelled && request.cancelled();
}

DWORD ReplyTimeout(const DeviceDiscoveryRequest& request) {
    return std::clamp<DWORD>(request.replyTimeoutMs, 100, 2000);
}

std::string InstanceId(HDEVINFO devices, SP_DEVINFO_DATA& info) {
    std::array<wchar_t, 1024> value{};
    if (!SetupDiGetDeviceInstanceIdW(devices, &info, value.data(),
            static_cast<DWORD>(value.size()), nullptr)) return {};
    return Utf8(value.data());
}

std::string HardwareIds(HDEVINFO devices, SP_DEVINFO_DATA& info) {
    std::array<wchar_t, 2048> value{};
    DWORD bytes = 0;
    if (!SetupDiGetDeviceRegistryPropertyW(devices, &info, SPDRP_HARDWAREID, nullptr,
            reinterpret_cast<PBYTE>(value.data()), static_cast<DWORD>(sizeof(value)), &bytes)) return {};
    const auto length = (std::min)(static_cast<std::size_t>(bytes / sizeof(wchar_t)), value.size());
    for (std::size_t i = 0; i < length; ++i)
        if (value[i] == L'\0') value[i] = L' ';
    return Upper(Utf8(std::wstring_view(value.data(), length)));
}

std::string PortName(HDEVINFO devices, SP_DEVINFO_DATA& info) {
    HKEY key = SetupDiOpenDevRegKey(devices, &info, DICS_FLAG_GLOBAL, 0, DIREG_DEV, KEY_QUERY_VALUE);
    if (key == INVALID_HANDLE_VALUE) return {};
    std::array<wchar_t, 64> value{};
    DWORD bytes = sizeof(value) - sizeof(wchar_t);
    DWORD type = 0;
    const auto status = RegQueryValueExW(key, L"PortName", nullptr, &type,
        reinterpret_cast<LPBYTE>(value.data()), &bytes);
    RegCloseKey(key);
    return status == ERROR_SUCCESS && type == REG_SZ ? NormalizePort(Utf8(value.data())) : std::string{};
}

bool ValidNetworkEndpoint(const NetworkDiscoveryProfile& profile, sockaddr_in& endpoint,
    std::uint32_t& identifier) {
    endpoint = {};
    endpoint.sin_family = AF_INET;
    endpoint.sin_port = htons(profile.commandPort);
    if (!profile.commandPort || InetPtonA(AF_INET, profile.ip.c_str(), &endpoint.sin_addr) != 1)
        return false;
    const auto address = ntohl(endpoint.sin_addr.s_addr);
    // A saved Ferrum loopback emulator is not a physical KMBox Net candidate.
    if ((address >> 24) == 127 || (address >> 24) == 0 || (address >> 28) >= 14 ||
        address == 0xffffffffu) return false;
    if (profile.mac.size() != 8 || !std::all_of(profile.mac.begin(), profile.mac.end(),
            [](unsigned char c) { return std::isxdigit(c) != 0; })) return false;
    identifier = 0;
    for (unsigned char c : profile.mac) {
        c = static_cast<unsigned char>(std::toupper(c));
        identifier = (identifier << 4) | (c <= '9' ? c - '0' : c - 'A' + 10);
    }
    return true;
}

class SerialProbe {
    HANDLE handle_ = INVALID_HANDLE_VALUE;
public:
    ~SerialProbe() { if (handle_ != INVALID_HANDLE_VALUE) CloseHandle(handle_); }
    bool Open(const std::string& port, DWORD baud, DWORD& error) {
        handle_ = CreateFileA(("\\\\.\\" + port).c_str(), GENERIC_READ | GENERIC_WRITE,
            0, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
        if (handle_ == INVALID_HANDLE_VALUE) { error = GetLastError(); return false; }
        DCB state{};
        state.DCBlength = sizeof(state);
        if (!GetCommState(handle_, &state)) { error = GetLastError(); return false; }
        state.BaudRate = baud;
        state.ByteSize = 8;
        state.Parity = NOPARITY;
        state.StopBits = ONESTOPBIT;
        state.fBinary = TRUE;
        state.fParity = FALSE;
        state.fOutxCtsFlow = state.fOutxDsrFlow = FALSE;
        state.fDsrSensitivity = FALSE;
        state.fOutX = state.fInX = FALSE;
        state.fDtrControl = DTR_CONTROL_DISABLE;
        state.fRtsControl = RTS_CONTROL_DISABLE;
        state.fAbortOnError = FALSE;
        COMMTIMEOUTS timeouts{};
        timeouts.ReadIntervalTimeout = MAXDWORD;
        timeouts.WriteTotalTimeoutConstant = 100;
        if (!SetCommState(handle_, &state) || !SetCommTimeouts(handle_, &timeouts)) {
            error = GetLastError(); return false;
        }
        PurgeComm(handle_, PURGE_RXCLEAR | PURGE_TXCLEAR);
        return true;
    }

    bool Query(std::string_view command, std::string_view expected,
        const DeviceDiscoveryRequest& request) {
        if (Cancelled(request)) return false;
        DWORD written = 0;
        const std::string line = std::string(command) + "\r\n";
        if (!WriteFile(handle_, line.data(), static_cast<DWORD>(line.size()), &written, nullptr) ||
            written != line.size()) return false;
        const auto deadline = GetTickCount64() + ReplyTimeout(request);
        std::string pending;
        while (!Cancelled(request) && GetTickCount64() < deadline) {
            std::array<char, 1024> bytes{};
            DWORD received = 0;
            if (!ReadFile(handle_, bytes.data(), static_cast<DWORD>(bytes.size()), &received, nullptr))
                return false;
            pending.append(bytes.data(), received);
            if (pending.size() > 8192) return false;
            while (true) {
                auto end = pending.find_first_of("\r\n");
                const auto prompt = pending.find(">>>");
                const bool endsWithPrompt = prompt < end;
                if (endsWithPrompt) end = prompt;
                if (end == std::string::npos) break;
                auto result = std::string_view(pending).substr(0, end);
                const auto first = result.find_first_not_of(" \t");
                if (first != std::string_view::npos) {
                    result = result.substr(first, result.find_last_not_of(" \t") - first + 1);
                    if (result == expected) return true;
                }
                pending.erase(0, end + (endsWithPrompt ? 3 : 1));
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(5));
        }
        return false;
    }
};

void ProbeSerial(DeviceDiscoveryCandidate& candidate, const DeviceDiscoveryRequest& request) {
    const bool makcu = IsMakcu(candidate.id);
    const auto& profile = makcu ? request.makcu : request.ferrum;
    std::vector<DWORD> bauds;
    const auto addBaud = [&](DWORD baud) {
        if (baud && std::find(bauds.begin(), bauds.end(), baud) == bauds.end()) bauds.push_back(baud);
    };
    // The product has two MAKCU baud variants. Do not change firmware baud settings.
    if (!makcu || profile.baud == 115200 || profile.baud == 4000000) addBaud(profile.baud);
    addBaud(115200);
    if (makcu) addBaud(4000000);
    for (DWORD baud : bauds) {
        if (Cancelled(request)) break;
        SerialProbe probe;
        DWORD error = ERROR_SUCCESS;
        if (!probe.Open(candidate.port, baud, error)) {
            if (error == ERROR_ACCESS_DENIED || error == ERROR_SHARING_VIOLATION) {
                candidate.state = DeviceDiscoveryState::Busy;
                candidate.reason = "串口正在被其他程序使用";
                return;
            }
            continue;
        }
        if (!probe.Query("km.version()", makcu ? "km.MAKCU" : "kmbox: Ferrum", request)) continue;
        candidate.baud = baud;
        if (makcu) candidate.id = baud == 4000000 ? DeviceId::Makcu4M : DeviceId::Makcu115200;
        if (!makcu && !probe.Query("km.has_mouse()", "1", request)) {
            candidate.state = Cancelled(request) ? DeviceDiscoveryState::Cancelled : DeviceDiscoveryState::Unavailable;
            candidate.reason = Cancelled(request) ? "识别已取消" : "Ferrum 已响应，但未确认已指定鼠标";
            return;
        }
        candidate.state = DeviceDiscoveryState::Ready;
        candidate.reason = makcu ? "已确认 MAKCU 身份" : "已确认 Ferrum 和已指定的鼠标";
        return;
    }
    candidate.state = Cancelled(request) ? DeviceDiscoveryState::Cancelled : DeviceDiscoveryState::Unavailable;
    candidate.reason = Cancelled(request) ? "识别已取消" : "设备未回复对应身份";
}

void ProbeNetwork(DeviceDiscoveryCandidate& candidate, const DeviceDiscoveryRequest& request) {
    sockaddr_in endpoint{};
    std::uint32_t identifier = 0;
    if (!ValidNetworkEndpoint(request.network, endpoint, identifier)) return;
    WSADATA data{};
    if (WSAStartup(MAKEWORD(2, 2), &data) != 0) {
        candidate.state = DeviceDiscoveryState::Unavailable;
        candidate.reason = "无法开始网络识别";
        return;
    }
    const SOCKET socket = ::socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    bool matched = false;
    if (socket != INVALID_SOCKET) {
        // Connection is the only UDP opcode permitted here. It carries no input.
        struct Header { std::uint32_t mac, random, sequence, command; };
        static_assert(sizeof(Header) == 16);
        const Header sent{identifier, GetTickCount() ^ GetCurrentProcessId(), 0, 0xaf3c2828u};
        const int count = sendto(socket, reinterpret_cast<const char*>(&sent), sizeof(sent), 0,
            reinterpret_cast<const sockaddr*>(&endpoint), sizeof(endpoint));
        const auto deadline = GetTickCount64() + ReplyTimeout(request);
        while (count == sizeof(sent) && !Cancelled(request) && GetTickCount64() < deadline) {
            fd_set readable;
            FD_ZERO(&readable);
            FD_SET(socket, &readable);
            timeval timeout{0, 10000};
            const int ready = select(0, &readable, nullptr, nullptr, &timeout);
            if (ready < 0) break;
            if (ready == 0) continue;
            std::array<char, 2048> bytes{};
            sockaddr_in source{};
            int sourceLength = sizeof(source);
            const int received = recvfrom(socket, bytes.data(), static_cast<int>(bytes.size()), 0,
                reinterpret_cast<sockaddr*>(&source), &sourceLength);
            if (received < static_cast<int>(sizeof(Header)) || sourceLength != sizeof(source) ||
                source.sin_family != AF_INET || source.sin_addr.s_addr != endpoint.sin_addr.s_addr ||
                source.sin_port != endpoint.sin_port) continue;
            Header reply{};
            std::memcpy(&reply, bytes.data(), sizeof(reply));
            // The preserved vendor SDK guarantees command/sequence echo, not
            // random/MAC echo. Requiring undocumented echo fields rejects devices.
            if (reply.command == sent.command && reply.sequence == sent.sequence) {
                matched = true; break;
            }
        }
        closesocket(socket);
    }
    WSACleanup();
    candidate.state = matched ? DeviceDiscoveryState::Ready :
        Cancelled(request) ? DeviceDiscoveryState::Cancelled : DeviceDiscoveryState::Unavailable;
    candidate.reason = matched ? "已保存的 KMBox Net 端点握手成功" :
        Cancelled(request) ? "识别已取消" : "已保存的 KMBox Net 端点没有有效回复";
}
}

std::vector<DeviceDiscoveryCandidate> EnumerateDeviceCandidates(const DeviceDiscoveryRequest& request) {
    std::vector<DeviceDiscoveryCandidate> candidates;
    const auto makcuInstance = Upper(request.makcu.instanceId);
    const auto ferrumInstance = Upper(request.ferrum.instanceId);
    HDEVINFO devices = SetupDiGetClassDevsW(&GUID_DEVCLASS_PORTS, nullptr, nullptr, DIGCF_PRESENT);
    if (devices != INVALID_HANDLE_VALUE) {
        SP_DEVINFO_DATA info{};
        info.cbSize = sizeof(info);
        for (DWORD index = 0; !Cancelled(request) && SetupDiEnumDeviceInfo(devices, index, &info); ++index) {
            const auto port = PortName(devices, info);
            const auto instance = InstanceId(devices, info);
            const auto upperInstance = Upper(instance);
            if (port.empty() || instance.empty()) continue;
            const bool savedMakcu = !makcuInstance.empty() && makcuInstance == upperInstance;
            const bool savedFerrum = !ferrumInstance.empty() && ferrumInstance == upperInstance;
            const bool knownFerrum = upperInstance.starts_with("FERRUM\\VIRTUALSERIALPORT\\");
            const bool knownMakcu = upperInstance.starts_with("USB\\") &&
                HardwareIds(devices, info).find("VID_1A86&PID_55D3") != std::string::npos;
            DeviceId id;
            if (knownFerrum) id = DeviceId::Ferrum;
            else if (knownMakcu) id = DeviceId::Makcu115200;
            else if (savedMakcu != savedFerrum) id = savedMakcu ? DeviceId::Makcu115200 : DeviceId::Ferrum;
            else continue;
            const auto& profile = IsMakcu(id) ? request.makcu : request.ferrum;
            candidates.push_back({id, port, instance, profile.baud, DeviceDiscoveryState::Candidate,
                IsMakcu(id) ? savedMakcu : savedFerrum, port == NormalizePort(profile.port), "等待身份确认"});
        }
        SetupDiDestroyDeviceInfoList(devices);
    }
    sockaddr_in endpoint{};
    std::uint32_t identifier = 0;
    if (!Cancelled(request) && ValidNetworkEndpoint(request.network, endpoint, identifier))
        candidates.push_back({DeviceId::KmBoxNet, {}, {}, 0, DeviceDiscoveryState::Candidate,
            true, false, "等待已保存网络端点回复"});
    std::stable_sort(candidates.begin(), candidates.end(), [&](const auto& left, const auto& right) {
        const auto rank = [&](const auto& candidate) {
            return (SameFamily(candidate.id, request.preferredId) ? 4 : 0) +
                (candidate.savedIdentity ? 2 : 0) + (candidate.savedPort ? 1 : 0);
        };
        return rank(left) > rank(right);
    });
    return candidates;
}

std::vector<DeviceDiscoveryCandidate> DiscoverDevices(const DeviceDiscoveryRequest& request) {
    auto candidates = EnumerateDeviceCandidates(request);
    for (auto& candidate : candidates) {
        if (Cancelled(request)) {
            candidate.state = DeviceDiscoveryState::Cancelled;
            candidate.reason = "识别已取消";
        } else if (candidate.id == DeviceId::KmBoxNet) ProbeNetwork(candidate, request);
        else ProbeSerial(candidate, request);
    }
    return candidates;
}

DeviceDiscoverySelection SelectDiscoveredDevice(
    const std::vector<DeviceDiscoveryCandidate>& candidates, DeviceId preferredId) {
    std::vector<std::size_t> ready, preferred, remembered;
    for (std::size_t index = 0; index < candidates.size(); ++index) {
        const auto& candidate = candidates[index];
        if (candidate.state != DeviceDiscoveryState::Ready) continue;
        ready.push_back(index);
        if (SameFamily(candidate.id, preferredId)) {
            preferred.push_back(index);
            if (candidate.savedIdentity) remembered.push_back(index);
        }
    }
    if (remembered.size() == 1) return {remembered.front(), false};
    if (preferred.size() == 1) return {preferred.front(), false};
    if (ready.size() == 1) return {ready.front(), false};
    return {std::nullopt, ready.size() > 1};
}
}
