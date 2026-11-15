#include "NetworkMaintenance.hpp"
#include <Windows.h>
#include <shellapi.h>
#include <array>
#include <cstdint>
#include <cstdio>
#include <vector>

namespace V2::Output::Platform {
namespace {
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

}

MaintenanceResult AllowInboundUdpForCurrentExecutable(int monitorPort, const std::wstring& ruleName) {
        if (monitorPort <= 0 || monitorPort > 65535)
            return { false, "Invalid monitor port" };

        const std::wstring executablePath = CurrentExecutablePath();
        if (executablePath.empty())
            return { false, "EXE path unavailable" };

        const std::wstring exe = PowerShellSingleQuoted(executablePath);
        const std::wstring displayName =
            PowerShellSingleQuoted(ruleName);
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

        return { true, "Firewall rule requested" };
    }

MaintenanceResult RestartAdapterForTargetIp(const char* ip, const char* fallbackDescription) {
        const std::wstring targetIp = PowerShellSingleQuoted(ip);
        const std::wstring fallback = PowerShellSingleQuoted(fallbackDescription);
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
            L"$adapter = Get-NetAdapter -InterfaceDescription " + fallback + L" -ErrorAction Stop | "
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

}
