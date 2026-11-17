#pragma once
#include <algorithm>
#include <charconv>
#include <cctype>
#include <filesystem>
#include <istream>
#include <map>
#include <string>

enum class DmaDeviceType {
    FTD3XX = 0, FTD3XXWU, RAWTCP, HVSAVEDSTATE, DRIVER, RAWUDP, FT2232H, FILE_DUMP
};

// Device strings follow LeechCore's device_fpga.c and LeechCore-plugins.
struct DmaDeviceConfig {
    DmaDeviceType type = DmaDeviceType::FTD3XX;
    std::string device = "fpga://algo=0";
    int memoryMap = -1;
    bool IsStatic() const { return type == DmaDeviceType::HVSAVEDSTATE || type == DmaDeviceType::FILE_DUMP; }
    bool IsFpga() const { return !IsStatic() && type != DmaDeviceType::RAWTCP; }
    static std::filesystem::path Utf8Path(const std::string& value) {
        return std::filesystem::path(std::u8string(value.begin(), value.end()));
    }

    static std::string Trim(std::string text) {
        const auto nonSpace = [](unsigned char c) { return !std::isspace(c); };
        text.erase(text.begin(), std::find_if(text.begin(), text.end(), nonSpace));
        text.erase(std::find_if(text.rbegin(), text.rend(), nonSpace).base(), text.end());
        return text;
    }
    static std::string Lower(std::string text) {
        std::transform(text.begin(), text.end(), text.begin(), [](unsigned char c) { return char(std::tolower(c)); });
        return text;
    }
    static bool Number(const std::string& value, unsigned maximum, unsigned& number) {
        const auto result = std::from_chars(value.data(), value.data() + value.size(), number);
        return !value.empty() && result.ec == std::errc{} && result.ptr == value.data() + value.size() && number <= maximum;
    }
    static bool IPv4(const std::string& text) {
        size_t start = 0;
        for (int part = 0; part < 4; ++part) {
            const auto end = text.find('.', start);
            if ((part < 3) != (end != std::string::npos)) return false;
            const auto value = text.substr(start, end == std::string::npos ? end : end - start);
            unsigned number = 0;
            if (!Number(value, 255, number) || (value.size() > 1 && value.front() == '0')) return false;
            start = end + 1;
        }
        return text != "0.0.0.0" && text != "255.255.255.255";
    }
    static bool Parse(std::istream& input, const std::filesystem::path& base,
                      DmaDeviceConfig& output, std::string& error) {
        auto fail = [&](const std::string& why) { error = why; return false; };
        std::map<std::string, std::string> values;
        std::string line;
        unsigned lineNumber = 0;
        while (std::getline(input, line)) {
            ++lineNumber;
            if (lineNumber == 1 && line.starts_with("\xef\xbb\xbf")) line.erase(0, 3);
            line = Trim(line);
            if (line.empty() || line.front() == '#' || line.front() == ';') continue;
            bool quoted = false;
            for (size_t i = 0; i < line.size(); ++i) {
                if (line[i] == '"') quoted = !quoted;
                if (!quoted && (line[i] == '#' || line[i] == ';') && i && std::isspace((unsigned char)line[i-1])) {
                    line.resize(i); break;
                }
            }
            const auto equals = line.find('=');
            if (equals == std::string::npos) return fail("expected key=value at line " + std::to_string(lineNumber));
            auto key = Lower(Trim(line.substr(0, equals)));
            auto value = Trim(line.substr(equals + 1));
            if (value.starts_with('"')) {
                if (value.size() < 2 || value.back() != '"') return fail("unclosed quoted value");
                value = value.substr(1, value.size()-2);
            }
            if (key == "address") key = "host";
            if (!values.emplace(key, value).second) return fail("duplicate key: " + key);
        }
        if (input.bad()) return fail("configuration read failed");
        if (!values.count("device")) return fail("missing device=");
        DmaDeviceConfig candidate;
        const auto name = Lower(values["device"]);
        if (name == "fpga" || name == "ftd3xx") candidate.type = DmaDeviceType::FTD3XX;
        else if (name == "ft601" || name == "ftd3xxwu") candidate.type = DmaDeviceType::FTD3XXWU;
        else if (name == "driver" || name == "zdma" || name == "leechdma") candidate.type = DmaDeviceType::DRIVER;
        else if (name == "rawtcp") candidate.type = DmaDeviceType::RAWTCP;
        else if (name == "rawudp") candidate.type = DmaDeviceType::RAWUDP;
        else if (name == "ft2232h") candidate.type = DmaDeviceType::FT2232H;
        else if (name == "hvsavedstate") candidate.type = DmaDeviceType::HVSAVEDSTATE;
        else if (name == "file") candidate.type = DmaDeviceType::FILE_DUMP;
        else return fail("unknown device: " + name);

        std::map<std::string, unsigned> fpgaOptions;
        for (const auto& [key, value] : values) {
            if (key == "device" || key == "host" || key == "file") continue;
            unsigned number = 0;
            if (key == "memory_map") {
                if (!Number(value, 1, number)) return fail("memory_map must be 0 or 1");
                candidate.memoryMap = int(number); continue;
            }
            if (!candidate.IsFpga()) return fail("unsupported key for this source: " + key);
            const unsigned maximum = key == "algo" ? 4 : key == "devindex" ? 15 :
                key == "pciegen" ? 2 : key == "readretry" ? 1 : key == "readsize" ? 16777216 :
                (key == "tmread" || key == "tmwrite" || key == "tmprobe") ? 1000000 : 0;
            if (!maximum || !Number(value, maximum, number)) return fail("invalid FPGA option: " + key);
            if ((key == "pciegen" && !number) || (key == "readsize" && (!number || number % 4096)))
                return fail("invalid FPGA option value: " + key);
            fpgaOptions.emplace(key, number);
        }
        if (candidate.IsStatic()) {
            if (values.count("host") || !values.count("file") || values["file"].empty()) return fail("file source requires file= and no host=");
            auto path = Utf8Path(values["file"]);
            if (path.is_relative()) path = base / path;
            path = path.lexically_normal();
            if (candidate.type == DmaDeviceType::HVSAVEDSTATE && Lower(path.extension().string()) != ".vmrs")
                return fail("hvsavedstate requires a .vmrs file");
            const auto utf8 = path.generic_u8string();
            candidate.device = (candidate.type == DmaDeviceType::HVSAVEDSTATE ? "hvsavedstate://" : "") + std::string(utf8.begin(), utf8.end());
        } else {
            if (values.count("file")) return fail("file= only applies to file/hvsavedstate");
            if (candidate.type == DmaDeviceType::RAWTCP || candidate.type == DmaDeviceType::RAWUDP) {
                auto host = values["host"];
                const auto lowerHost = Lower(host);
                // Accept the old config spelling, but always select the real plugin.
                if (lowerHost.starts_with("rawtcp://")) host.erase(0, 9);
                else if (lowerHost.starts_with("tcp://")) host.erase(0, 6);
                const auto colon = host.find(':');
                const auto ip = host.substr(0, colon);
                if (!IPv4(ip)) return fail("host must be a numeric IPv4 address");
                if (candidate.type == DmaDeviceType::RAWTCP) {
                    unsigned port = 0;
                    if (colon != std::string::npos && (!Number(host.substr(colon+1), 65535, port) || !port)) return fail("invalid TCP port");
                    candidate.device = "rawtcp://" + host;
                } else {
                    if (colon != std::string::npos) return fail("rawudp uses the firmware's fixed port 28474; omit port");
                    candidate.device = "fpga://ip=" + ip;
                }
            } else {
                if (values.count("host")) return fail("host= only applies to rawtcp/rawudp");
                candidate.device = "fpga://";
                if (candidate.type == DmaDeviceType::DRIVER) candidate.device += "driver=1,";
                if (candidate.type == DmaDeviceType::FTD3XXWU) candidate.device += "ft601=1,";
                if (candidate.type == DmaDeviceType::FT2232H) candidate.device += "ft2232h=1,";
                candidate.device += "algo=" + std::to_string(fpgaOptions.count("algo") ? fpgaOptions["algo"] : 0);
            }
            if (candidate.IsFpga()) for (const auto& [key, value] : fpgaOptions) {
                if (key == "algo" && candidate.type != DmaDeviceType::RAWUDP) continue;
                candidate.device += "," + key + "=" + std::to_string(value);
            }
        }
        if (candidate.device.size() >= 260 || candidate.device.find('\0') != std::string::npos)
            return fail("device string exceeds LeechCore MAX_PATH or contains NUL");
        output = std::move(candidate);
        error.clear();
        return true;
    }
};
