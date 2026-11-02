#include <Windows.h>
#include <shellapi.h>

#include "Features/UI.hpp"
#include "ui/WorkspaceShell.hpp"
#include "ui/SlotSelector.hpp"
#include "ui/Text.hpp"
#include "ui/Theme.hpp"
#include "ui/EditorSupport.hpp"
using UI::Text::UiText;
using UI::Text::kUiLanguageOptions;
#include "output/DeviceCapabilities.hpp"
#include "game/profiles/ProfileState.hpp"
#include "Features/UiLabels.hpp"
#include "Features/UiValueFormat.hpp"
using namespace UI::ValueFormat;
#include "output/MockBackend.hpp"
#include "output/OutputRuntime.hpp"
#include "Utils/Config.hpp"
#include "Game/HeroSkills.hpp"
#include "Game/Offsets.hpp"
#include "Game/Overwatch.hpp"
#include "Game/Target.hpp"
#include "Game/Structs.hpp"
#include "Game/WeaponSpec.hpp"
#include "Renderer/Overlay.hpp"
#include "Renderer/Renderer.hpp"
#include "Utils/Diagnostics.hpp"
#include "Utils/InputLabels.hpp"
#include "Utils/ProcessConnection.hpp"
#include "resource.h"
#include <imgui.h>
#include <imgui_internal.h>
#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cctype>
#include <cfloat>
#include <cstdint>
#include <cstring>
#include <cstdio>
#include <functional>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

void ApplySelectedTypePreset();
void DrawRuntimeReadinessDetails();

// =====================================================================
// LOCAL UI STATE
// =====================================================================
namespace UI::EditorCommands {





    struct FirePatternRecorderEvent {
        long long timeMs = 0;
        int previousMask = 0;
        int mask = 0;
    };

    constexpr int kFirePatternRecorderMaxMs = 8000;
    std::atomic<bool> g_firePatternRecorderRunning{ false };
    std::jthread g_firePatternRecorderThread;
    std::mutex g_firePatternRecorderMutex;
    std::string g_firePatternRecorderStatus = "Idle";
    bool g_firePatternRecorderInterrupted = false;

    enum class RecorderSampleResult {
        Ready,
        ConnectionChanged,
        SampleUnavailable
    };

    int MouseButtonMask(bool left, bool right)
    {
        return (left ? 0x01 : 0) | (right ? 0x02 : 0);
    }

    RecorderSampleResult ReadRecorderMouseButtons(uint64_t connectionEpoch, int& mask)
    {
        const auto sameConnection = [=] {
            return connectionEpoch != 0 && OW::ProcessConnection::IsConnected() &&
                OW::ProcessConnection::ConnectionEpoch() == connectionEpoch;
        };
        if (!sameConnection())
            return RecorderSampleResult::ConnectionChanged;

        // Both buttons must belong to the same complete, fresh DMA bitmap.
        std::lock_guard<std::mutex> lock(KeyState::keyStateMutex);
        if (!KeyState::HasFreshSample(GetTickCount64()))
            return RecorderSampleResult::SampleUnavailable;
        const auto left = KeyState::SampleVkInBitmap(KeyState::keyStateBitmap, VK_LBUTTON);
        const auto right = KeyState::SampleVkInBitmap(KeyState::keyStateBitmap, VK_RBUTTON);
        if (!left.valid || !left.available || !right.valid || !right.available)
            return RecorderSampleResult::SampleUnavailable;
        if (!sameConnection())
            return RecorderSampleResult::ConnectionChanged;
        mask = MouseButtonMask(left.down, right.down);
        return RecorderSampleResult::Ready;
    }

    const char* RecorderDmaSourceName()
    {
        return "dma_keystate";
    }

    std::string MaskChangeText(int previousMask, int mask)
    {
        const int changed = previousMask ^ mask;
        std::string text;
        auto append = [&](const char* item) {
            if (!text.empty())
                text += "+";
            text += item;
        };

        if (changed & 0x01)
            append((mask & 0x01) ? "L_down" : "L_up");
        if (changed & 0x02)
            append((mask & 0x02) ? "R_down" : "R_up");
        if (text.empty())
            text = "none";
        return text;
    }

    void SetFirePatternRecorderStatus(const std::string& status, bool interrupted = false)
    {
        std::lock_guard<std::mutex> lock(g_firePatternRecorderMutex);
        g_firePatternRecorderStatus = status;
        g_firePatternRecorderInterrupted = interrupted;
    }

std::string GetFirePatternRecorderStatus(bool& interrupted) {
        std::lock_guard<std::mutex> lock(g_firePatternRecorderMutex);
        interrupted = g_firePatternRecorderInterrupted;
        return g_firePatternRecorderStatus;
    }

    void LogFirePatternRecorderSummary(const std::vector<FirePatternRecorderEvent>& events,
                                       long long totalMs,
                                       const char* sourceName)
    {
        Diagnostics::Aim("fire_pattern.record stop source=%s events=%zu totalMs=%lld",
            sourceName,
            events.size(),
            totalMs);

        if (events.empty()) {
            Diagnostics::Aim("fire_pattern.record result=no_mouse_edges");
            SetFirePatternRecorderStatus("No events captured");
            return;
        }

        const long long firstEventMs = events.front().timeMs;
        std::string candidate = "fire_pattern.steps_candidate ";
        for (size_t index = 0; index < events.size(); ++index) {
            const FirePatternRecorderEvent& event = events[index];
            const long long nextMs = index + 1 < events.size()
                ? events[index + 1].timeMs
                : totalMs;
            const long long durationMs = (std::max)(0ll, nextMs - event.timeMs);
            const long long cumulativeMs = (std::max)(0ll, nextMs - firstEventMs);
            const std::string action = MaskChangeText(event.previousMask, event.mask);

            Diagnostics::Aim("fire_pattern.raw event=%zu action=%s mask=0x%02X nextDeltaMs=%lld cumulativeMs=%lld",
                index + 1,
                action.c_str(),
                event.mask,
                durationMs,
                cumulativeMs);

            Diagnostics::Aim("fire_pattern.step step=%zu mask=0x%02X durationMs=%lld boundary=%s",
                index + 1,
                event.mask,
                durationMs,
                index + 1 < events.size() ? "next_event" : "stop");

            char stepText[48] = {};
            std::snprintf(stepText, sizeof(stepText), "{0x%02X,%lld}%s",
                event.mask,
                durationMs,
                index + 1 < events.size() ? "," : "");
            candidate += stepText;
        }

        Diagnostics::Aim("%s", candidate.c_str());

        char status[96] = {};
        std::snprintf(status, sizeof(status), "Captured %zu events", events.size());
        SetFirePatternRecorderStatus(status);
    }

    void FirePatternRecorderLoop(std::stop_token stopToken, uint64_t connectionEpoch)
    {
        timeBeginPeriod(1);

        std::vector<FirePatternRecorderEvent> events;
        events.reserve(64);

        const char* sourceName = RecorderDmaSourceName();
        const auto started = std::chrono::steady_clock::now();
        int previousMask = 0;
        RecorderSampleResult sampleResult = ReadRecorderMouseButtons(connectionEpoch, previousMask);

        if (sampleResult == RecorderSampleResult::Ready) {
            Diagnostics::Aim("fire_pattern.record start source=%s epoch=%llu initialMask=0x%02X maxMs=%d",
                sourceName,
                static_cast<unsigned long long>(connectionEpoch),
                previousMask,
                kFirePatternRecorderMaxMs);
        }

        long long elapsedMs = 0;
        while (sampleResult == RecorderSampleResult::Ready && !stopToken.stop_requested()) {
            const auto now = std::chrono::steady_clock::now();
            elapsedMs = std::chrono::duration_cast<std::chrono::milliseconds>(now - started).count();
            if (elapsedMs >= kFirePatternRecorderMaxMs)
                break;

            int mask = 0;
            sampleResult = ReadRecorderMouseButtons(connectionEpoch, mask);
            if (sampleResult != RecorderSampleResult::Ready)
                break;
            if (mask != previousMask) {
                events.push_back({ elapsedMs, previousMask, mask });
                Diagnostics::Aim("fire_pattern.edge tMs=%lld action=%s previousMask=0x%02X mask=0x%02X",
                    elapsedMs,
                    MaskChangeText(previousMask, mask).c_str(),
                    previousMask,
                    mask);
                previousMask = mask;
            }

            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }

        const auto finished = std::chrono::steady_clock::now();
        elapsedMs = std::chrono::duration_cast<std::chrono::milliseconds>(finished - started).count();
        // A stop request may race device recovery; check the final boundary too.
        if (sampleResult == RecorderSampleResult::Ready) {
            int finalMask = 0;
            sampleResult = ReadRecorderMouseButtons(connectionEpoch, finalMask);
        }
        if (sampleResult == RecorderSampleResult::Ready) {
            LogFirePatternRecorderSummary(events, elapsedMs, sourceName);
        } else {
            const bool connectionChanged = sampleResult == RecorderSampleResult::ConnectionChanged;
            Diagnostics::Aim("fire_pattern.record interrupted source=%s reason=%s epoch=%llu currentEpoch=%llu events=%zu totalMs=%lld steps_candidate_discarded=1",
                sourceName,
                connectionChanged ? "connection_changed" : "dma_sample_unavailable",
                static_cast<unsigned long long>(connectionEpoch),
                static_cast<unsigned long long>(OW::ProcessConnection::ConnectionEpoch()),
                events.size(),
                elapsedMs);
            SetFirePatternRecorderStatus(connectionChanged
                ? "Interrupted: connection changed. Partial recording discarded; reconnect and start again."
                : "Interrupted: DMA key sample unavailable. Partial recording discarded; check DMA and start again.", true);
        }
        timeEndPeriod(1);
        g_firePatternRecorderRunning.store(false, std::memory_order_release);
    }

bool IsFirePatternRecorderRunning() {
        return g_firePatternRecorderRunning.load(std::memory_order_acquire);
    }

void StartFirePatternRecorder() {
        if (IsFirePatternRecorderRunning())
            return;

        if (g_firePatternRecorderThread.joinable())
            g_firePatternRecorderThread.join();

        const uint64_t connectionEpoch = OW::ProcessConnection::ConnectionEpoch();
        SetFirePatternRecorderStatus("Recording...");
        g_firePatternRecorderRunning.store(true, std::memory_order_release);
        g_firePatternRecorderThread = std::jthread([connectionEpoch](std::stop_token stopToken) {
            FirePatternRecorderLoop(stopToken, connectionEpoch);
        });
    }

void StopFirePatternRecorder() {
        if (!g_firePatternRecorderThread.joinable())
            return;

        g_firePatternRecorderThread.request_stop();
        g_firePatternRecorderThread.join();
    }

    std::string CurrentDirectoryPath()
    {
        char buffer[MAX_PATH]{};
        DWORD length = GetCurrentDirectoryA(MAX_PATH, buffer);
        if (length == 0)
            return ".";

        if (length < MAX_PATH)
            return std::string(buffer, length);

        std::vector<char> dynamicBuffer(static_cast<size_t>(length) + 1);
        length = GetCurrentDirectoryA(static_cast<DWORD>(dynamicBuffer.size()), dynamicBuffer.data());
        if (length == 0 || static_cast<size_t>(length) >= dynamicBuffer.size())
            return ".";

        return std::string(dynamicBuffer.data(), length);
    }

    std::string JoinPath(const std::string& directory, const char* child)
    {
        if (directory.empty())
            return child;

        const char tail = directory.back();
        if (tail == '\\' || tail == '/')
            return directory + child;

        return directory + "\\" + child;
    }

    bool RegularFileExists(const std::string& path)
    {
        const DWORD attributes = GetFileAttributesA(path.c_str());
        return attributes != INVALID_FILE_ATTRIBUTES &&
            (attributes & FILE_ATTRIBUTE_DIRECTORY) == 0;
    }

    std::string Win32ErrorMessage(const char* prefix, DWORD error)
    {
        char buffer[96]{};
        std::snprintf(buffer, sizeof(buffer), "%s (error %lu)", prefix, static_cast<unsigned long>(error));
        return buffer;
    }

    std::wstring PowerShellSingleQuoted(const char* text)
    {
        std::wstring quoted = L"'";
        if (text != nullptr) {
            for (const char* current = text; *current != '\0'; ++current) {
                if (*current == '\'')
                    quoted += L"''";
                else
                    quoted += static_cast<unsigned char>(*current);
            }
        }
        quoted += L"'";
        return quoted;
    }

    std::wstring PowerShellSingleQuoted(const std::wstring& text)
    {
        std::wstring quoted = L"'";
        for (const wchar_t ch : text) {
            if (ch == L'\'')
                quoted += L"''";
            else
                quoted += ch;
        }
        quoted += L"'";
        return quoted;
    }

    std::wstring CurrentExecutablePath()
    {
        std::array<wchar_t, MAX_PATH> stackBuffer{};
        DWORD length = GetModuleFileNameW(nullptr, stackBuffer.data(), static_cast<DWORD>(stackBuffer.size()));
        if (length == 0)
            return {};

        if (length < static_cast<DWORD>(stackBuffer.size()))
            return std::wstring(stackBuffer.data(), length);

        std::vector<wchar_t> dynamicBuffer(32768);
        length = GetModuleFileNameW(nullptr, dynamicBuffer.data(), static_cast<DWORD>(dynamicBuffer.size()));
        if (length == 0 || length >= static_cast<DWORD>(dynamicBuffer.size()))
            return {};

        return std::wstring(dynamicBuffer.data(), length);
    }

OutputConnectionTestResult AllowOutputMonitorFirewall() {
        if (OW::Config::outputDeviceType != 0)
            return { false, "Network mode only" };

        const int monitorPort = OW::Config::EffectiveOutputMonitorPort();
        if (monitorPort <= 0 || monitorPort > 65535)
            return { false, "Invalid monitor port" };

        const std::wstring executablePath = CurrentExecutablePath();
        if (executablePath.empty())
            return { false, "EXE path unavailable" };

        const std::wstring exe = PowerShellSingleQuoted(executablePath);
        const std::wstring displayName =
            PowerShellSingleQuoted(L"UNDMA CN KMBox Monitor UDP " + std::to_wstring(monitorPort));
        const std::wstring command =
            L"-NoProfile -ExecutionPolicy Bypass -Command "
            L"\"$exe = " + exe + L"; "
            L"$port = " + std::to_wstring(monitorPort) + L"; "
            L"$portText = [string]$port; "
            L"$name = " + displayName + L"; "
            L"$blocks = Get-NetFirewallRule -Direction Inbound -Action Block -ErrorAction SilentlyContinue | "
            L"Where-Object { "
            L"try { "
            L"$app = Get-NetFirewallApplicationFilter -AssociatedNetFirewallRule $_ -ErrorAction Stop; "
            L"$pf = Get-NetFirewallPortFilter -AssociatedNetFirewallRule $_ -ErrorAction Stop; "
            L"($app.Program -eq $exe) -and "
            L"(($pf.Protocol -eq 'UDP') -or ($pf.Protocol -eq 'Any')) -and "
            L"((@($pf.LocalPort) -contains $portText) -or (@($pf.LocalPort) -contains 'Any')) "
            L"} catch { $false } "
            L"}; "
            L"$blocks | Disable-NetFirewallRule -ErrorAction SilentlyContinue; "
            L"Get-NetFirewallRule -DisplayName $name -ErrorAction SilentlyContinue | "
            L"Remove-NetFirewallRule -ErrorAction SilentlyContinue; "
            L"New-NetFirewallRule -DisplayName $name -Direction Inbound -Action Allow "
            L"-Program $exe -Protocol UDP -LocalPort $port -Profile Any -ErrorAction Stop | Out-Null\"";

        HINSTANCE result = ShellExecuteW(
            nullptr,
            L"runas",
            L"powershell.exe",
            command.c_str(),
            nullptr,
            SW_HIDE);

        const auto resultCode = reinterpret_cast<intptr_t>(result);
        if (resultCode <= 32)
            return { false, Win32ErrorMessage("Firewall launch failed", static_cast<DWORD>(resultCode)) };

        Diagnostics::Info("KMBox monitor firewall allow requested. effectiveMonitorPort=%d commandPort=%d manualOverride=%d",
            monitorPort,
            OW::Config::outputPort,
            OW::Config::outputMonitorPortManualOverride ? 1 : 0);
        Diagnostics::Aim("output.firewall allow_requested effectiveMonitorPort=%d commandPort=%d manualOverride=%d",
            monitorPort,
            OW::Config::outputPort,
            OW::Config::outputMonitorPortManualOverride ? 1 : 0);
        return { true, "Firewall rule requested" };
    }

OutputConnectionTestResult RestartOutputNetworkAdapter() {
        const std::wstring targetIp = PowerShellSingleQuoted(OW::Config::outputIp);
        const std::wstring command =
            L"-NoProfile -ExecutionPolicy Bypass -Command "
            L"\"$target = " + targetIp + L"; "
            L"$adapter = $null; "
            L"try { "
            L"$route = Find-NetRoute -RemoteIPAddress $target -ErrorAction Stop | "
            L"Sort-Object RouteMetric,InterfaceMetric | Select-Object -First 1; "
            L"if ($route) { $adapter = Get-NetAdapter -InterfaceIndex $route.InterfaceIndex -ErrorAction Stop; } "
            L"} catch {} "
            L"if (-not $adapter) { "
            L"$adapter = Get-NetAdapter -InterfaceDescription 'USB 2.0 Ethernet Adapter' -ErrorAction Stop | "
            L"Select-Object -First 1; "
            L"} "
            L"Restart-NetAdapter -Name $adapter.Name -Confirm:$false\"";

        HINSTANCE result = ShellExecuteW(
            nullptr,
            L"runas",
            L"powershell.exe",
            command.c_str(),
            nullptr,
            SW_HIDE);

        const auto resultCode = reinterpret_cast<intptr_t>(result);
        if (resultCode <= 32)
            return { false, Win32ErrorMessage("Restart launch failed", static_cast<DWORD>(resultCode)) };

        return { true, "NIC restart requested" };
    }

OutputConnectionTestResult TestOutputConnection() {
        const V2::Output::OutputRuntimeAppliedState applied =
            V2::Output::RuntimeController().Applied();
        const int lastError = V2::Output::RuntimeController().LastError();

        if (!V2::Output::RuntimeController().IsOutputGateOpen() ||
            applied.descriptor.backend == V2::Output::OutputRuntimeBackend::None) {
            if (lastError != success) {
                char message[96]{};
                std::snprintf(message, sizeof(message), "Inactive: error %d", lastError);
                return { false, message };
            }
            return { false, "Runtime inactive" };
        }

        const char* backend = "unknown";
        switch (applied.descriptor.backend) {
        case V2::Output::OutputRuntimeBackend::Network: backend = "network"; break;
        case V2::Output::OutputRuntimeBackend::Serial:  backend = "serial"; break;
        case V2::Output::OutputRuntimeBackend::CatBox:  backend="catbox";break;
        case V2::Output::OutputRuntimeBackend::Mock:    backend = "mock"; break;
        default: break;
        }

        const OutputConnectionState connectionState =
            V2::Output::ActiveRuntimeConnectionState();
        if (connectionState != OutputConnectionState::Connected) {
            char message[96]{};
            std::snprintf(message, sizeof(message), "Active %s: %s",
                backend,
                ToString(connectionState));
            return { false, message };
        }

        char message[96]{};
        std::snprintf(message, sizeof(message), "Connected: %s (gen %llu)",
            backend,
            static_cast<unsigned long long>(applied.generation));
        return { true, message };
    }

OutputConnectionTestResult ApplyOutputRuntimeFromCurrentConfig(
        std::chrono::milliseconds timeout) {
        const int status = V2::Output::ReconcileRuntimeFromConfig(timeout);
        const V2::Output::OutputRuntimeAppliedState applied =
            V2::Output::RuntimeController().Applied();

        Diagnostics::Info(
            "Output UI runtime reconcile completed. status=%d enabled=%d desiredType=%d activeBackend=%d generation=%llu",
            status,
            OW::Config::outputEnabled ? 1 : 0,
            OW::Config::outputDeviceType,
            static_cast<int>(applied.descriptor.backend),
            static_cast<unsigned long long>(applied.generation));
        Diagnostics::Aim(
            "output.ui_reconcile status=%d enabled=%d desiredType=%d activeBackend=%d generation=%llu",
            status,
            OW::Config::outputEnabled ? 1 : 0,
            OW::Config::outputDeviceType,
            static_cast<int>(applied.descriptor.backend),
            static_cast<unsigned long long>(applied.generation));

        if (status != success) {
            char message[96]{};
            std::snprintf(message, sizeof(message), "Apply failed: %d", status);
            return { false, message };
        }
        if (!OW::Config::outputEnabled)
            return { true, "Disabled" };
        if (!V2::Output::RuntimeController().IsOutputGateOpen() ||
            applied.descriptor.backend == V2::Output::OutputRuntimeBackend::None) {
            return { false, "Runtime inactive" };
        }
        return TestOutputConnection();
    }

void CopyConfigProfileName(char (&destination)[kConfigProfileNameBufferSize], const std::string& name) {
        std::snprintf(destination, kConfigProfileNameBufferSize, "%s", name.c_str());
    }

std::string NormalizeProfileFileName(const char* rawName) {
        std::string name = rawName ? rawName : "";

        const auto isSpace = [](unsigned char ch) { return std::isspace(ch) != 0; };
        name.erase(name.begin(), std::find_if(name.begin(), name.end(),
            [&](unsigned char ch) { return !isSpace(ch); }));
        name.erase(std::find_if(name.rbegin(), name.rend(),
            [&](unsigned char ch) { return !isSpace(ch); }).base(), name.end());

        const size_t slash = name.find_last_of("\\/");
        if (slash != std::string::npos)
            name = name.substr(slash + 1);

        for (char& ch : name) {
            const unsigned char value = static_cast<unsigned char>(ch);
            if (value < 32 || ch == '<' || ch == '>' || ch == ':' || ch == '"' ||
                ch == '/' || ch == '\\' || ch == '|' || ch == '?' || ch == '*') {
                ch = '_';
            }
        }

        if (name.empty())
            name = "config";

        std::string lowered = name;
        std::transform(lowered.begin(), lowered.end(), lowered.begin(),
            [](unsigned char ch) { return static_cast<char>(std::tolower(ch)); });
        if (lowered.size() < 4 || lowered.substr(lowered.size() - 4) != ".ini")
            name += ".ini";

        return name;
    }

    void ReloadConfigProfile()
    {
        const std::string savedName = OW::Config::configFileName;
        const std::string path = OW::Config::ConfigPath();
        OW::Config::LoadConfig(path);
        OW::Config::configFileName = savedName;
        OW::RefreshScreenSizeFromConfig();
        const OutputConnectionTestResult runtimeResult =
            ApplyOutputRuntimeFromCurrentConfig();
        if (!runtimeResult.ok) {
            Diagnostics::Warn(
                "KMBox profile reconcile failed. profile=%s result=%s",
                savedName.c_str(),
                runtimeResult.message.c_str());
        }
    }

bool SelectConfigProfile(const char* name, char (&profileName)[kConfigProfileNameBufferSize],
                             bool saveCurrent) {
        if (saveCurrent && !OW::Config::SaveAllConfig(OW::Config::ConfigPath()))
            return false;
        CopyConfigProfileName(profileName, name);
        OW::Config::configFileName = name ? name : "config.ini";
        ReloadConfigProfile();
        OW::Config::lastConfigProfile = OW::Config::configFileName;
        ApplySelectedTypePreset();
        return true;
    }

std::vector<std::string> EnumerateConfigProfiles() {
        std::vector<std::string> profiles;
        WIN32_FIND_DATAA findData{};
        const std::string searchPath = JoinPath(OW::Config::ConfigDirectoryPath(), "*.ini");
        HANDLE findHandle = FindFirstFileA(searchPath.c_str(), &findData);
        if (findHandle != INVALID_HANDLE_VALUE) {
            do {
                if ((findData.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) == 0 &&
                    _stricmp(findData.cFileName, "app-state.ini") != 0)
                    profiles.emplace_back(findData.cFileName);
            } while (FindNextFileA(findHandle, &findData));
            FindClose(findHandle);
        }

        if (std::find(profiles.begin(), profiles.end(), "config.ini") == profiles.end())
            profiles.emplace_back("config.ini");
        if (!OW::Config::configFileName.empty() &&
            std::find(profiles.begin(), profiles.end(), OW::Config::configFileName) == profiles.end())
            profiles.emplace_back(OW::Config::configFileName);

        std::sort(profiles.begin(), profiles.end());
        profiles.erase(std::unique(profiles.begin(), profiles.end()), profiles.end());
        return profiles;
    }

bool CreateConfigProfileFromCurrent(const std::string& profileName) {
        const std::string targetPath = JoinPath(OW::Config::ConfigDirectoryPath(), profileName.c_str());
        return OW::Config::CreateConfigProfileFromCurrent(targetPath);
    }

int FindConfigProfileIndex(const std::vector<std::string>& profiles, const std::string& name) {
        for (int i = 0; i < static_cast<int>(profiles.size()); ++i) {
            if (profiles[static_cast<size_t>(i)] == name)
                return i;
        }
        return 0;
    }

bool PersistLastConfigProfile() {
        return OW::Config::PersistActiveConfigProfile(OW::Config::configFileName);
    }

} // namespace UI::EditorCommands
using namespace UI::EditorCommands;

// =====================================================================
// SELECT OPTION STRINGS (matching React selectOptions)
// =====================================================================
const char* T(UiText id) { return UI::Text::Get(id,OW::Config::uiLanguage==OW::Config::kUiLanguageChineseSimplified); }
const char* L(const char* value) { return UI::Text::Label(value,OW::Config::uiLanguage==OW::Config::kUiLanguageChineseSimplified); }
static const char* HeroScopeName(const HeroOption& hero) {
    return hero.heroId == 0 ? L("Current settings") : hero.label;
}

static void DrawProbeState(const char* label, bool available, bool down) {
    const ImVec4 color = !available
        ? ImVec4(0.55f, 0.58f, 0.62f, 1.0f)
        : (down ? ImVec4(0.25f, 1.0f, 0.45f, 1.0f) : ImVec4(0.86f, 0.88f, 0.92f, 1.0f));
    ImGui::TextColored(color, "%s: %s", label,
        !available ? T(UiText::StatusNa) : (down ? T(UiText::StatusDown) : T(UiText::StatusUp)));
}

static void DrawKeyStateSample(const char* label, const KeyState::KeyStateVkSample& sample) {
    const bool available = sample.valid && sample.available;
    const ImVec4 color = !available
        ? ImVec4(0.55f, 0.58f, 0.62f, 1.0f)
        : (sample.down ? ImVec4(0.25f, 1.0f, 0.45f, 1.0f) : ImVec4(0.86f, 0.88f, 0.92f, 1.0f));
    ImGui::TextColored(color, "%s: %s  vk=0x%02X byte[%zu]=0x%02X mask=0x%02X",
        label,
        !available ? T(UiText::StatusNa) : (sample.down ? T(UiText::StatusDown) : T(UiText::StatusUp)),
        sample.valid ? sample.vk : 0,
        sample.byteIndex,
        static_cast<unsigned>(sample.rawByte),
        static_cast<unsigned>(sample.downMask));
}

static void DrawDmaMouseButtonSamples() {
    struct MouseSample {
        const char* label;
        int vk;
    };
    constexpr MouseSample kMouseSamples[] = {
        { "Left Mouse", VK_LBUTTON },
        { "Right Mouse", VK_RBUTTON },
        { "Middle Mouse", VK_MBUTTON },
        { "Mouse 4 / X1", VK_XBUTTON1 },
        { "Mouse 5 / X2", VK_XBUTTON2 },
    };

    ImGui::TextUnformatted(L("DMA mouse VK samples"));
    ImGui::Indent();
    for (const MouseSample& entry : kMouseSamples)
        DrawKeyStateSample(entry.label, KeyState::SampleVk(entry.vk));
    ImGui::Unindent();
}

void DrawAimHotkeyProbe() {
    const int keySetting = OW::Config::View_aim_key();
    const int vk = OW::get_bind_id(keySetting);
    const char* keyLabel = OW::Labels::AimActivationKeyName(keySetting);

    ImGui::Text("%s: %s  vk=0x%02X", T(UiText::HotkeyProbe), keyLabel, vk > 0 ? vk : 0);
    if (vk <= 0) {
        DrawProbeState("DMA KeyState", false, false);
        DrawDmaMouseButtonSamples();
        return;
    }

    DrawKeyStateSample("DMA KeyState", KeyState::SampleVk(vk));
    DrawDmaMouseButtonSamples();
}

static int ClampHeroPresetSlotIndex(int slotIndex) {
    return ImClamp(slotIndex, 0, OW::Config::kMaxHeroPresetSlots - 1);
}



static int& ActiveHeroPresetSlotRef(ActionSlotKind kind) {
    return kind == ActionSlotKind::Aim
        ? UI::state.aimHeroSegActive
        : UI::state.triggerHeroSegActive;
}

int ActiveHeroPresetSlotIndex(ActionSlotKind kind) {
    int& activeSlot = ActiveHeroPresetSlotRef(kind);
    activeSlot = ClampHeroPresetSlotIndex(activeSlot);
    return activeSlot;
}

bool IsHeroActionSlotEnabled(ActionSlotKind kind, uint64_t heroId, int slotIndex) {
    return kind == ActionSlotKind::Aim
        ? OW::Config::IsHeroAimSlotEnabled(heroId, slotIndex)
        : OW::Config::IsHeroTriggerSlotEnabled(heroId, slotIndex);
}

static void SetHeroActionSlotEnabled(ActionSlotKind kind, uint64_t heroId, int slotIndex, bool enabled) {
    if (kind == ActionSlotKind::Aim)
        OW::Config::SetHeroAimSlotEnabled(heroId, slotIndex, enabled);
    else
        OW::Config::SetHeroTriggerSlotEnabled(heroId, slotIndex, enabled);
}

static int GetHeroActionSlotCount(ActionSlotKind kind, uint64_t heroId) {
    return kind == ActionSlotKind::Aim
        ? OW::Config::GetHeroAimSlotCount(heroId)
        : OW::Config::GetHeroTriggerSlotCount(heroId);
}

OW::Config::HeroPreset MakeCurrentHeroActionPreset(ActionSlotKind kind) {
    return kind == ActionSlotKind::Aim
        ? OW::Config::MakeHeroAimPresetFromCurrent()
        : OW::Config::MakeHeroTriggerPresetFromCurrent();
}

static bool HasHeroActionPreset(ActionSlotKind kind, uint64_t heroId) {
    return kind == ActionSlotKind::Aim
        ? OW::Config::HasHeroAimPreset(heroId)
        : OW::Config::HasHeroTriggerPreset(heroId);
}

OW::Config::HeroPreset GetHeroActionPresetOrDefault(ActionSlotKind kind,
                                                           uint64_t heroId,
                                                           int slotIndex) {
    return kind == ActionSlotKind::Aim
        ? OW::Config::GetHeroAimPresetOrDefault(heroId, slotIndex)
        : OW::Config::GetHeroTriggerPresetOrDefault(heroId, slotIndex);
}

void SetHeroActionPreset(ActionSlotKind kind,
                                uint64_t heroId,
                                int slotIndex,
                                const OW::Config::HeroPreset& preset) {
    if (kind == ActionSlotKind::Aim)
        OW::Config::SetHeroAimPreset(heroId, slotIndex, preset);
    else
        OW::Config::SetHeroTriggerPreset(heroId, slotIndex, preset);
}

static int AddHeroActionSlot(ActionSlotKind kind,
                             uint64_t heroId,
                             const OW::Config::HeroPreset& seedPreset) {
    return kind == ActionSlotKind::Aim
        ? OW::Config::AddHeroAimSlot(heroId, seedPreset)
        : OW::Config::AddHeroTriggerSlot(heroId, seedPreset);
}

static bool DeleteHeroActionSlot(ActionSlotKind kind, uint64_t heroId, int slotIndex) {
    return kind == ActionSlotKind::Aim
        ? OW::Config::DeleteHeroAimSlot(heroId, slotIndex)
        : OW::Config::DeleteHeroTriggerSlot(heroId, slotIndex);
}

void ApplyHeroActionPresetToGlobals(ActionSlotKind kind, const OW::Config::HeroPreset& preset) {
    if (kind == ActionSlotKind::Aim)
        OW::Config::ApplyHeroAimPresetToGlobals(preset);
    else
        OW::Config::ApplyHeroTriggerPresetToGlobals(preset);
}

void PushHeroActionSlotControlId(ActionSlotKind kind, uint64_t heroId, int slotIndex) {
    char id[64] = {};
    std::snprintf(id, sizeof(id), "%s:%llX:%d",
                  kind == ActionSlotKind::Aim ? "AimSlot" : "TriggerSlot",
                  static_cast<unsigned long long>(heroId),
                  ClampHeroPresetSlotIndex(slotIndex));
    ImGui::PushID(id);
}

static bool IsConcreteHeroSelection(const HeroOption& hero) {
    return hero.heroId != 0;
}

static void ShowAimSlotSummaryTooltip(bool hasSpecificHero) {
    ImGui::SetItemTooltip("%s",
        hasSpecificHero
            ? T(UiText::TooltipHeroSpecific)
            : T(UiText::TooltipHeroFallback));
}

static void ShowSaveConfigTooltip(bool savesSelectedHero) {
    ImGui::SetItemTooltip("%s",
        savesSelectedHero
            ? T(UiText::TooltipSaveHero)
            : T(UiText::TooltipSaveFallback));
}

// =====================================================================
// GROUP BOX STATE (lazy border drawing)
// =====================================================================
static bool     g_gbOpen      = false;
static char     g_gbTitle[64] = "";
static ImVec2   g_gbStartPos(0, 0);
static float    g_gbWidth     = 0.0f;
static float    g_gbMinHeight = 0.0f;
static float    g_gbBottomGap = 7.0f;
static ImDrawListSplitter g_gbDrawSplitter;
static uint64_t s_lastSyncedDetectedHeroId = 0;
static std::string s_configSaveStatus;
static double s_configSaveStatusUntil = 0.0;
static bool s_configSaveFailed = false;

void SetPersistenceStatus(bool saved, const char* successText) {
    s_configSaveFailed = !saved;
    s_configSaveStatus = saved ? std::string(successText) : OW::Config::LastPersistenceError();
    s_configSaveStatusUntil = saved ? ImGui::GetTime() + 4.0 : 0.0;
}

void UI::AutoSave(bool flush) {
    static uint64_t lastPollMs = 0;
    const uint64_t nowMs = GetTickCount64();
    if (!flush && nowMs - lastPollMs < 100) return;
    lastPollMs = nowMs;
    const auto result = OW::Config::PollAutoSave(OW::Config::ConfigPath(), nowMs, flush);
    if (result != OW::Config::AutoSaveResult::Idle && ImGui::GetCurrentContext()) {
        const bool chinese = OW::Config::uiLanguage == OW::Config::kUiLanguageChineseSimplified;
        SetPersistenceStatus(result == OW::Config::AutoSaveResult::Saved,
            chinese ? "已自动保存" : "Changes saved automatically");
    }
}

using namespace UI::Theme;
static ImGuiID s_preNewFrameInitHook = 0;
static ID3D11ShaderResourceView* s_logoTexture = nullptr;
static constexpr int kLogoTextureSize = 32;
static constexpr float kBrandLogoDrawSize = 30.0f;
static constexpr float kBrandTitleGap = 12.0f;
static float s_measuredBodyHeightByPage[kMeasuredPageCount] = {};
static UI::MenuClientSize s_desiredMenuClientSize{ kShellWidth, 680.0f };

// Forward declarations

static void InitStyleBeforeNewFrame(ImGuiContext*, ImGuiContextHook*) {
    if (!UI::state.initialized)
        UI::InitStyle();
}

static void EnsurePreNewFrameInitHook() {
    if (s_preNewFrameInitHook != 0)
        return;

    ImGuiContext* context = ImGui::GetCurrentContext();
    if (!context)
        return;

    ImGuiContextHook hook;
    hook.Type = ImGuiContextHookType_NewFramePre;
    hook.Callback = InitStyleBeforeNewFrame;
    s_preNewFrameInitHook = ImGui::AddContextHook(context, &hook);
}

float MaxFloat(float a, float b) {
    return (a > b) ? a : b;
}

static float MinFloat(float a, float b) {
    return (a < b) ? a : b;
}

static ImU32 MixColor(ImU32 from, ImU32 to, float t) {
    t = ImClamp(t, 0.0f, 1.0f);
    ImVec4 a = ImGui::ColorConvertU32ToFloat4(from);
    ImVec4 b = ImGui::ColorConvertU32ToFloat4(to);
    return ImGui::ColorConvertFloat4ToU32(ImVec4(
        a.x + (b.x - a.x) * t,
        a.y + (b.y - a.y) * t,
        a.z + (b.z - a.z) * t,
        a.w + (b.w - a.w) * t
    ));
}

static float VisualTransition(ImGuiID id, bool enabled, float speed = 16.0f) {
    BOOL animations = TRUE;
    SystemParametersInfoW(SPI_GETCLIENTAREAANIMATION, 0, &animations, 0);
    if (!animations) return enabled ? 1.0f : 0.0f;
    ImGuiStorage* storage = ImGui::GetStateStorage();
    float current = storage->GetFloat(id, enabled ? 1.0f : 0.0f);
    float target = enabled ? 1.0f : 0.0f;
    float step = ImClamp(ImGui::GetIO().DeltaTime * speed, 0.0f, 1.0f);
    current += (target - current) * step;
    storage->SetFloat(id, current);
    return current;
}

int FindMenuToggleKeyIndex(int vk) {
    for (int i = 0; i < IM_ARRAYSIZE(kMenuToggleVk); ++i) {
        if (kMenuToggleVk[i] == vk)
            return i;
    }
    return 0;
}

int ClampHeroSelectionIndex(int index) {
    return ImClamp(index, 0, IM_ARRAYSIZE(kHeroOptions) - 1);
}

const HeroOption& CurrentHeroOption() {
    UI::state.selectedTypeIndex = ClampHeroSelectionIndex(UI::state.selectedTypeIndex);
    return kHeroOptions[UI::state.selectedTypeIndex];
}

static int FindHeroOptionIndexById(uint64_t heroId) {
    if (heroId == 0)
        return 0;

    for (int i = 1; i < IM_ARRAYSIZE(kHeroOptions); ++i) {
        if (kHeroOptions[i].heroId == heroId)
            return i;
    }
    return -1;
}

static const HeroOption* FindHeroOptionById(uint64_t heroId) {
    const int index = FindHeroOptionIndexById(heroId);
    return index >= 0 ? &kHeroOptions[index] : nullptr;
}

static ID3D11ShaderResourceView* HeroAvatarForName(const char* label) {
    if (!label || label[0] == '\0')
        return nullptr;

    IconManager* icons = Render::GetIconManager();
    if (!icons)
        return nullptr;

    const std::string slug = OW::HeroDisplayNameToSlug(label);
    if (slug.empty() || slug == "all")
        return nullptr;

    return icons->LoadHeroAvatar(slug);
}

static ID3D11ShaderResourceView* HeroAvatarForOption(const HeroOption& hero) {
    if (hero.heroId == 0)
        return nullptr;
    return HeroAvatarForName(hero.label);
}

ID3D11ShaderResourceView* HeroSkillIconForDefinition(const OW::HeroSkillDefinition& definition) {
    IconManager* icons = Render::GetIconManager();
    if (!icons || !definition.heroSlug || !definition.iconSlug)
        return nullptr;

    const std::string key = std::string(definition.heroSlug) + "/" + definition.iconSlug;
    if (ID3D11ShaderResourceView* icon = icons->GetIcon(key))
        return icon;

    icons->LoadAbilityIcons(definition.heroSlug, { definition.iconSlug });
    return icons->GetIcon(key);
}

static std::string HeroDisplayNameForId(uint64_t heroId) {
    if (heroId == 0)
        return "Not detected";

    if (const HeroOption* option = FindHeroOptionById(heroId))
        return option->label;

    const OW::c_entity localSnapshot = OW::SnapshotLocalEntity();
    std::string name = OW::GetHeroEngNames(heroId, localSnapshot.LinkBase);
    if (name.empty() || name == "Unknown") {
        char unknown[64] = {};
        std::snprintf(unknown, sizeof(unknown), "Unknown (0x%llX)",
                      static_cast<unsigned long long>(heroId));
        return unknown;
    }
    return name;
}

static std::string SaveSelectedConfig() {
    const HeroOption& selectedHero = CurrentHeroOption();
    const std::string path = OW::Config::ConfigPath();
    const OW::c_entity localSnapshot = OW::SnapshotLocalEntity();
    OW::Config::NormalizeHeroPresets();

    if (IsConcreteHeroSelection(selectedHero)) {
        const std::string heroPath = OW::Config::HeroConfigPath(path);
        if (!OW::Config::SaveConfig(path)) {
            s_configSaveFailed = true;
            return OW::Config::LastPersistenceError();
        }
        if (!OW::Config::SaveConfigForHero(path, selectedHero.heroId, localSnapshot.LinkBase)) {
            s_configSaveFailed = true;
            return std::string(OW::Config::uiLanguage == OW::Config::kUiLanguageChineseSimplified
                ? "系统设置已保存，当前英雄保存失败：" : "System saved; current hero save failed: ") +
                OW::Config::LastPersistenceError();
        }
        s_configSaveFailed = false;
        std::string logStatus = "Saved ";
        logStatus += selectedHero.label;
        logStatus += " config";
        Diagnostics::Info("%s to %s.", logStatus.c_str(), heroPath.c_str());
        std::string status = T(UiText::SavedPrefix);
        status += selectedHero.label;
        status += T(UiText::ConfigSuffix);
        return status;
    }

    if (!OW::Config::SaveAllConfig(path)) {
        s_configSaveFailed = true;
        return OW::Config::LastPersistenceError();
    }
    s_configSaveFailed = false;
    const uint64_t savedHeroId = OW::Config::lastheroid > 0
        ? static_cast<uint64_t>(OW::Config::lastheroid)
        : localSnapshot.HeroID;
    const std::string savedName = savedHeroId != 0
        ? HeroDisplayNameForId(savedHeroId)
        : std::string("global");
    std::string logStatus = "Saved ";
    logStatus += savedName;
    logStatus += " config";
    Diagnostics::Info("%s to %s.", logStatus.c_str(), path.c_str());
    std::string status = T(UiText::SavedPrefix);
    status += savedHeroId != 0 ? savedName : T(UiText::GlobalConfig);
    status += T(UiText::ConfigSuffix);
    return status;
}

static uint64_t DetectedLocalHeroId() {
    const Diagnostics::StatusSnapshot snapshot = Diagnostics::Snapshot();
    return snapshot.localEntity.selectedHeroId;
}

void ApplySelectedTypePreset() {
    const HeroOption& hero = CurrentHeroOption();
    if (hero.heroId == 0)
        return;

    const int aimSlotIndex = ActiveHeroPresetSlotIndex(ActionSlotKind::Aim);
    if (IsHeroActionSlotEnabled(ActionSlotKind::Aim, hero.heroId, aimSlotIndex)) {
        const OW::Config::HeroPreset preset =
            GetHeroActionPresetOrDefault(ActionSlotKind::Aim, hero.heroId, aimSlotIndex);
        ApplyHeroActionPresetToGlobals(ActionSlotKind::Aim, preset);
    }

    const int triggerSlotIndex = ActiveHeroPresetSlotIndex(ActionSlotKind::Trigger);
    if (IsHeroActionSlotEnabled(ActionSlotKind::Trigger, hero.heroId, triggerSlotIndex)) {
        const OW::Config::HeroPreset preset =
            GetHeroActionPresetOrDefault(ActionSlotKind::Trigger, hero.heroId, triggerSlotIndex);
        ApplyHeroActionPresetToGlobals(ActionSlotKind::Trigger, preset);
    }
}

static bool SelectTypeIndex(int index) {
    index = ClampHeroSelectionIndex(index);
    if (UI::state.selectedTypeIndex == index)
        return false;

    UI::state.selectedTypeIndex = index;
    ApplySelectedTypePreset();
    return true;
}

static void SyncSelectedTypeWithDetectedHero() {
    if (!UI::state.followDetectedHero) return;
    const uint64_t detectedHeroId = DetectedLocalHeroId();
    if (detectedHeroId == 0) {
        s_lastSyncedDetectedHeroId = 0;
        return;
    }

    if (detectedHeroId == s_lastSyncedDetectedHeroId)
        return;

    s_lastSyncedDetectedHeroId = detectedHeroId;
    const int detectedIndex = FindHeroOptionIndexById(detectedHeroId);
    if (detectedIndex > 0)
        SelectTypeIndex(detectedIndex);
}

static bool SaveSelectedTypePreset() {
    const HeroOption& hero = CurrentHeroOption();
    if (hero.heroId == 0)
        return false;

    const int slotIndex = ActiveHeroPresetSlotIndex(ActionSlotKind::Aim);
    const OW::Config::HeroPreset preset = MakeCurrentHeroActionPreset(ActionSlotKind::Aim);
    SetHeroActionPreset(ActionSlotKind::Aim, hero.heroId, slotIndex, preset);
    return OW::Config::SaveHeroConfigForHero(OW::Config::ConfigPath(), hero.heroId);
}

static bool LoadSelectedTypePreset() {
    const HeroOption& hero = CurrentHeroOption();
    if (hero.heroId == 0)
        return false;

    ActiveHeroPresetSlotIndex(ActionSlotKind::Aim);
    ActiveHeroPresetSlotIndex(ActionSlotKind::Trigger);
    OW::Config::LoadHeroConfig(OW::Config::ConfigPath());
    ApplySelectedTypePreset();
    return true;
}

static std::string SkeletonBoneMaskPreview(OW::SkeletonBoneMask rawMask);

static const char* PresetAimBehaviorName(int behavior) {
    behavior = ImClamp(behavior, 0, IM_ARRAYSIZE(kAimBehavior) - 1);
    return kAimBehavior[behavior];
}

static void DrawPresetSummary(const HeroOption& hero,
                              const OW::Config::HeroPreset& preset,
                              bool hasStoredPreset,
                              ActionSlotKind kind) {
    char summary[320] = {};
    const char* scope = hero.heroId == 0
        ? "Global defaults"
        : (hasStoredPreset ? "Stored preset" : "Using global defaults");
    if (kind == ActionSlotKind::Aim) {
        const std::string skeleton = SkeletonBoneMaskPreview(preset.aimBoneMask);
        std::snprintf(summary, sizeof(summary),
                      "%s - %s | %s | Speed %.1f%% | FOV %.0f deg | %s | Hitbox %.0f%%",
                      hero.label, scope, PresetAimBehaviorName(preset.aimBehavior),
                      preset.smooth, preset.fov,
                      skeleton.c_str(), preset.hitbox);
    } else {
        std::snprintf(summary, sizeof(summary),
                      "%s - %s | Trigger %s | %s | %s | Hitbox %.0f%% | %s",
                      hero.label, scope,
                      preset.trigger.enabled ? "On" : "Off",
                      OW::AttackActionNameForHero(hero.heroId, preset.trigger.action),
                      OW::Labels::TriggerbotModeName(preset.trigger.mode),
                      preset.hitbox,
                      OW::Labels::AimModeName(preset.aimMode));
    }
    ImGui::TextUnformatted(summary);
}

static bool TypeSelectorButton(const HeroOption& hero, const ImVec2& size) {
    ImGuiContext& g = *ImGui::GetCurrentContext();
    ImGuiWindow* window = ImGui::GetCurrentWindow();
    if (window->SkipItems)
        return false;

    const ImVec2 pos = ImGui::GetCursorScreenPos();
    const ImRect bb(pos, ImVec2(pos.x + size.x, pos.y + size.y));
    const ImGuiID id = window->GetID("##globalTypeSelector");

    ImGui::ItemSize(bb);
    if (!ImGui::ItemAdd(bb, id))
        return false;

    bool hovered = false;
    bool held = false;
    const bool pressed = ImGui::ButtonBehavior(bb, id, &hovered, &held);
    const float hoverT = VisualTransition(id ^ 0x61ad, hovered || held, 18.0f);

    const ImU32 frameCol = MixColor(kColControl, kColControlHover, hoverT);
    window->DrawList->AddRectFilled(bb.Min, bb.Max, frameCol, kControlRounding);
    window->DrawList->AddRect(bb.Min, bb.Max,
                              MixColor(kColStrokeDark, kColStroke, hoverT),
                              kControlRounding, 0, 1.0f);

    const float iconSize = 20.0f;
    const ImVec2 iconMin(bb.Min.x + 5.0f, bb.Min.y + (size.y - iconSize) * 0.5f);
    const ImVec2 iconMax(iconMin.x + iconSize, iconMin.y + iconSize);
    if (ID3D11ShaderResourceView* avatar = HeroAvatarForOption(hero)) {
        window->DrawList->AddImage(reinterpret_cast<ImTextureID>(avatar), iconMin, iconMax);
    } else {
        window->DrawList->AddRectFilled(iconMin, iconMax, kColPanelSoft, 0.0f);
        window->DrawList->AddRect(iconMin, iconMax, kColStroke, 0.0f);
        const char* fallback = hero.heroId == 0 ? "All" : "?";
        const ImVec2 fallbackSize = ImGui::CalcTextSize(fallback);
        window->DrawList->AddText(ImVec2(iconMin.x + (iconSize - fallbackSize.x) * 0.5f,
                                         iconMin.y + (iconSize - fallbackSize.y) * 0.5f),
                                  kColTextMuted, fallback);
    }

    const ImVec2 textPos(iconMax.x + 7.0f,
                         bb.Min.y + (size.y - ImGui::GetTextLineHeight()) * 0.5f);
    window->DrawList->AddText(textPos, kColText, HeroScopeName(hero));

    const ImVec2 caretCenter(bb.Max.x - 11.0f, bb.Min.y + size.y * 0.5f);
    const ImU32 caretCol = MixColor(kColTextDim, kColText, hoverT);
    window->DrawList->AddLine(ImVec2(caretCenter.x - 4.0f, caretCenter.y - 2.0f),
                              ImVec2(caretCenter.x, caretCenter.y + 2.5f),
                              caretCol, 1.35f);
    window->DrawList->AddLine(ImVec2(caretCenter.x, caretCenter.y + 2.5f),
                              ImVec2(caretCenter.x + 4.0f, caretCenter.y - 2.0f),
                              caretCol, 1.35f);

    ImGui::RenderNavCursor(bb, id);
    IMGUI_TEST_ENGINE_ITEM_INFO(id, "##globalTypeSelector", ImGui::GetCurrentContext()->LastItemData.StatusFlags);
    return pressed;
}

static void DrawDetectedTypeReadout() {
    const uint64_t heroId = DetectedLocalHeroId();
    const bool detected = heroId != 0;
    const std::string displayName = HeroDisplayNameForId(heroId);
    const HeroOption* option = detected ? FindHeroOptionById(heroId) : nullptr;
    ID3D11ShaderResourceView* avatar = option ? HeroAvatarForOption(*option) : HeroAvatarForName(displayName.c_str());

    if (avatar) {
        constexpr float kDetectedAvatarSize = 28.0f;
        ImGui::Image(reinterpret_cast<ImTextureID>(avatar),
                     ImVec2(kDetectedAvatarSize, kDetectedAvatarSize));
        ImGui::SameLine(0.0f, ImGui::GetStyle().ItemInnerSpacing.x);
    }

    ImGui::AlignTextToFramePadding();
    ImGui::TextColored(ImGui::ColorConvertU32ToFloat4(detected ? kColText : kColTextDim),
                       "%s", displayName.c_str());
}

static bool TypePickerPanel() {
    const ImVec2 viewport = ImGui::GetMainViewport()->Size;
    ImGui::SetNextWindowSize(ImVec2((std::min)(520.0f, viewport.x - 16.0f),
                                  (std::min)(430.0f, viewport.y - 24.0f)), ImGuiCond_Appearing);
    if (!ImGui::BeginPopup("TypePickerPopup"))
        return false;

    bool changed = false;

    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(8.0f, 8.0f));
    ImGui::PushStyleColor(ImGuiCol_ChildBg, kColPanelSoft);

    const char* categories[] = { "Global", "Tank", "Damage", "Support" };
    const float itemW = (std::max)(76.0f, ImGui::GetFontSize() * 5.0f);
    float nameHeight = ImGui::GetTextLineHeight();
    for(const auto& hero : kHeroOptions)
        nameHeight = (std::max)(nameHeight, ImGui::CalcTextSize(HeroScopeName(hero), nullptr, false, itemW - 8.0f).y);
    const float itemH = 58.0f + nameHeight + 8.0f;
    constexpr float iconSize = 42.0f;

    if (ImGui::BeginChild("##typePickerScroll", ImVec2(0.0f, 0.0f), true)) {
        const float availW = ImGui::GetContentRegionAvail().x;
        const int columns = ImClamp(static_cast<int>(availW / (itemW + 8.0f)), 1, 8);

        for (const char* category : categories) {
            ImGui::TextColored(ImGui::ColorConvertU32ToFloat4(kColTextMuted), "%s", L(category));

            int column = 0;
            for (int i = 0; i < IM_ARRAYSIZE(kHeroOptions); ++i) {
                const HeroOption& hero = kHeroOptions[i];
                if (std::strcmp(hero.category, category) != 0)
                    continue;

                if (column > 0)
                    ImGui::SameLine();

                ImGui::PushID(i);
                const ImVec2 pos = ImGui::GetCursorScreenPos();
                const bool selected = UI::state.selectedTypeIndex == i;
                const bool pressed = ImGui::InvisibleButton(HeroScopeName(hero), ImVec2(itemW, itemH), ImGuiButtonFlags_EnableNav);
                const bool hovered = ImGui::IsItemHovered();
                if (pressed) {
                    UI::state.followDetectedHero = false;
                    changed |= SelectTypeIndex(i);
                    ImGui::CloseCurrentPopup();
                }

                ImDrawList* drawList = ImGui::GetWindowDrawList();
                const ImU32 fill = selected
                    ? IM_COL32(0x25, 0x18, 0x20, 0xFF)
                    : (hovered ? kColControlHover : kColControl);
                const ImU32 border = selected ? kColAccent : (hovered ? kColStroke : kColStrokeDark);
                drawList->AddRectFilled(pos, ImVec2(pos.x + itemW, pos.y + itemH), fill, 0.0f);
                drawList->AddRect(pos, ImVec2(pos.x + itemW, pos.y + itemH), border, 0.0f, 0, selected ? 2.0f : 1.0f);

                const ImVec2 iconMin(pos.x + (itemW - iconSize) * 0.5f, pos.y + 7.0f);
                const ImVec2 iconMax(iconMin.x + iconSize, iconMin.y + iconSize);
                if (ID3D11ShaderResourceView* avatar = HeroAvatarForOption(hero)) {
                    drawList->AddImage(reinterpret_cast<ImTextureID>(avatar), iconMin, iconMax);
                } else {
                    drawList->AddRectFilled(iconMin, iconMax, kColPanelSoft, 0.0f);
                    drawList->AddRect(iconMin, iconMax, kColStroke, 0.0f);
                    const char* fallback = hero.heroId == 0 ? "All" : "?";
                    const ImVec2 fallbackSize = ImGui::CalcTextSize(fallback);
                    drawList->AddText(ImVec2(iconMin.x + (iconSize - fallbackSize.x) * 0.5f,
                                             iconMin.y + (iconSize - fallbackSize.y) * 0.5f),
                                      kColTextMuted, fallback);
                }

                const ImVec2 textSize = ImGui::CalcTextSize(HeroScopeName(hero), nullptr, false, itemW - 8.0f);
                const float textX = pos.x + (itemW - textSize.x) * 0.5f;
                drawList->PushClipRect(ImVec2(pos.x + 4.0f, pos.y + 54.0f),
                                       ImVec2(pos.x + itemW - 4.0f, pos.y + itemH - 4.0f),
                                       true);
                drawList->AddText(ImGui::GetFont(), ImGui::GetFontSize(),
                                  ImVec2(MaxFloat(pos.x + 4.0f, textX), pos.y + 55.0f),
                                  selected ? kColText : kColTextMuted,
                                  HeroScopeName(hero), nullptr, itemW - 8.0f);
                drawList->PopClipRect();
                ImGui::SetItemTooltip("%s", HeroScopeName(hero));
                ImGui::RenderNavCursor(ImRect(pos, ImVec2(pos.x + itemW, pos.y + itemH)), ImGui::GetItemID());
                ImGui::PopID();

                ++column;
                if (column >= columns) {
                    column = 0;
                }
            }

            ImGui::Dummy(ImVec2(0.0f, 4.0f));
        }
    }
    ImGui::EndChild();

    ImGui::PopStyleColor();
    ImGui::PopStyleVar();
    ImGui::EndPopup();
    return changed;
}

static bool CreateTextureFromIconResource(ID3D11Device* device, int resourceId, int size,
                                          ID3D11ShaderResourceView** outTexture) {
    if (!device || !outTexture || size <= 0)
        return false;

    *outTexture = nullptr;

    HICON icon = static_cast<HICON>(
        LoadImageW(GetModuleHandleW(nullptr), MAKEINTRESOURCEW(resourceId),
                   IMAGE_ICON, size, size, LR_DEFAULTCOLOR)
    );
    if (!icon)
        return false;

    BITMAPINFO bitmapInfo = {};
    bitmapInfo.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bitmapInfo.bmiHeader.biWidth = size;
    bitmapInfo.bmiHeader.biHeight = -size;
    bitmapInfo.bmiHeader.biPlanes = 1;
    bitmapInfo.bmiHeader.biBitCount = 32;
    bitmapInfo.bmiHeader.biCompression = BI_RGB;

    HDC screenDc = GetDC(nullptr);
    HDC memoryDc = screenDc ? CreateCompatibleDC(screenDc) : nullptr;
    void* bgraBits = nullptr;
    HBITMAP bitmap = screenDc
        ? CreateDIBSection(screenDc, &bitmapInfo, DIB_RGB_COLORS, &bgraBits, nullptr, 0)
        : nullptr;
    if (screenDc)
        ReleaseDC(nullptr, screenDc);

    if (!memoryDc || !bitmap || !bgraBits) {
        if (bitmap)
            DeleteObject(bitmap);
        if (memoryDc)
            DeleteDC(memoryDc);
        DestroyIcon(icon);
        return false;
    }

    const size_t pixelBytes = static_cast<size_t>(size) * static_cast<size_t>(size) * 4;
    std::memset(bgraBits, 0, pixelBytes);

    HGDIOBJ oldBitmap = SelectObject(memoryDc, bitmap);
    const BOOL drewIcon = DrawIconEx(memoryDc, 0, 0, icon, size, size, 0, nullptr, DI_NORMAL);
    if (oldBitmap)
        SelectObject(memoryDc, oldBitmap);

    if (!drewIcon) {
        DeleteObject(bitmap);
        DeleteDC(memoryDc);
        DestroyIcon(icon);
        return false;
    }

    std::vector<std::uint8_t> rgba(pixelBytes);
    const auto* bgra = static_cast<const std::uint8_t*>(bgraBits);
    bool hasAlpha = false;
    for (int i = 0; i < size * size; ++i) {
        rgba[i * 4 + 0] = bgra[i * 4 + 2];
        rgba[i * 4 + 1] = bgra[i * 4 + 1];
        rgba[i * 4 + 2] = bgra[i * 4 + 0];
        rgba[i * 4 + 3] = bgra[i * 4 + 3];
        hasAlpha = hasAlpha || rgba[i * 4 + 3] != 0;
    }

    if (!hasAlpha) {
        for (int i = 0; i < size * size; ++i) {
            if (rgba[i * 4 + 0] || rgba[i * 4 + 1] || rgba[i * 4 + 2])
                rgba[i * 4 + 3] = 0xFF;
        }
    }

    DeleteObject(bitmap);
    DeleteDC(memoryDc);
    DestroyIcon(icon);

    D3D11_TEXTURE2D_DESC textureDesc = {};
    textureDesc.Width = static_cast<UINT>(size);
    textureDesc.Height = static_cast<UINT>(size);
    textureDesc.MipLevels = 1;
    textureDesc.ArraySize = 1;
    textureDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    textureDesc.SampleDesc.Count = 1;
    textureDesc.SampleDesc.Quality = 0;
    textureDesc.Usage = D3D11_USAGE_DEFAULT;
    textureDesc.BindFlags = D3D11_BIND_SHADER_RESOURCE;

    D3D11_SUBRESOURCE_DATA initData = {};
    initData.pSysMem = rgba.data();
    initData.SysMemPitch = static_cast<UINT>(size * 4);

    ID3D11Texture2D* texture = nullptr;
    HRESULT hr = device->CreateTexture2D(&textureDesc, &initData, &texture);
    if (FAILED(hr) || !texture)
        return false;

    D3D11_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
    srvDesc.Format = textureDesc.Format;
    srvDesc.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
    srvDesc.Texture2D.MostDetailedMip = 0;
    srvDesc.Texture2D.MipLevels = 1;

    ID3D11ShaderResourceView* srv = nullptr;
    hr = device->CreateShaderResourceView(texture, &srvDesc, &srv);
    texture->Release();

    if (FAILED(hr) || !srv)
        return false;

    *outTexture = srv;
    return true;
}

static void DrawText(ImDrawList* drawList, ImFont* font, const ImVec2& pos,
                     ImU32 color, const char* text) {
    if (font)
        ImGui::PushFont(font);
    drawList->AddText(pos, color, text);
    if (font)
        ImGui::PopFont();
}

// =====================================================================
// FORMAT SLIDER VALUE  (mirrors React formatSliderText)
// =====================================================================
static float StepFromDecimalPlaces(int decimals) {
    decimals = ImClamp(decimals, 0, 4);

    float step = 1.0f;
    for (int i = 0; i < decimals; ++i)
        step *= 0.1f;
    return step;
}

static float SliderKeyboardSlowStep(float, float, const Spec& format, bool integralValue) {
    return integralValue ? 1.0f : StepFromDecimalPlaces(format.decimals);
}

static float RoundToSliderStep(float value, float step) {
    if (step <= 0.0f)
        return value;
    return std::round(value / step) * step;
}

// =====================================================================
// UIGroupBox  --  Opens a group box.  Closes any previously open group
//                 box (drawing its border lazily via CloseGroupBox).
// =====================================================================
bool UIGroupBox(const char* title, float minHeightPx, float bottomGapPx) {
    title = L(title);
    // Close previous group box first (draws its border)
    if (g_gbOpen)
        CloseGroupBox();

    g_gbOpen = true;
    g_gbMinHeight = minHeightPx;
    g_gbBottomGap = bottomGapPx;
    std::strncpy(g_gbTitle, title, sizeof(g_gbTitle) - 1);
    g_gbTitle[sizeof(g_gbTitle) - 1] = '\0';

    if (s_boldFont)
        ImGui::PushFont(s_boldFont);
    ImVec2 textSize = ImGui::CalcTextSize(title);
    if (s_boldFont)
        ImGui::PopFont();

    ImVec2 pos = ImGui::GetCursorScreenPos();
    auto* dl = ImGui::GetWindowDrawList();
    g_gbDrawSplitter.Split(dl, 2);
    g_gbDrawSplitter.SetCurrentChannel(dl, 1);

    ImGui::Dummy(ImVec2(0.0f, textSize.y + 10.0f));

    // Save state for lazy border drawing
    g_gbStartPos = ImGui::GetCursorScreenPos();
    g_gbWidth    = ImGui::GetContentRegionAvail().x;

    ImGui::Indent(kGroupContentIndent);
    ImGui::BeginGroup();
    return true;
}

// =====================================================================
// CloseGroupBox  --  Draws the border and legend of the current group
//                    box and restores indentation.
// =====================================================================
void CloseGroupBox() {
    if (!g_gbOpen) return;

    ImVec2 textSize = s_boldFont ? ImGui::CalcTextSize(g_gbTitle) : ImGui::CalcTextSize(g_gbTitle);
    float  titleH   = textSize.y + 10.0f;
    float  borderTopY = g_gbStartPos.y - titleH;

    if (g_gbMinHeight > 0.0f) {
        float desiredContentEndY = borderTopY + g_gbMinHeight - 6.0f;
        float currentY = ImGui::GetCursorScreenPos().y;
        if (currentY < desiredContentEndY)
            ImGui::Dummy(ImVec2(0.0f, desiredContentEndY - currentY));
    }

    ImGui::EndGroup();   // tracks the content bounds
    ImGui::Unindent(kGroupContentIndent);

    ImVec2 contentEnd = ImGui::GetItemRectMax();
    auto*  dl = ImGui::GetWindowDrawList();

    ImVec2 borderMin = g_gbStartPos;
    borderMin.y       = borderTopY;
    ImVec2 borderMax  = ImVec2(
        MaxFloat(borderMin.x + 1.0f, g_gbStartPos.x + g_gbWidth - kGroupBorderClipInset),
        contentEnd.y + 9.0f);
    if (g_gbMinHeight > 0.0f)
        borderMax.y = MaxFloat(borderMax.y, borderMin.y + g_gbMinHeight);

    g_gbDrawSplitter.SetCurrentChannel(dl, 0);
    dl->AddRectFilled(borderMin, borderMax, kColPanelSoft, kGroupRounding);
    dl->AddRect(borderMin, borderMax, kColStroke, kGroupRounding, 0, 1.0f);

    dl->AddRectFilled(
        ImVec2(borderMin.x + 13.0f, borderMin.y - 1.0f),
        ImVec2(borderMin.x + 18.0f + textSize.x + 12.0f, borderMin.y + textSize.y + 4.0f),
        kColPanel
    );

    g_gbDrawSplitter.SetCurrentChannel(dl, 1);
    DrawText(dl, s_boldFont, ImVec2(borderMin.x + 17.0f, borderMin.y + 2.0f),
             kColText, g_gbTitle);

    ImVec2 cursor = ImGui::GetCursorScreenPos();
    if (cursor.y < borderMax.y + g_gbBottomGap)
        ImGui::Dummy(ImVec2(0.0f, borderMax.y + g_gbBottomGap - cursor.y));

    g_gbDrawSplitter.Merge(dl);
    g_gbOpen = false;
    g_gbMinHeight = 0.0f;
}

// =====================================================================
// UICheckbox  --  compact custom checkbox with premium dark-state styling.
// =====================================================================
bool UICheckbox(const char* label, bool* value) {
    ImGuiWindow* window = ImGui::GetCurrentWindow();
    if (window->SkipItems) return false;

    ImGuiContext& g    = *GImGui;
    const ImGuiID id   = window->GetID(label);
    const float sz = (std::max)(16.0f,ImGui::GetFontSize());
    const float height = ControlHeight();
    const float spacing = g.Style.ItemInnerSpacing.x + 2.0f;

    const char* displayLabel = label[0] == '#' ? label : L(label);
    const char* labelEnd = ImGui::FindRenderedTextEnd(displayLabel);
    const bool hasVisibleLabel = labelEnd > displayLabel;
    const ImVec2 labelSize = hasVisibleLabel
        ? ImGui::CalcTextSize(displayLabel, labelEnd, false)
        : ImVec2(0.0f, 0.0f);
    ImVec2 pos = window->DC.CursorPos;

    ImRect bb(pos, ImVec2(pos.x + sz + (hasVisibleLabel ? spacing + labelSize.x : 0.0f), pos.y + height));

    ImGui::ItemSize(bb, g.Style.FramePadding.y);
    if (!ImGui::ItemAdd(bb, id)) return false;

    bool hovered = false, held = false;
    bool pressed = ImGui::ButtonBehavior(bb, id, &hovered, &held);
    if (pressed)
        *value = !*value;
    ImGui::RenderNavCursor(bb, id);
    IMGUI_TEST_ENGINE_ITEM_INFO(id, label, g.LastItemData.StatusFlags);

    float hoverT = VisualTransition(id ^ 0x23a7, hovered || held, 18.0f);
    float checkedT = VisualTransition(id ^ 0x51d3, *value, 18.0f);
    ImVec2 boxMin(bb.Min.x, bb.Min.y + (height - sz) * 0.5f);
    ImVec2 boxMax(boxMin.x + sz, boxMin.y + sz);
    ImU32 baseCol = MixColor(kColControl, kColControlHover, hoverT);
    ImU32 squareCol = MixColor(baseCol, kColAccent, checkedT);
    float rounding = 0.0f;

    window->DrawList->AddRectFilled(boxMin, boxMax, squareCol, rounding);
    window->DrawList->AddRect(boxMin, boxMax,
                              MixColor(kColStroke, IM_COL32(0xf0, 0x34, 0x5d, 0xD0), checkedT),
                              rounding, 0, 1.0f);

    if (checkedT > 0.18f) {
        ImU32 tickCol = MixColor(IM_COL32(0xff, 0xff, 0xff, 0x00),
                                 IM_COL32(0xff, 0xff, 0xff, 0xF4), checkedT);
        window->DrawList->AddLine(ImVec2(boxMin.x + 3.0f, boxMin.y + 6.0f),
                                  ImVec2(boxMin.x + 5.2f, boxMin.y + 8.1f),
                                  tickCol, 1.4f);
        window->DrawList->AddLine(ImVec2(boxMin.x + 5.2f, boxMin.y + 8.1f),
                                  ImVec2(boxMin.x + 9.1f, boxMin.y + 3.8f),
                                  tickCol, 1.4f);
    }

    if (hasVisibleLabel) {
        ImVec2 labelPos(bb.Min.x + sz + spacing, bb.Min.y + (height - labelSize.y) * 0.5f);
        ImU32 labelCol = MixColor(kColTextMuted, kColText, MaxFloat(hoverT, checkedT * 0.5f));
        window->DrawList->AddText(labelPos, labelCol, displayLabel, labelEnd);
    }

    return pressed;
}

// =====================================================================
// UISlider  --  Custom dark track with accent fill and value text.
// =====================================================================
void PushControlWidth(float rightPaddingPx) {
    const float width = MaxFloat(1.0f, ImGui::GetContentRegionAvail().x - rightPaddingPx);
    ImGui::PushItemWidth(width);
}

bool UISlider(const char* label, float* value, float v_min, float v_max,
                     const Spec& format, bool integralValue) {
    ImGuiWindow* window = ImGui::GetCurrentWindow();
    if (window->SkipItems) return false;

    const ImGuiID id   = window->GetID(label);
    const float height = ControlHeight();

    ImVec2 pos  = window->DC.CursorPos;
    const float availableWidth = ImGui::GetContentRegionAvail().x;
    float width = ImGui::CalcItemWidth();
    if (width <= 0.0f || width > availableWidth)
        width = availableWidth;
    width = MaxFloat(1.0f, width);
    ImRect bb(pos, ImVec2(pos.x + width, pos.y + height));

    ImGui::ItemSize(bb);
    if (!ImGui::ItemAdd(bb, id, nullptr, ImGuiItemFlags_Inputable)) return false;

    // Normalise value to [0,1]
    float range  = (v_max - v_min);
    float v_norm = (range > 0.0f) ? (*value - v_min) / range : 0.0f;
    v_norm = ImClamp(v_norm, 0.0f, 1.0f);

    // Interaction
    ImGuiContext& g = *GImGui;
    bool hovered = false, held = false;
    bool pressed = ImGui::ButtonBehavior(bb, id, &hovered, &held,
                                         ImGuiButtonFlags_MouseButtonLeft |
                                         ImGuiButtonFlags_PressedOnClick);
    bool valueChanged = false;

    auto assignValue = [&](float next) {
        next = ImClamp(next, v_min, v_max);
        if (integralValue)
            next = static_cast<float>(ImClamp(static_cast<int>(std::lround(next)),
                                              static_cast<int>(std::lround(v_min)),
                                              static_cast<int>(std::lround(v_max))));
        if (std::fabs(*value - next) <= 0.000001f)
            return;

        *value = next;
        valueChanged = true;
    };

    auto setFromNorm = [&](float normalized) {
        normalized = ImClamp(normalized, 0.0f, 1.0f);
        assignValue(v_min + normalized * range);
    };

    const bool mousePressed = pressed && ImGui::IsMouseClicked(ImGuiMouseButton_Left);
    const bool mouseDragging = held && g.ActiveId == id && ImGui::IsMouseDown(ImGuiMouseButton_Left);

    if (mousePressed) {
        ImGui::SetActiveID(id, window);
        ImGui::SetFocusID(id, window);
        ImGui::FocusWindow(window);
    }

    if (mousePressed || mouseDragging) {
        const ImVec2 mousePos = ImGui::GetMousePos();
        setFromNorm((mousePos.x - bb.Min.x) / (bb.Max.x - bb.Min.x));
    }

    const bool keyboardTarget = (ImGui::IsItemFocused() || hovered || g.ActiveId == id) &&
                                ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows);
    if (keyboardTarget && !ImGui::GetIO().WantTextInput) {
        ImGui::SetKeyOwner(ImGuiKey_LeftArrow, id);
        ImGui::SetKeyOwner(ImGuiKey_RightArrow, id);
        ImGui::SetKeyOwner(ImGuiKey_UpArrow, id);
        ImGui::SetKeyOwner(ImGuiKey_DownArrow, id);
        ImGui::SetKeyOwner(ImGuiKey_Home, id);
        ImGui::SetKeyOwner(ImGuiKey_End, id);

        const float slowStep = SliderKeyboardSlowStep(v_min, v_max, format, integralValue);
        const float fastStep = slowStep * 10.0f;
        auto nudgeValue = [&](float delta) {
            const float next = ImClamp(*value + delta, v_min, v_max);
            assignValue(RoundToSliderStep(next, slowStep));
        };

        const ImGuiInputFlags repeat = ImGuiInputFlags_Repeat;
        if (ImGui::IsKeyPressed(ImGuiKey_RightArrow, repeat, id)) {
            nudgeValue(slowStep);
        } else if (ImGui::IsKeyPressed(ImGuiKey_LeftArrow, repeat, id)) {
            nudgeValue(-slowStep);
        } else if (ImGui::IsKeyPressed(ImGuiKey_UpArrow, repeat, id)) {
            nudgeValue(fastStep);
        } else if (ImGui::IsKeyPressed(ImGuiKey_DownArrow, repeat, id)) {
            nudgeValue(-fastStep);
        } else if (ImGui::IsKeyPressed(ImGuiKey_Home, repeat, id)) {
            assignValue(v_min);
        } else if (ImGui::IsKeyPressed(ImGuiKey_End, repeat, id)) {
            assignValue(v_max);
        }
    }

    v_norm = (range > 0.0f) ? (*value - v_min) / range : 0.0f;
    v_norm = ImClamp(v_norm, 0.0f, 1.0f);

    float hoverT = VisualTransition(id ^ 0x6f47, hovered || held || ImGui::IsItemFocused(), 18.0f);
    ImU32 trackCol = MixColor(kColControl, kColControlHover, hoverT);
    window->DrawList->AddRectFilled(bb.Min, bb.Max, IM_COL32(0x00, 0x00, 0x00, 0x28), kControlRounding);
    window->DrawList->AddRectFilled(ImVec2(bb.Min.x, bb.Min.y + 1.0f),
                                    bb.Max, trackCol, kControlRounding);
    window->DrawList->AddRect(bb.Min, bb.Max,
                              MixColor(kColStrokeDark, kColStroke, hoverT),
                              kControlRounding, 0, 1.0f);
    window->DrawList->AddLine(ImVec2(bb.Min.x + 5.0f, bb.Min.y + 1.0f),
                              ImVec2(bb.Max.x - 5.0f, bb.Min.y + 1.0f),
                              IM_COL32(0xff, 0xff, 0xff, 0x12), 1.0f);

    float fillW = v_norm * (bb.Max.x - bb.Min.x);
    if (fillW > 0.0f) {
        ImVec2 fillMax(bb.Min.x + fillW, bb.Max.y);
        window->DrawList->AddRectFilled(bb.Min, fillMax, kColAccent, kControlRounding);
    }

    // Value text
    char tmp[64];
    const char* displayText = L(Format(tmp, sizeof(tmp), *value, format));
    ImVec2 textSize = ImGui::CalcTextSize(displayText);
    ImVec2 textPos(bb.Max.x - textSize.x - 8.0f, bb.Min.y + (height - textSize.y) * 0.5f);
    window->DrawList->AddText(
        ImVec2(textPos.x + 1.0f, textPos.y + 1.0f),
        IM_COL32(0x00, 0x00, 0x00, 0x80), displayText);
    window->DrawList->AddText(textPos, kColText, displayText);

    if (valueChanged)
        ImGui::MarkItemEdited(id);

    ImGui::RenderNavCursor(bb, id);
    return valueChanged;
}

bool UISlider(const char* label, int* value, float v_min, float v_max,
                     const Spec& format) {
    const int minValue = static_cast<int>(std::lround(v_min));
    const int maxValue = static_cast<int>(std::lround(v_max));
    const int clampedValue = ImClamp(*value, minValue, maxValue);
    float sliderValue = static_cast<float>(clampedValue);

    const bool sliderChanged = UISlider(label, &sliderValue, v_min, v_max, format, true);
    const int roundedValue = ImClamp(static_cast<int>(std::lround(sliderValue)), minValue, maxValue);
    if (roundedValue != *value) {
        *value = roundedValue;
        return true;
    }
    return sliderChanged;
}

// =====================================================================
// UISelect  --  Custom-styled dropdown using ImGui BeginCombo.
//               Dropdown items get a red #e41143 background on hover/active.
// =====================================================================
bool UISelect(const char* label, int* current, const char* const items[], int itemCount, bool translateItems) {
    if (*current < 0 || *current >= itemCount)
        *current = 0;

    const char* preview = translateItems ? L(items[*current]) : items[*current];
    ImGuiWindow* window = ImGui::GetCurrentWindow();
    ImVec2 pos = ImGui::GetCursorScreenPos();
    const float availableWidth = ImGui::GetContentRegionAvail().x;
    float width = ImGui::CalcItemWidth();
    if (width <= 0.0f || width > availableWidth)
        width = availableWidth;
    width = MaxFloat(1.0f, width);
    const float height = ControlHeight();
    ImRect bb(pos, ImVec2(pos.x + width, pos.y + height));

    bool preHovered = ImGui::IsMouseHoveringRect(bb.Min, bb.Max);
    float hoverT = VisualTransition(window->GetID(label) ^ 0x3911, preHovered, 18.0f);
    ImU32 frameCol = MixColor(kColControl, kColControlHover, hoverT);
    window->DrawList->AddRectFilled(bb.Min, bb.Max, IM_COL32(0x00, 0x00, 0x00, 0x2E), kControlRounding);
    window->DrawList->AddRectFilled(ImVec2(bb.Min.x, bb.Min.y + 1.0f), bb.Max,
                                    frameCol, kControlRounding);

    ImGui::PushStyleColor(ImGuiCol_Header,        kColAccentDark);
    ImGui::PushStyleColor(ImGuiCol_HeaderHovered, kColAccent);
    ImGui::PushStyleColor(ImGuiCol_HeaderActive,  kColAccent);
    ImGui::PushStyleColor(ImGuiCol_PopupBg,       kColPanel);
    ImGui::PushStyleColor(ImGuiCol_Border,        kColStroke);
    ImGui::PushStyleColor(ImGuiCol_FrameBg,       frameCol);
    ImGui::PushStyleColor(ImGuiCol_FrameBgHovered, MixColor(kColControlHover, kColControlHot, hoverT));
    ImGui::PushStyleColor(ImGuiCol_FrameBgActive, kColControlHot);
    ImGui::PushStyleVar(ImGuiStyleVar_PopupBorderSize, 1.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding,
                        ImVec2(8.0f, MaxFloat(0.0f, (height - ImGui::GetTextLineHeight()) * 0.5f)));
    ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, kControlRounding);
    ImGui::PushStyleVar(ImGuiStyleVar_PopupRounding, kControlRounding);

    bool changed = false;
    ImGui::SetNextItemWidth(width);
    if (ImGui::BeginCombo(label, preview,
                          ImGuiComboFlags_HeightLargest | ImGuiComboFlags_NoArrowButton)) {
        for (int i = 0; i < itemCount; i++) {
            const bool isSelected = (*current == i);
            std::string optionLabel = translateItems ? L(items[i]) : items[i];
            optionLabel += "###option";
            ImGui::PushID(i);
            if (ImGui::Selectable(optionLabel.c_str(), isSelected)) {
                *current = i;
                changed = true;
            }
            if (isSelected)
                ImGui::SetItemDefaultFocus();
            ImGui::PopID();
        }
        ImGui::EndCombo();
    }

    ImGui::PopStyleVar(4);
    ImGui::PopStyleColor(8);

    window->DrawList->AddRect(bb.Min, bb.Max,
                              MixColor(kColStrokeDark, kColStroke, hoverT),
                              kControlRounding, 0, 1.0f);
    ImVec2 caretCenter(bb.Max.x - 11.0f, bb.Min.y + height * 0.5f);
    ImU32 caretCol = MixColor(kColTextDim, kColText, hoverT);
    window->DrawList->AddLine(ImVec2(caretCenter.x - 4.0f, caretCenter.y - 2.0f),
                              ImVec2(caretCenter.x, caretCenter.y + 2.5f),
                              caretCol, 1.35f);
    window->DrawList->AddLine(ImVec2(caretCenter.x, caretCenter.y + 2.5f),
                              ImVec2(caretCenter.x + 4.0f, caretCenter.y - 2.0f),
                              caretCol, 1.35f);
    return changed;
}

static std::string SkeletonBoneMaskPreview(OW::SkeletonBoneMask rawMask) {
    const OW::SkeletonBoneMask mask = OW::NormalizeSkeletonBoneMask(rawMask);
    if (OW::IsAllSkeletonBonesSelected(mask))
        return L("All");

    std::string preview;
    int selectedCount = 0;
    for (const OW::SkeletonBoneGroupDefinition& group : OW::kSkeletonBoneGroups) {
        if (!OW::SkeletonBoneMaskIncludesGroup(mask, group.mask))
            continue;
        if (!preview.empty())
            preview += " + ";
        preview += L(group.label);
        ++selectedCount;
    }
    if (selectedCount == 1 && OW::Config::uiLanguage != OW::Config::kUiLanguageChineseSimplified)
        preview += " only";
    return preview.empty() ? L("Head only") : preview;
}

bool UISkeletonBoneMultiSelect(const char* label, OW::SkeletonBoneMask* rawMask) {
    if (!rawMask)
        return false;

    *rawMask = OW::NormalizeSkeletonBoneMask(*rawMask);
    const std::string preview = SkeletonBoneMaskPreview(*rawMask);
    ImGuiWindow* window = ImGui::GetCurrentWindow();
    const ImVec2 pos = ImGui::GetCursorScreenPos();
    const float availableWidth = ImGui::GetContentRegionAvail().x;
    float width = ImGui::CalcItemWidth();
    if (width <= 0.0f || width > availableWidth)
        width = availableWidth;
    width = MaxFloat(1.0f, width);
    const float height = ControlHeight();
    const ImRect bb(pos, ImVec2(pos.x + width, pos.y + height));

    const bool preHovered = ImGui::IsMouseHoveringRect(bb.Min, bb.Max);
    const float hoverT = VisualTransition(window->GetID(label) ^ 0x5B71, preHovered, 18.0f);
    const ImU32 frameCol = MixColor(kColControl, kColControlHover, hoverT);
    window->DrawList->AddRectFilled(bb.Min, bb.Max, IM_COL32(0x00, 0x00, 0x00, 0x2E), kControlRounding);
    window->DrawList->AddRectFilled(ImVec2(bb.Min.x, bb.Min.y + 1.0f), bb.Max,
                                    frameCol, kControlRounding);

    ImGui::PushStyleColor(ImGuiCol_Header, kColAccentDark);
    ImGui::PushStyleColor(ImGuiCol_HeaderHovered, kColAccent);
    ImGui::PushStyleColor(ImGuiCol_HeaderActive, kColAccent);
    ImGui::PushStyleColor(ImGuiCol_PopupBg, kColPanel);
    ImGui::PushStyleColor(ImGuiCol_Border, kColStroke);
    ImGui::PushStyleColor(ImGuiCol_FrameBg, frameCol);
    ImGui::PushStyleColor(ImGuiCol_FrameBgHovered, MixColor(kColControlHover, kColControlHot, hoverT));
    ImGui::PushStyleColor(ImGuiCol_FrameBgActive, kColControlHot);
    ImGui::PushStyleVar(ImGuiStyleVar_PopupBorderSize, 1.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding,
                        ImVec2(8.0f, MaxFloat(0.0f, (height - ImGui::GetTextLineHeight()) * 0.5f)));
    ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, kControlRounding);
    ImGui::PushStyleVar(ImGuiStyleVar_PopupRounding, kControlRounding);

    bool changed = false;
    ImGui::SetNextItemWidth(width);
    if (ImGui::BeginCombo(label, L(preview.c_str()),
                          ImGuiComboFlags_HeightLargest | ImGuiComboFlags_NoArrowButton)) {
        const bool allSelected = OW::IsAllSkeletonBonesSelected(*rawMask);
        if (ImGui::Selectable(L("All"), allSelected, ImGuiSelectableFlags_NoAutoClosePopups) &&
            !allSelected) {
            *rawMask = OW::kSkeletonBoneAllMask;
            changed = true;
        }
        if (allSelected)
            ImGui::SetItemDefaultFocus();

        ImGui::Separator();
        for (const OW::SkeletonBoneGroupDefinition& group : OW::kSkeletonBoneGroups) {
            const bool selected = OW::SkeletonBoneMaskIncludesGroup(*rawMask, group.mask);
            if (ImGui::Selectable(L(group.label), selected, ImGuiSelectableFlags_NoAutoClosePopups)) {
                const OW::SkeletonBoneMask next = OW::ToggleSkeletonBoneGroup(*rawMask, group.mask);
                if (next != *rawMask) {
                    *rawMask = next;
                    changed = true;
                }
            }
        }
        ImGui::EndCombo();
    }

    ImGui::PopStyleVar(4);
    ImGui::PopStyleColor(8);

    window->DrawList->AddRect(bb.Min, bb.Max,
                              MixColor(kColStrokeDark, kColStroke, hoverT),
                              kControlRounding, 0, 1.0f);
    const ImVec2 caretCenter(bb.Max.x - 11.0f, bb.Min.y + height * 0.5f);
    const ImU32 caretCol = MixColor(kColTextDim, kColText, hoverT);
    window->DrawList->AddLine(ImVec2(caretCenter.x - 4.0f, caretCenter.y - 2.0f),
                              ImVec2(caretCenter.x, caretCenter.y + 2.5f),
                              caretCol, 1.35f);
    window->DrawList->AddLine(ImVec2(caretCenter.x, caretCenter.y + 2.5f),
                              ImVec2(caretCenter.x + 4.0f, caretCenter.y - 2.0f),
                              caretCol, 1.35f);
    return changed;
}

int NextAimMethodPresetId() {
    int nextId = 1;
    for (const OW::Config::AimMethodPreset& preset : OW::Config::View_aimMethodPresets())
        nextId = (std::max)(nextId, preset.id + 1);
    return nextId;
}

static int NextAimBehaviorPresetId() {
    int nextId = 1;
    for (const OW::Config::AimBehaviorPreset& preset : OW::Config::View_aimBehaviorPresets())
        nextId = (std::max)(nextId, preset.id + 1);
    return nextId;
}

int NextDynamicFovPresetId() {
    int nextId = 1;
    for (const OW::Config::DynamicFovPreset& preset : OW::Config::View_dynamicFovPresets())
        nextId = (std::max)(nextId, preset.id + 1);
    return nextId;
}

std::string NormalizeUiPresetName(const char* text, const char* fallback, int index) {
    std::string value = text ? text : "";
    while (!value.empty() && std::isspace(static_cast<unsigned char>(value.front())))
        value.erase(value.begin());
    while (!value.empty() && std::isspace(static_cast<unsigned char>(value.back())))
        value.pop_back();
    if (value.empty()) {
        value = fallback ? fallback : "Preset";
        if (index >= 0) {
            value += " ";
            value += std::to_string(index + 1);
        }
    }
    if (value.size() > 63)
        value.resize(63);
    return value;
}

void CopyPresetNameToBuffer(const std::string& name, char (&buffer)[64]) {
    std::snprintf(buffer, sizeof(buffer), "%s", name.c_str());
}

OW::Config::AimMethodPreset CaptureMethodPresetFromCurrent(int method, const char* name) {
    const int normalizedMethod = OW::Config::ClampAimMethodIndex(method);
    OW::Config::AimMethodPreset preset{};
    preset.id = NextAimMethodPresetId();
    preset.name = NormalizeUiPresetName(name, "Method Preset", static_cast<int>(OW::Config::View_aimMethodPresets().size()));
    preset.method = normalizedMethod;
    preset.angularSpeedScale = OW::Config::ClampAimMethodAngularSpeedScalePercent(
        OW::Config::aimMethodAngularSpeedScale[static_cast<size_t>(normalizedMethod)]);
    preset.pidP = std::clamp(OW::Config::View_aimPidP(), 0.0f, 2.0f);
    preset.pidI = std::clamp(OW::Config::View_aimPidI(), 0.0f, 0.5f);
    preset.pidD = std::clamp(OW::Config::View_aimPidD(), 0.0f, 1.0f);
    preset.pidMaxIntegral = std::clamp(OW::Config::View_aimPidMaxIntegral(), 1.0f, 50.0f);
    preset.pidDeadzone = std::clamp(OW::Config::View_aimPidDeadzone(), 0.0f, 10.0f);
    preset.bezierControlPoints = std::clamp(OW::Config::View_aimBezierControlPoints(), 2, 6);
    preset.bezierCurvature = std::clamp(OW::Config::View_aimBezierCurvature(), 0.0f, 1.0f);
    preset.bezierSpeed = std::clamp(OW::Config::View_aimBezierSpeed(), 1.0f, 200.0f);
    preset.piecewiseNearDegrees = OW::Config::AimPiecewiseNearDegrees();
    preset.piecewiseMidDegrees = OW::Config::AimPiecewiseMidDegrees();
    preset.piecewiseFarDegrees = OW::Config::AimPiecewiseFarDegrees();
    preset.piecewiseNearScale = OW::Config::AimPiecewiseNearScale();
    preset.piecewiseMidScale = OW::Config::AimPiecewiseMidScale();
    preset.piecewiseFarScale = OW::Config::AimPiecewiseFarScale();
    preset.accelLimitedAcceleration = OW::Config::AimMethodAcceleration(4);
    preset.constantAngularSpeedDeg = OW::Config::AimConstantAngularSpeedDeg();
    preset.hybridConstantSpeedDeg = OW::Config::aimHybridConstantSpeedDeg;
    preset.hybridMaxSpeedDeg = OW::Config::aimHybridMaxSpeedDeg;
    preset.hybridAccelerationDeg = OW::Config::aimHybridAccelerationDeg;
    preset.hybridDecelerationDeg = OW::Config::aimHybridDecelerationDeg;
    preset.hybridNearRadiusDeg = OW::Config::aimHybridNearRadiusDeg;
    preset.hybridTargetMotionGain = OW::Config::aimHybridTargetMotionGain;
    preset.hybridSuddenMotionBoost = OW::Config::aimHybridSuddenMotionBoost;
    preset.hybridDeadzoneDeg = OW::Config::aimHybridDeadzoneDeg;
    return preset;
}

OW::Config::AimBehaviorPreset CaptureBehaviorPresetFromCurrent(int behavior,
                                                                      int method,
                                                                      int methodPresetId,
                                                                      float baseSpeed,
                                                                      const OW::AimStartLimiterProfile& startLimiter,
                                                                      bool moveSplitEnabled,
                                                                      int moveSplitMaxPixels,
                                                                      int moveSplitDelayUs,
                                                                      const char* name) {
    OW::Config::AimBehaviorPreset preset{};
    preset.id = NextAimBehaviorPresetId();
    preset.name = NormalizeUiPresetName(name, "Behavior Preset", static_cast<int>(OW::Config::View_aimBehaviorPresets().size()));
    preset.behavior = OW::Config::ClampAimBehaviorIndex(behavior);
    preset.method = OW::Config::ClampAimMethodIndex(method);
    preset.methodPresetId = OW::Config::ClampAimMethodPresetId(methodPresetId);
    if (const OW::Config::AimMethodPreset* methodPreset = OW::Config::FindAimMethodPreset(preset.methodPresetId))
        preset.method = OW::Config::ClampAimMethodIndex(methodPreset->method);
    preset.baseSpeed = std::isfinite(baseSpeed) ? std::clamp(baseSpeed, 0.0f, 100.0f) : 100.0f;
    preset.startLimiter = OW::ValidateAimStartLimiterProfile(startLimiter);
    preset.moveSplitEnabled = moveSplitEnabled;
    preset.moveSplitMaxPixels = OW::Config::ClampMoveSplitMaxPixels(moveSplitMaxPixels);
    preset.moveSplitDelayUs = OW::Config::ClampMoveSplitDelayUs(moveSplitDelayUs);
    return preset;
}

static const char* AimBehaviorPresetPreview(int presetId, const char* noneText) {
    const OW::Config::AimBehaviorPreset* preset = OW::Config::FindAimBehaviorPreset(presetId);
    return preset ? preset->name.c_str() : noneText;
}

static const char* AimMethodPresetPreview(int presetId, const char* noneText) {
    const OW::Config::AimMethodPreset* preset = OW::Config::FindAimMethodPreset(presetId);
    return preset ? preset->name.c_str() : noneText;
}

static const char* DynamicFovPresetPreview(int presetId, const char* noneText) {
    const OW::Config::DynamicFovPreset* preset = OW::Config::FindDynamicFovPreset(presetId);
    return preset ? preset->name.c_str() : noneText;
}

bool UISelectAimMethodPreset(const char* label, int* presetId, int filterMethod) {
    if (!OW::Config::FindAimMethodPreset(*presetId))
        *presetId = -1;

    bool changed = false;
    const char* preview = AimMethodPresetPreview(*presetId, L("Current Method Config"));
    if (ImGui::BeginCombo(label, preview, ImGuiComboFlags_HeightLargest)) {
        const bool noneSelected = *presetId < 0;
        if (ImGui::Selectable(L("Current Method Config"), noneSelected)) {
            *presetId = -1;
            changed = true;
        }
        if (noneSelected)
            ImGui::SetItemDefaultFocus();

        const int normalizedFilter = filterMethod >= 0 ? OW::Config::ClampAimMethodIndex(filterMethod) : -1;
        for (const OW::Config::AimMethodPreset& preset : OW::Config::View_aimMethodPresets()) {
            const int method = OW::Config::ClampAimMethodIndex(preset.method);
            if (normalizedFilter >= 0 && method != normalizedFilter)
                continue;

            std::string labelText = preset.name;
            labelText += " / ";
            labelText += L(kAimMethod[method]);
            const bool selected = *presetId == preset.id;
            if (ImGui::Selectable(labelText.c_str(), selected)) {
                *presetId = preset.id;
                changed = true;
            }
            if (selected)
                ImGui::SetItemDefaultFocus();
        }
        ImGui::EndCombo();
    }
    return changed;
}

bool UISelectAimBehaviorPreset(const char* label, int* presetId, int filterBehavior) {
    if (!OW::Config::FindAimBehaviorPreset(*presetId))
        *presetId = -1;

    bool changed = false;
    const char* preview = AimBehaviorPresetPreview(*presetId, L("Class Default"));
    if (ImGui::BeginCombo(label, preview, ImGuiComboFlags_HeightLargest)) {
        const bool noneSelected = *presetId < 0;
        if (ImGui::Selectable(L("Class Default"), noneSelected)) {
            *presetId = -1;
            changed = true;
        }
        if (noneSelected)
            ImGui::SetItemDefaultFocus();

        const int normalizedFilter = filterBehavior >= 0 ? OW::Config::ClampAimBehaviorIndex(filterBehavior) : -1;
        for (const OW::Config::AimBehaviorPreset& preset : OW::Config::View_aimBehaviorPresets()) {
            const int behavior = OW::Config::ClampAimBehaviorIndex(preset.behavior);
            if (normalizedFilter >= 0 && behavior != normalizedFilter)
                continue;

            std::string labelText = preset.name;
            labelText += " / ";
            labelText += L(kAimBehavior[behavior]);
            if (const OW::Config::AimMethodPreset* methodPreset = OW::Config::FindAimMethodPreset(preset.methodPresetId)) {
                labelText += " / ";
                labelText += methodPreset->name;
            }
            const bool selected = *presetId == preset.id;
            if (ImGui::Selectable(labelText.c_str(), selected)) {
                *presetId = preset.id;
                changed = true;
            }
            if (selected)
                ImGui::SetItemDefaultFocus();
        }
        ImGui::EndCombo();
    }
    return changed;
}

bool UISelectDynamicFovPreset(const char* label, int* presetId, bool allowNone) {
    if (!OW::Config::FindDynamicFovPreset(*presetId))
        *presetId = -1;

    bool changed = false;
    const char* preview = DynamicFovPresetPreview(*presetId, allowNone ? L("Fixed FOV") : L("Select Preset"));
    if (ImGui::BeginCombo(label, preview, ImGuiComboFlags_HeightLargest)) {
        if (allowNone) {
            const bool noneSelected = *presetId < 0;
            if (ImGui::Selectable(L("Fixed FOV"), noneSelected)) {
                *presetId = -1;
                changed = true;
            }
            if (noneSelected)
                ImGui::SetItemDefaultFocus();
        }

        for (const OW::Config::DynamicFovPreset& preset : OW::Config::View_dynamicFovPresets()) {
            const OW::Config::DynamicFovPreset normalized =
                OW::Config::ValidateDynamicFovPresetValue(preset);
            char labelText[160] = {};
            const float nearFov = OW::Config::EvaluateDynamicFovPreset(normalized, 0.0f, OW::Config::kDefaultFovDeg);
            const float farFov = OW::Config::EvaluateDynamicFovPreset(
                normalized,
                normalized.points[static_cast<size_t>(normalized.pointCount - 1)].distanceM,
                OW::Config::kDefaultFovDeg);
            std::snprintf(labelText,
                          sizeof(labelText),
                          "%s / %.0f->%.0f deg",
                          normalized.name.c_str(),
                          nearFov,
                          farFov);
            const bool selected = *presetId == preset.id;
            if (ImGui::Selectable(labelText, selected)) {
                *presetId = preset.id;
                changed = true;
            }
            if (selected)
                ImGui::SetItemDefaultFocus();
        }
        ImGui::EndCombo();
    }
    return changed;
}

void ApplyAimBehaviorToHeroPreset(OW::Config::HeroPreset& preset, int behavior) {
    preset.aimBehavior = OW::Config::ClampAimBehaviorIndex(behavior);
    preset.aimMode = OW::Config::IsTrackingBehavior(preset.aimBehavior) ? 0 : 1;
    if (preset.firePolicy == 0 || preset.firePolicy == 1 || preset.firePolicy == 2) {
        preset.firePolicy = static_cast<int>(OW::DefaultFirePolicyForBehavior(
            OW::ClampAimBehavior(preset.aimBehavior)));
        preset.keepFiring = preset.firePolicy == 1;
        preset.autoshot = preset.firePolicy != 0 && preset.firePolicy != 1;
    }
}

void UIDisabledSelect(const char* label, const char* previewText) {
    ImGuiWindow* window = ImGui::GetCurrentWindow();
    if (window->SkipItems) return;
    if (!previewText)
        previewText = T(UiText::NotOpened);

    const ImGuiID id = window->GetID(label);
    ImVec2 pos = ImGui::GetCursorScreenPos();
    const float availableWidth = ImGui::GetContentRegionAvail().x;
    float width = ImGui::CalcItemWidth();
    if (width <= 0.0f || width > availableWidth)
        width = availableWidth;
    width = MaxFloat(1.0f, width);
    const float height = ControlHeight();
    ImRect bb(pos, ImVec2(pos.x + width, pos.y + height));

    ImGui::ItemSize(bb);
    if (!ImGui::ItemAdd(bb, id))
        return;

    const ImU32 frameCol = MixColor(kColPanelSoft, kColControl, 0.45f);
    window->DrawList->AddRectFilled(bb.Min, bb.Max, IM_COL32(0x00, 0x00, 0x00, 0x2E), kControlRounding);
    window->DrawList->AddRectFilled(ImVec2(bb.Min.x, bb.Min.y + 1.0f), bb.Max,
                                    frameCol, kControlRounding);
    window->DrawList->AddRect(bb.Min, bb.Max,
                              MixColor(kColStrokeDark, kColStroke, 0.25f),
                              kControlRounding, 0, 1.0f);

    const ImVec2 textSize = ImGui::CalcTextSize(previewText);
    const ImVec2 textPos(bb.Min.x + 8.0f, bb.Min.y + (height - textSize.y) * 0.5f);
    ImGui::PushClipRect(bb.Min, bb.Max, true);
    window->DrawList->AddText(textPos, kColTextDim, previewText);
    ImGui::PopClipRect();

    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("%s", previewText);
}

static std::vector<int> AttackActionsForHero(uint64_t heroId) {
    std::vector<int> actions;

    auto addAction = [&](int action) {
        if (action < 0 || action >= OW::Labels::AttackActionCount())
            return;
        if (std::find(actions.begin(), actions.end(), action) == actions.end())
            actions.push_back(action);
    };

    if (heroId != 0) {
        for (const OW::WeaponSpec* spec = OW::WeaponSpecsBegin(); spec != OW::WeaponSpecsEnd(); ++spec) {
            if (spec->heroId == heroId)
                addAction(spec->action);
        }
    }

    if (actions.empty()) {
        const int actionCount = OW::Labels::AttackActionCount();
        for (int action = 0; action < actionCount; ++action)
            actions.push_back(action);
    }

    return actions;
}

bool UIAttackActionSelect(const char* label, int* action, uint64_t heroId) {
    std::vector<int> actionValues = AttackActionsForHero(heroId);
    if (actionValues.empty())
        return false;

    bool changed = false;
    int currentIndex = 0;
    const auto current = std::find(actionValues.begin(), actionValues.end(), *action);
    if (current != actionValues.end()) {
        currentIndex = static_cast<int>(current - actionValues.begin());
    } else {
        *action = actionValues.front();
        changed = true;
    }

    std::vector<const char*> actionLabels;
    actionLabels.reserve(actionValues.size());
    for (int value : actionValues)
        actionLabels.push_back(OW::AttackActionNameForHero(heroId, value));

    if (UISelect(label, &currentIndex, actionLabels.data(), static_cast<int>(actionLabels.size()))) {
        *action = actionValues[static_cast<size_t>(currentIndex)];
        changed = true;
    }

    return changed;
}

// =====================================================================
// UISegmented  --  Row of buttons, active one gets accent background.
//                  Returns the new active index (or old if unchanged).
// =====================================================================
static int UISegmented(const char* items[], int itemCount, int active) {
    ImGuiWindow* window = ImGui::GetCurrentWindow();

    float width  = ImGui::GetContentRegionAvail().x;
    float height = (itemCount <= 5) ? 21.0f : 17.0f;

    ImVec2 pos = window->DC.CursorPos;

    window->DrawList->AddRectFilled(pos, ImVec2(pos.x + width, pos.y + height),
                                    kColControl, kControlRounding);
    window->DrawList->AddRect(pos, ImVec2(pos.x + width, pos.y + height),
                              kColStrokeDark, kControlRounding, 0, 1.0f);

    float segW = width / itemCount;
    int   result = active;

    for (int i = 0; i < itemCount; i++) {
        ImVec2 segMin(pos.x + i * segW, pos.y);
        ImVec2 segMax(pos.x + (i + 1) * segW, pos.y + height);

        ImGui::SetCursorScreenPos(segMin);
        ImGui::PushID(i);
        ImGui::InvisibleButton("##seg", ImVec2(segW, height));
        bool hovered = ImGui::IsItemHovered();
        if (ImGui::IsItemClicked())
            result = i;
        ImGui::PopID();

        float hoverT = VisualTransition(window->GetID(items[i]) ^ 0x7791, hovered, 16.0f);
        if (hovered && i != active) {
            window->DrawList->AddRectFilled(ImVec2(segMin.x + 1.0f, segMin.y + 1.0f),
                                            ImVec2(segMax.x - 1.0f, segMax.y - 1.0f),
                                            MixColor(IM_COL32(0x00, 0x00, 0x00, 0x00),
                                                     kColControlHover, hoverT),
                                            kControlRounding);
        }
        if (i == active) {
            window->DrawList->AddRectFilled(ImVec2(segMin.x + 1.0f, segMin.y + 1.0f),
                                            ImVec2(segMax.x - 1.0f, segMax.y - 1.0f),
                                            kColAccent, kControlRounding);
            window->DrawList->AddLine(ImVec2(segMin.x + 5.0f, segMin.y + 1.0f),
                                      ImVec2(segMax.x - 5.0f, segMin.y + 1.0f),
                                      IM_COL32(0xff, 0xff, 0xff, 0x28), 1.0f);
        }

        const char* txt = items[i];
        ImVec2 tsz = ImGui::CalcTextSize(txt);
        ImVec2 txtPos(segMin.x + (segW - tsz.x) * 0.5f,
                      segMin.y + (height - tsz.y) * 0.5f);
        ImU32 txtCol = (i == active)
            ? IM_COL32(0xff, 0xff, 0xff, 0xFF)
            : MixColor(kColTextMuted, kColText, hoverT);
        window->DrawList->AddText(txtPos, txtCol, txt);
    }

    ImGui::Dummy(ImVec2(0.0f, height + 8.0f));

    return result;
}

static std::string ActionSlotTabLabel(int slotIndex) {
    return std::to_string(ClampHeroPresetSlotIndex(slotIndex) + 1);
}

static std::string ActionSlotReadoutLabel(int slotIndex) {
    return std::string("Slot ") + ActionSlotTabLabel(slotIndex);
}

bool UIActionSlotSelector(ActionSlotKind kind, uint64_t heroId) {
    if (!heroId) return false;
    int& active=ActiveHeroPresetSlotRef(kind);
    const int count=ImClamp(GetHeroActionSlotCount(kind,heroId),1,OW::Config::kMaxHeroPresetSlots);
    active=ImClamp(active,0,count-1);
    std::vector<UI::Slots::Item> items;
    items.reserve(count);
    for(int i=0;i<count;++i) {
        OW::Config::HeroSlotPreset slot;
        if(kind==ActionSlotKind::Aim)OW::Config::TryGetHeroAimSlot(heroId,i,slot);
        else OW::Config::TryGetHeroTriggerSlot(heroId,i,slot);
        items.push_back({slot.stableId,slot.name,slot.enabled});
    }
    const auto command=UI::Slots::Render(items,active,OW::Config::kMaxHeroPresetSlots,
        OW::Config::uiLanguage==OW::Config::kUiLanguageChineseSimplified);
    switch(command.action) {
    case UI::Slots::Action::Select:active=command.index;return true;
    case UI::Slots::Action::Add: {
        const int added=AddHeroActionSlot(kind,heroId,GetHeroActionPresetOrDefault(kind,heroId,active));
        if(added<0)return false;
        active=added;return true;
    }
    case UI::Slots::Action::Remove:
        if(!DeleteHeroActionSlot(kind,heroId,active))return false;
        active=ImClamp(active,0,GetHeroActionSlotCount(kind,heroId)-1);return true;
    default:return false;
    }
}

// =====================================================================
// UITwoColumns  --  Two equal-width columns.
// =====================================================================
void UITwoColumns(std::function<void()> left, std::function<void()> right) {
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(10.0f, 6.0f));
    if(ImGui::GetContentRegionAvail().x < 620.0f * (ImGui::GetFontSize()/15.0f)) {
        left(); CloseGroupBox(); ImGui::Spacing(); right();
    } else {
        ImGui::Columns(2, nullptr, false);
        left(); ImGui::NextColumn(); right(); ImGui::Columns(1);
    }
    ImGui::PopStyleVar();
}

// =====================================================================
// SettingRow  --  Renders a fixed-width label column + the next widget.
// =====================================================================
void SettingRow(const char* label, float labelWidthPx) {
    label = L(label);
    const float available = ImGui::GetContentRegionAvail().x;
    labelWidthPx *= (std::max)(1.0f, ImGui::GetFontSize()/15.0f);
    if(available < labelWidthPx + ImGui::GetFontSize()*7.0f || ImGui::CalcTextSize(label).x > labelWidthPx - 8.0f) {
        ImGui::TextWrapped("%s",label);
        return;
    }
    const ImVec2 cursor = ImGui::GetCursorPos();
    const ImVec2 pos = ImGui::GetCursorScreenPos();
    const ImVec2 size = ImGui::CalcTextSize(label);
    const ImVec2 labelMin(pos.x, pos.y + (ControlHeight() - size.y) * 0.5f);
    const ImVec2 labelMax(pos.x + labelWidthPx - 8.0f, labelMin.y + size.y);
    ImGui::RenderTextEllipsis(ImGui::GetWindowDrawList(), labelMin, labelMax, labelMax.x, label, nullptr, &size);
    if (size.x > labelMax.x - labelMin.x && ImGui::IsMouseHoveringRect(labelMin, labelMax))
        ImGui::SetTooltip("%s", label);
    ImGui::SetCursorPos(ImVec2(cursor.x + labelWidthPx, cursor.y));
}

bool UIActionSlotEnabledCheckbox(ActionSlotKind kind,
                                        const HeroOption& hero,
                                        const char* checkboxId,
                                        float labelWidthPx) {
    SettingRow("Slot Enabled", labelWidthPx);
    if (hero.heroId == 0) {
        ImGui::AlignTextToFramePadding();
        ImGui::TextUnformatted(L("Always"));
        return false;
    }

    const int slotIndex = ActiveHeroPresetSlotIndex(kind);
    bool slotEnabled = IsHeroActionSlotEnabled(kind, hero.heroId, slotIndex);
    if (!UICheckbox(checkboxId, &slotEnabled))
        return false;

    SetHeroActionSlotEnabled(kind, hero.heroId, slotIndex, slotEnabled);
    return true;
}

bool UIColorEdit(const char* label, ImVec4* value,
                        float labelWidthPx) {
    ImGui::PushID(label);
    SettingRow(label, labelWidthPx);
    PushControlWidth();
    ImGui::PushStyleColor(ImGuiCol_FrameBg, kColControl);
    ImGui::PushStyleColor(ImGuiCol_FrameBgHovered, kColControlHover);
    ImGui::PushStyleColor(ImGuiCol_FrameBgActive, kColControlHot);
    ImGui::PushStyleColor(ImGuiCol_Border, kColStroke);
    ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, kControlRounding);

    const bool changed = ImGui::ColorEdit4(
        "##color",
        &value->x,
        ImGuiColorEditFlags_NoLabel |
        ImGuiColorEditFlags_AlphaBar |
        ImGuiColorEditFlags_DisplayRGB |
        ImGuiColorEditFlags_InputRGB
    );

    ImGui::PopStyleVar();
    ImGui::PopStyleColor(4);
    ImGui::PopItemWidth();
    ImGui::PopID();
    return changed;
}

// =====================================================================
// DrawDivider  --  Thin separator line matching React .divider
// =====================================================================
static void DrawDivider() {
    ImVec2 pos = ImGui::GetCursorScreenPos();
    float width = ImGui::GetContentRegionAvail().x;
    ImGui::GetWindowDrawList()->AddLine(
        pos, ImVec2(pos.x + width, pos.y), IM_COL32(0x08, 0x09, 0x0b, 0xB8));
    ImGui::Dummy(ImVec2(0.0f, 4.0f));
}

void DrawCheckboxGrid3(const char* labels[], bool* values[], int rowCount,
                              float gapX, const float ratios[3]) {
    float avail = ImGui::GetContentRegionAvail().x;
    float ratioTotal = ratios[0] + ratios[1] + ratios[2];
    float usable = MaxFloat(0.0f, avail - gapX * 2.0f);
    float colW[3] = {
        usable * ratios[0] / ratioTotal,
        usable * ratios[1] / ratioTotal,
        usable * ratios[2] / ratioTotal
    };

    ImVec2 start = ImGui::GetCursorScreenPos();
    float colX[3] = {
        start.x,
        start.x + colW[0] + gapX,
        start.x + colW[0] + gapX + colW[1] + gapX
    };
    const float rowH = ControlHeight();
    const float rowGap = 10.0f;

    for (int row = 0; row < rowCount; ++row) {
        for (int col = 0; col < 3; ++col) {
            int idx = row * 3 + col;
            if (!labels[idx] || !values[idx])
                continue;

            ImGui::SetCursorScreenPos(ImVec2(colX[col], start.y + row * (rowH + rowGap)));
            UICheckbox(labels[idx], values[idx]);
        }
    }

    float gridH = (rowCount > 0) ? (rowCount * rowH + (rowCount - 1) * rowGap) : 0.0f;
    ImGui::SetCursorScreenPos(ImVec2(start.x, start.y + gridH));
    ImGui::Dummy(ImVec2(0.0f, 0.0f));
}

static void DrawTopTabIcon(ImDrawList* drawList, int tabIndex, const ImVec2& min, ImU32 color) {
    ImVec2 c(min.x + 9.0f, min.y + 9.0f);

    if (tabIndex == 0) {
        drawList->AddCircle(c, 6.0f, color, 24, 1.5f);
        drawList->AddCircle(c, 2.0f, color, 16, 1.5f);
        drawList->AddLine(ImVec2(c.x - 8.0f, c.y), ImVec2(c.x - 4.5f, c.y), color, 1.5f);
        drawList->AddLine(ImVec2(c.x + 4.5f, c.y), ImVec2(c.x + 8.0f, c.y), color, 1.5f);
        drawList->AddLine(ImVec2(c.x, c.y - 8.0f), ImVec2(c.x, c.y - 4.5f), color, 1.5f);
        drawList->AddLine(ImVec2(c.x, c.y + 4.5f), ImVec2(c.x, c.y + 8.0f), color, 1.5f);
    } else if (tabIndex == 1) {
        drawList->AddEllipse(c, ImVec2(8.0f, 4.8f), color, 0.0f, 24, 1.5f);
        drawList->AddCircle(c, 2.4f, color, 16, 1.5f);
    } else if (tabIndex == 2) {
        drawList->AddCircle(c, 7.0f, color, 24, 1.5f);
        drawList->AddCircleFilled(ImVec2(c.x - 2.5f, c.y - 2.2f), 1.1f, color, 8);
        drawList->AddCircleFilled(ImVec2(c.x + 2.0f, c.y - 2.5f), 1.1f, color, 8);
        drawList->AddCircleFilled(ImVec2(c.x - 0.5f, c.y + 2.4f), 1.1f, color, 8);
        drawList->AddCircle(ImVec2(c.x + 4.2f, c.y + 3.4f), 2.1f, color, 10, 1.5f);
    } else {
        drawList->AddCircle(c, 3.3f, color, 16, 1.5f);
        for (int i = 0; i < 8; ++i) {
            float a = (3.14159265f * 2.0f * i) / 8.0f;
            ImVec2 inner(c.x + std::cos(a) * 5.1f, c.y + std::sin(a) * 5.1f);
            ImVec2 outer(c.x + std::cos(a) * 8.0f, c.y + std::sin(a) * 8.0f);
            drawList->AddLine(inner, outer, color, 1.5f);
        }
    }
}

static int CurrentPageKey() {
    switch (UI::state.activeTab) {
        case UI::TAB_AIMING:
            return ImClamp(UI::state.aimingSubTab, 0, kAimingSubTabCount - 1);
        case UI::TAB_VISUALS:
            return kVisualsPageKey;
        case UI::TAB_THEME:
            return kThemePageKeyBase + ImClamp(UI::state.themeSubTab, 0, kThemeSubTabCount - 1);
        case UI::TAB_MISC:
            return kMiscPageKeyBase + ImClamp(UI::state.miscSubTab, 0, kMiscSubTabCount - 1);
        default:
            return 0;
    }
}

static float CurrentSubBarHeight() {
    if (UI::state.activeTab == UI::TAB_AIMING)
        return 32.0f;
    if (UI::state.activeTab == UI::TAB_VISUALS)
        return 42.0f;
    if (UI::state.activeTab == UI::TAB_THEME)
        return 32.0f;
    if (UI::state.activeTab == UI::TAB_MISC)
        return 32.0f;
    return 0.0f;
}

static float CurrentBodyTopPad() {
    if (UI::state.activeTab == UI::TAB_AIMING)
        return 8.0f;
    if (UI::state.activeTab == UI::TAB_VISUALS)
        return 10.0f;
    if (UI::state.activeTab == UI::TAB_MISC)
        return 8.0f;
    return 10.0f;
}

static float CurrentShellFrameHeight() {
    return kShellBorder * 2.0f + kHeaderHeight + CurrentSubBarHeight() + CurrentBodyTopPad();
}

static float FallbackShellHeight() {
    if (UI::state.activeTab == UI::TAB_AIMING)
        return 548.0f;
    if (UI::state.activeTab == UI::TAB_VISUALS)
        return 476.0f;
    if (UI::state.activeTab == UI::TAB_THEME)
        return 548.0f;
    if (UI::state.activeTab == UI::TAB_MISC)
        return 548.0f;
    return 140.0f;
}

static float CurrentShellHeight() {
    return 680.0f;
}

static void UpdateDesiredMenuClientSize(float measuredBodyHeight) {
    s_measuredBodyHeightByPage[CurrentPageKey()] = MaxFloat(0.0f, measuredBodyHeight);
    s_desiredMenuClientSize.width = kShellWidth;
    s_desiredMenuClientSize.height = CurrentShellHeight();
}

void UI::InitializeResources(ID3D11Device* device) {
    if (s_logoTexture || !device)
        return;

    CreateTextureFromIconResource(device, IDI_UNLEASHED, kLogoTextureSize, &s_logoTexture);
}

void UI::ShutdownResources() {
    StopFirePatternRecorder();
    AutoSave(true);
    if (s_logoTexture) {
        s_logoTexture->Release();
        s_logoTexture = nullptr;
    }
}

UI::MenuClientSize UI::DesiredMenuClientSize() {
    return s_desiredMenuClientSize;
}

// =====================================================================
// UI::InitStyle  --  Refined dark overlay styling.
// =====================================================================
void UI::InitStyle() { state.initialized = UI::Theme::Initialize(); }

// =====================================================================
// UI::AimbotPage
// =====================================================================
void DrawRuntimeReadinessDetails() {
    UIGroupBox("Connection status");
        const Diagnostics::StatusSnapshot runtimeStatus = Diagnostics::Snapshot();
        const bool connected = OW::ProcessConnection::IsConnected();
        const KeyState::KeyStateVkSample keySample = KeyState::SampleVk(VK_LBUTTON);
        const bool keysReady = keySample.valid && keySample.available;
        const bool cameraFresh = OW::HasFreshViewMatrices();
        const V2::Output::OutputRuntimeAppliedState device = V2::Output::RuntimeController().Applied();
        const bool deviceReady = OW::Config::outputEnabled &&
            V2::Output::RuntimeController().IsOutputGateOpen() &&
            device.descriptor.backend != V2::Output::OutputRuntimeBackend::None;
        const bool mockDevice = deviceReady &&
            device.descriptor.backend == V2::Output::OutputRuntimeBackend::Mock;
        const bool chinese = OW::Config::uiLanguage == OW::Config::kUiLanguageChineseSimplified;
        char readiness[512]{};
        std::snprintf(readiness, sizeof(readiness), chinese
            ? "DMA %s   游戏 %s   按键 %s   相机 %s   外设 %s"
            : "DMA %s   Game %s   Keys %s   Camera %s   Device %s",
            runtimeStatus.dmaReady ? (chinese ? "就绪" : "ready") : (chinese ? "未就绪" : "unavailable"),
            connected ? (chinese ? "已连接" : "connected") : (chinese ? "未连接" : "disconnected"),
            keysReady ? (chinese ? "可用" : "ready") : (chinese ? "不可用" : "unavailable"),
            cameraFresh ? (chinese ? "新鲜" : "fresh") : (chinese ? "过期" : "stale"),
            mockDevice ? (chinese ? "模拟" : "mock") : deviceReady ? (chinese ? "就绪" : "ready") :
                OW::Config::outputEnabled ? (chinese ? "未就绪" : "unavailable") : (chinese ? "未启用" : "disabled"));
        const char* nextStep = nullptr;
        if (!runtimeStatus.dmaReady)
            nextStep = chinese ? "下一步：检查 DMA 连接及设备设置，稍候自动重试或在设置→通用点击重建设备连接。" : "Next: check DMA and device settings, wait for automatic retry or use Settings > General > Rebuild DMA connection.";
        else if (!connected)
            nextStep = chinese ? "下一步：打开游戏，或在设置→通用点击重连。" : "Next: start the game, or use Settings > General > Reconnect.";
        else if (!keysReady)
            nextStep = chinese ? "下一步：在设置→诊断查看主机按键采样错误。" : "Next: check host key sampling under Settings > Diagnostics.";
        else if (!cameraFresh)
            nextStep = chinese ? "下一步：进入游戏场景；若仍未更新，在设置→通用重连。" : "Next: enter a game scene; reconnect if the camera remains stale.";
        else if (!deviceReady)
            nextStep = chinese ? "下一步：在设置→KMBox 设置外设并检查连接。" : "Next: configure and check the device under Settings > KMBox.";
        else if (mockDevice)
            nextStep = chinese ? "当前外设为模拟设备，未使用真实外设。" : "The active device is a mock; no physical device is in use.";
        else if (OW::Config::OutputSuppressedByMenu())
            nextStep = chinese ? "外设就绪；菜单打开时已暂停输出。" : "Device ready; output is paused while this menu is open.";
        else
            nextStep = chinese ? "采样及外设就绪；完整状态可在设置→诊断查看。" : "Samples and device ready. See Settings > Diagnostics for details.";
    ImGui::TextWrapped("%s", readiness);
    ImGui::TextWrapped("%s", nextStep);
    CloseGroupBox();
}

static void DrawRuntimeStatusStrip(ImDrawList* dl, const ImVec2& pos, float width) {
        const Diagnostics::StatusSnapshot runtimeStatus = Diagnostics::Snapshot();
        const bool connected = OW::ProcessConnection::IsConnected();
        const KeyState::KeyStateVkSample keySample = KeyState::SampleVk(VK_LBUTTON);
        const bool keysReady = keySample.valid && keySample.available;
        const bool cameraFresh = OW::HasFreshViewMatrices();
        const V2::Output::OutputRuntimeAppliedState device = V2::Output::RuntimeController().Applied();
        const bool deviceReady = OW::Config::outputEnabled &&
            V2::Output::RuntimeController().IsOutputGateOpen() &&
            device.descriptor.backend != V2::Output::OutputRuntimeBackend::None;
        const bool mockDevice = deviceReady &&
            device.descriptor.backend == V2::Output::OutputRuntimeBackend::Mock;
        const bool chinese = OW::Config::uiLanguage == OW::Config::kUiLanguageChineseSimplified;
        char readiness[512]{};
        std::snprintf(readiness, sizeof(readiness), chinese
            ? "DMA %s   游戏 %s   按键 %s   相机 %s   外设 %s"
            : "DMA %s   Game %s   Keys %s   Camera %s   Device %s",
            runtimeStatus.dmaReady ? (chinese ? "就绪" : "ready") : (chinese ? "未就绪" : "unavailable"),
            connected ? (chinese ? "已连接" : "connected") : (chinese ? "未连接" : "disconnected"),
            keysReady ? (chinese ? "可用" : "ready") : (chinese ? "不可用" : "unavailable"),
            cameraFresh ? (chinese ? "新鲜" : "fresh") : (chinese ? "过期" : "stale"),
            mockDevice ? (chinese ? "模拟" : "mock") : deviceReady ? (chinese ? "就绪" : "ready") :
                OW::Config::outputEnabled ? (chinese ? "未就绪" : "unavailable") : (chinese ? "未启用" : "disabled"));
        const char* nextStep = nullptr;
        if (!runtimeStatus.dmaReady)
            nextStep = chinese ? "下一步：检查 DMA 连接及设备设置，稍候自动重试或在设置→通用点击重建设备连接。" : "Next: check DMA and device settings, wait for automatic retry or use Settings > General > Rebuild DMA connection.";
        else if (!connected)
            nextStep = chinese ? "下一步：打开游戏，或在设置→通用点击重连。" : "Next: start the game, or use Settings > General > Reconnect.";
        else if (!keysReady)
            nextStep = chinese ? "下一步：在设置→诊断查看主机按键采样错误。" : "Next: check host key sampling under Settings > Diagnostics.";
        else if (!cameraFresh)
            nextStep = chinese ? "下一步：进入游戏场景；若仍未更新，在设置→通用重连。" : "Next: enter a game scene; reconnect if the camera remains stale.";
        else if (!deviceReady)
            nextStep = chinese ? "下一步：在设置→KMBox 设置外设并检查连接。" : "Next: configure and check the device under Settings > KMBox.";
        else if (mockDevice)
            nextStep = chinese ? "当前外设为模拟设备，未使用真实外设。" : "The active device is a mock; no physical device is in use.";
        else if (OW::Config::OutputSuppressedByMenu())
            nextStep = chinese ? "外设就绪；菜单打开时已暂停输出。" : "Device ready; output is paused while this menu is open.";
        else
            nextStep = chinese ? "采样及外设就绪；完整状态可在设置→诊断查看。" : "Samples and device ready. See Settings > Diagnostics for details.";
        const char* summary = !runtimeStatus.dmaReady ? L("DMA unavailable")
            : !connected ? L("Game disconnected")
            : !keysReady ? L("Key sampling unavailable")
            : !cameraFresh ? L("Camera data stale")
            : !deviceReady ? L(OW::Config::outputEnabled ? "Device unavailable" : "Device disabled")
            : mockDevice ? L("Mock device")
            : OW::Config::OutputSuppressedByMenu() ? L("Output paused (menu)")
            : L("Connected");
        dl->AddLine(pos, ImVec2(pos.x + width, pos.y), kColStroke);
        const float diagnosticWidth = (std::max)(98.0f, ImGui::CalcTextSize(T(UiText::SubDiagnostics)).x+20.0f);
        ImGui::PushClipRect(pos, ImVec2(pos.x + width - diagnosticWidth - 20.0f, pos.y + ImGui::GetFontSize()+16.0f), true);
        ImGui::SetCursorScreenPos(ImVec2(pos.x + 12.0f, pos.y + 6.0f));
        ImGui::TextDisabled("%s", summary);
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("%s\n%s", readiness, nextStep);
        ImGui::SameLine();
        if (!s_configSaveStatus.empty()) {
            ImGui::TextColored(s_configSaveFailed ? ImVec4(1.0f,.4f,.3f,1) : ImVec4(.7f,.73f,.77f,1),
                               "%s", s_configSaveStatus.c_str());
        }
        ImGui::PopClipRect();
        ImGui::SetCursorScreenPos(ImVec2(pos.x + width - diagnosticWidth - 4.0f, pos.y + 3.0f));
        if (ImGui::Button(T(UiText::SubDiagnostics), ImVec2(diagnosticWidth, ControlHeight()))) {
            UI::state.activeTab = UI::TAB_MISC;
            UI::state.miscSubTab = 1;
        }
}

static void DrawHeroStrip() {
    static int lastSelection=-1;
    ImGui::BeginChild("HeroStrip",ImVec2(0,82),false,ImGuiWindowFlags_HorizontalScrollbar);
    for(int i=0;i<IM_ARRAYSIZE(kHeroOptions);++i) {
        if(i)ImGui::SameLine(0,8);
        const auto& hero=kHeroOptions[i];
        ImGui::PushID(i);
        const bool selected=UI::state.selectedTypeIndex==i;
        const auto pos=ImGui::GetCursorScreenPos();
        if(selected)ImGui::PushStyleColor(ImGuiCol_Button,ImGui::ColorConvertU32ToFloat4(kColAccentDark));
        if(ImGui::Button("##hero",ImVec2(52,58))) { UI::state.followDetectedHero=false; SelectTypeIndex(i); }
        if(selected)ImGui::PopStyleColor();
        auto* draw=ImGui::GetWindowDrawList();
        if(auto* avatar=HeroAvatarForOption(hero))
            draw->AddImage(reinterpret_cast<ImTextureID>(avatar),ImVec2(pos.x+4,pos.y+4),ImVec2(pos.x+48,pos.y+48));
        else draw->AddText(ImVec2(pos.x+6,pos.y+18),kColText,hero.heroId?"?":"当前");
        if(selected)draw->AddRectFilled(ImVec2(pos.x+4,pos.y+53),ImVec2(pos.x+48,pos.y+56),kColAccent);
        if(selected && lastSelection!=i)ImGui::SetScrollHereX(0.5f);
        ImGui::SetItemTooltip("%s",HeroScopeName(hero));
        ImGui::PopID();
    }
    lastSelection=UI::state.selectedTypeIndex;
    ImGui::EndChild();
}

static void DrawEditingContext() {
    const bool heroPage = UI::state.activeTab == UI::TAB_AIMING ||
        (UI::state.activeTab == UI::TAB_THEME && UI::state.themeSubTab == 1);
    if (!heroPage) {
        ImGui::TextDisabled("%s", L("Shared settings"));
        ImGui::Spacing();
        return;
    }
    DrawHeroStrip();
    ImGui::AlignTextToFramePadding();
    ImGui::TextDisabled("%s", L("Editing"));
    ImGui::SameLine();
    if (TypeSelectorButton(CurrentHeroOption(), ImVec2((std::min)(192.0f,ImGui::GetContentRegionAvail().x), ControlHeight())))
        ImGui::OpenPopup("TypePickerPopup");
    const float followWidth = ImGui::CalcTextSize(L("Follow current hero")).x + ControlHeight() + 20.0f;
    if (ImGui::GetWindowPos().x + ImGui::GetWindowContentRegionMax().x - ImGui::GetItemRectMax().x > followWidth) ImGui::SameLine();
    if (UICheckbox(L("Follow current hero"), &UI::state.followDetectedHero) &&
        UI::state.followDetectedHero) {
        s_lastSyncedDetectedHeroId = 0;
        SyncSelectedTypeWithDetectedHero();
    }
    TypePickerPanel();
    const std::string detectedName = HeroDisplayNameForId(DetectedLocalHeroId());
    const float detectedWidth = ImGui::CalcTextSize(L("Current")).x + ImGui::CalcTextSize(L(detectedName.c_str())).x + 24.0f;
    if (ImGui::GetWindowPos().x + ImGui::GetWindowContentRegionMax().x - ImGui::GetItemRectMax().x > detectedWidth) ImGui::SameLine();
    ImGui::TextDisabled("%s: %s", L("Current"), L(detectedName.c_str()));
    const HeroOption& hero = CurrentHeroOption();
    const bool ringPage = UI::state.activeTab == UI::TAB_THEME;
    const bool slotPage = UI::state.activeTab == UI::TAB_AIMING && UI::state.aimingSubTab < 2;
    const char* source = L("Hero-specific settings");
    if (ringPage) source = L("Shared ring styles; hero selects the preview schemes.");
    else if (hero.heroId == 0) source = L("Current runtime settings");
    else if (slotPage) {
        OW::Config::HeroSlotPreset slot{};
        const bool present = UI::state.aimingSubTab == 0
            ? OW::Config::TryGetHeroAimSlot(hero.heroId, UI::state.aimHeroSegActive, slot)
            : OW::Config::TryGetHeroTriggerSlot(hero.heroId, UI::state.triggerHeroSegActive, slot);
        source = present ? L("Hero-specific scheme")
            : L("Uses current settings until an independent scheme is saved.");
    }
    ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled));
    ImGui::TextWrapped("%s", source);
    ImGui::PopStyleColor();
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("%s", L("Current settings edits the live configuration. Select a hero to edit its own schemes."));
    ImGui::Spacing();
}

// =====================================================================
// UI::Render  --  Main entry point, called each frame.
//                 Draws the top bar, sub-tabs, and page body.
// =====================================================================
void UI::Render() {
    OW::Config::ScopedConfigEdit configEdit;
    EnsurePreNewFrameInitHook();
    if (!state.initialized) InitStyle();
    SyncSelectedTypeWithDetectedHero();
    if (!OW::Config::Menu) return;
    int page = 0;
    if(state.activeTab==TAB_AIMING) page=ImClamp(state.aimingSubTab,0,3);
    else if(state.activeTab==TAB_VISUALS) page=4;
    else if(state.activeTab==TAB_THEME) page=5+ImClamp(state.themeSubTab,0,1);
    else { const int mapping[]={12,13,7,8,9,10,11}; page=mapping[ImClamp(state.miscSubTab,0,6)]; }
    const bool chinese=OW::Config::uiLanguage==OW::Config::kUiLanguageChineseSimplified;
    const Workspace::Page pages[] = {
        {chinese?"瞄准":"Aim", chinese?"英雄方案":"Hero schemes"},
        {chinese?"扳机":"Trigger", chinese?"英雄方案":"Hero schemes"},
        {chinese?"技能":"Skills", chinese?"英雄方案":"Hero schemes"},
        {chinese?"序列":"Sequences", chinese?"英雄方案":"Hero schemes"},
        {chinese?"画面":"Visuals", chinese?"显示":"Display"},
        {chinese?"主题":"Theme", chinese?"显示":"Display"},
        {chinese?"范围圈":"FOV rings", chinese?"显示":"Display"},
        {"Behavior", chinese?"共享参数":"Shared settings"},
        {chinese?"运动方法":"Methods", chinese?"共享参数":"Shared settings"},
        {chinese?"动态范围":"Dynamic FOV", chinese?"共享参数":"Shared settings"},
        {chinese?"设备":"Devices", chinese?"系统":"System"},
        {chinese?"屏幕":"Screen", chinese?"系统":"System"},
        {chinese?"配置":"Configuration", chinese?"系统":"System"},
        {chinese?"诊断":"Diagnostics", chinese?"系统":"System"}
    };
    if (!s_configSaveFailed && ImGui::GetTime() >= s_configSaveStatusUntil) s_configSaveStatus.clear();
    Workspace::Model model{pages,page,T(UiText::SaveAllChanges),s_configSaveStatus.c_str(),s_configSaveFailed};
    Workspace::Render(model, [] {
        SetPersistenceStatus(OW::Config::SaveAllConfig(OW::Config::ConfigPath()), T(UiText::SavedAllChanges));
    }, [&](int selected) {
        page=selected;
        if (page<4) { state.activeTab=TAB_AIMING; state.aimingSubTab=page; }
        else if (page==4) state.activeTab=TAB_VISUALS;
        else if (page<7) { state.activeTab=TAB_THEME; state.themeSubTab=page-5; }
        else { state.activeTab=TAB_MISC; const int mapping[]={2,3,4,5,6,0,1}; state.miscSubTab=mapping[page-7]; }
        DrawEditingContext();
        switch(page) {
        case 0: AimbotPage(); break; case 1: TriggerPage(); break;
        case 2: SkillsPage(); break; case 3: SequencesPage(); break;
        case 4: VisualsPage(); break; case 5: DrawThemeGeneralPage(); break;
        case 6: DrawThemeFovRingsPage(); break; case 7: DrawMiscBehaviorPage(); break;
        case 8: DrawMiscMethodPage(); break; case 9: DrawMiscDynamicFovPage(); break;
        case 10: DrawMiscOutputPage(); break; case 11: DrawMiscScreenPage(); break;
        case 12: DrawMiscGeneralPage(); break; case 13: DrawMiscDiagnosticsPage(); break;
        }
        CloseGroupBox();
    }, DrawRuntimeStatusStrip);
    page=model.selected;
}
