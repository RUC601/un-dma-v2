#include "Speech.hpp"
#include <Windows.h>
#include <mmsystem.h>
#include <mmddk.h>
#include <fstream>
#include <sstream>
#include <stdexcept>
#include <cstring>

namespace V2::Alerts {
namespace {
std::wstring Wide(const std::string& text) {
    int count = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, text.data(), int(text.size()), nullptr, 0);
    std::wstring out(count, L'\0');
    if (count) MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, text.data(), int(text.size()), out.data(), count);
    return out;
}
std::string Utf8(const wchar_t* text) {
    int count = WideCharToMultiByte(CP_UTF8, 0, text, -1, nullptr, 0, nullptr, nullptr);
    std::string out(count, '\0');
    if (count) WideCharToMultiByte(CP_UTF8, 0, text, -1, out.data(), count, nullptr, nullptr);
    if (!out.empty()) out.pop_back();
    return out;
}
std::filesystem::path VoiceRoot() {
    std::wstring path(32768, L'\0');
    DWORD count = GetModuleFileNameW(nullptr, path.data(), DWORD(path.size()));
    if (!count || count >= path.size()) throw std::runtime_error("voice_path");
    path.resize(count);
    return std::filesystem::path(path).parent_path() / L"alert-voice";
}
std::wstring Quote(const std::wstring& value) {
    std::wstring result = L"\"";
    size_t slashes = 0;
    for (wchar_t ch : value) {
        if (ch == L'\\') { ++slashes; continue; }
        result.append(slashes * (ch == L'\"' ? 2 : 1), L'\\');
        slashes = 0;
        if (ch == L'\"') result += L'\\';
        result += ch;
    }
    result.append(slashes * 2, L'\\');
    return result + L'\"';
}
std::wstring Pronunciation(const std::string& text) {
    auto upper = [](wchar_t ch) { return ch >= L'A' && ch <= L'Z'; };
    auto letter = [&](wchar_t ch) { return upper(ch) || (ch >= L'a' && ch <= L'z'); };
    const auto wide = Wide(text);
    std::wstring result;
    for (size_t i = 0; i < wide.size();) {
        if (!letter(wide[i])) { result += wide[i++]; continue; }
        const size_t start = i;
        while (i < wide.size() && letter(wide[i])) ++i;
        const auto word = std::wstring_view(wide).substr(start, i-start);
        // Kokoro's lexicon skips unknown words. Speak uppercase initials using its letter entries.
        if (word.size() > 1 && std::all_of(word.begin(), word.end(), upper)) {
            result += L' ';
            for (wchar_t ch : word) {
                result += wchar_t(ch - L'A' + L'a');
                result += L' ';
            }
        } else result += word;
    }
    return result;
}
std::wstring CacheName(const std::string& message, int voice, bool legacy = false) {
    uint64_t hash = 14695981039346656037ull;
    for (unsigned char ch : message) { hash ^= ch; hash *= 1099511628211ull; }
    std::wostringstream name;
    name << (legacy ? L"kokoro11-" : L"kokoro11-initials-") << voice << L"-" << std::hex << hash;
    return name.str();
}
void Generate(const std::filesystem::path& root, const std::filesystem::path& output,
              const Settings& settings, const RuleSettings& rule, const Cancel& cancelled) {
    const auto bin = root / L"sherpa-onnx-v2.0.1-win-x64-shared-MD-Release" / L"bin";
    const auto executable = bin / L"sherpa-onnx-offline-tts.exe";
    const auto model = root / L"kokoro-int8-multi-lang-v1_1";
    if (!std::filesystem::exists(executable) || !std::filesystem::exists(model / L"model.int8.onnx"))
        throw std::runtime_error("voice_missing");
    std::vector<std::wstring> arguments{
        executable.wstring(), L"--debug=0", L"--num-threads=2",
        L"--kokoro-model=" + (model / L"model.int8.onnx").wstring(),
        L"--kokoro-voices=" + (model / L"voices.bin").wstring(),
        L"--kokoro-tokens=" + (model / L"tokens.txt").wstring(),
        L"--kokoro-lexicon=" + (model / L"lexicon-us-en.txt").wstring() + L"," + (model / L"lexicon-zh.txt").wstring(),
        L"--sid=" + std::to_wstring(settings.voice == 1 ? 58 : 3),
        L"--output-filename=" + output.wstring(), Pronunciation(rule.message)
    };
    std::wstring command;
    for (const auto& argument : arguments) { if (!command.empty()) command += L' '; command += Quote(argument); }
    STARTUPINFOW startup{sizeof(startup)};
    PROCESS_INFORMATION process{};
    if (!CreateProcessW(executable.c_str(), command.data(), nullptr, nullptr, FALSE,
                        CREATE_NO_WINDOW, nullptr, bin.c_str(), &startup, &process))
        throw std::runtime_error("voice_start_failed");
    CloseHandle(process.hThread);
    const uint64_t started = GetTickCount64();
    while (WaitForSingleObject(process.hProcess, 20) == WAIT_TIMEOUT) {
        if (cancelled() || GetTickCount64()-started > 30000) {
            // This handle belongs only to the TTS child created immediately above.
            TerminateProcess(process.hProcess, 1);
            WaitForSingleObject(process.hProcess, INFINITE);
            CloseHandle(process.hProcess);
            throw std::runtime_error("voice_cancelled");
        }
    }
    DWORD code = 1;
    GetExitCodeProcess(process.hProcess, &code);
    CloseHandle(process.hProcess);
    if (code) throw std::runtime_error("voice_generation_failed");
}
}
std::vector<AudioDevice> PlaybackDevices() {
    std::vector<AudioDevice> devices{{"", ""}};
    for (UINT index = 0; index < waveOutGetNumDevs(); ++index) {
        WAVEOUTCAPSW caps{};
        DWORD bytes = 0;
        const auto device = reinterpret_cast<HWAVEOUT>(UINT_PTR(index));
        if (waveOutGetDevCapsW(index, &caps, sizeof(caps)) != MMSYSERR_NOERROR ||
            waveOutMessage(device, DRV_QUERYFUNCTIONINSTANCEIDSIZE, DWORD_PTR(&bytes), 0) != MMSYSERR_NOERROR ||
            bytes < sizeof(wchar_t) || bytes > 32768) continue;
        std::vector<wchar_t> id(bytes/sizeof(wchar_t)+1, L'\0');
        if (waveOutMessage(device, DRV_QUERYFUNCTIONINSTANCEID, DWORD_PTR(id.data()), bytes) == MMSYSERR_NOERROR)
            devices.push_back({Utf8(id.data()), Utf8(caps.szPname), index});
    }
    return devices;
}
AudioClip ReadClip(const std::filesystem::path& path) {
    std::ifstream input(path, std::ios::binary | std::ios::ate);
    if (!input) throw std::runtime_error("voice_clip_missing");
    const auto size = input.tellg();
    if (size < 44 || size > 4*1024*1024) throw std::runtime_error("voice_clip_invalid");
    std::vector<char> bytes(static_cast<size_t>(size));
    input.seekg(0); input.read(bytes.data(), std::streamsize(bytes.size()));
    if (!input || std::memcmp(bytes.data(), "RIFF", 4) || std::memcmp(bytes.data()+8, "WAVE", 4))
        throw std::runtime_error("voice_clip_invalid");
    auto u32 = [&](size_t at) { uint32_t value; std::memcpy(&value, bytes.data()+at, 4); return value; };
    auto u16 = [&](size_t at) { uint16_t value; std::memcpy(&value, bytes.data()+at, 2); return value; };
    AudioClip clip;
    size_t data = 0, dataSize = 0;
    for (size_t at = 12; at+8 <= bytes.size();) {
        const size_t length = u32(at+4), start = at+8;
        if (length > bytes.size()-start) throw std::runtime_error("voice_clip_invalid");
        if (!std::memcmp(bytes.data()+at, "fmt ", 4)) {
            if (length < 16 || u16(start) != 1 || u16(start+2) != 1 || u16(start+14) != 16)
                throw std::runtime_error("voice_clip_format");
            clip.sampleRate = u32(start+4);
        } else if (!std::memcmp(bytes.data()+at, "data", 4)) { data = start; dataSize = length; }
        at = start+length+(length&1);
    }
    if (clip.sampleRate < 8000 || clip.sampleRate > 48000 || !dataSize || dataSize%2 ||
        dataSize > size_t(clip.sampleRate)*2*15) throw std::runtime_error("voice_clip_invalid");
    clip.samples.resize(dataSize/2);
    std::memcpy(clip.samples.data(), bytes.data()+data, dataSize);
    return clip;
}
AudioClip PrepareSpeech(const Settings& settings, const RuleSettings& rule, const Cancel& cancelled) {
    const auto root = VoiceRoot(), cache = root / L"cache";
    const auto name = CacheName(rule.message, settings.voice);
    const auto wave = cache / (name+L".wav"), text = cache / (name+L".txt");
    std::ifstream signature(text, std::ios::binary);
    std::string previous((std::istreambuf_iterator<char>(signature)), {});
    if (previous == rule.message) {
        try { return ReadClip(wave); } catch (const std::exception&) { /* Regenerate a damaged cache file. */ }
    }
    const auto executable = root / L"sherpa-onnx-v2.0.1-win-x64-shared-MD-Release/bin/sherpa-onnx-offline-tts.exe";
    if (!std::filesystem::is_regular_file(executable) ||
        !std::filesystem::is_regular_file(root / L"kokoro-int8-multi-lang-v1_1/model.int8.onnx"))
        throw std::runtime_error("voice_missing");
    std::filesystem::create_directories(cache);
    const auto temporary = cache / (name+L"-"+std::to_wstring(GetCurrentProcessId())+L".tmp.wav");
    try {
        Generate(root, temporary, settings, rule, cancelled);
        auto clip = ReadClip(temporary);
        if (cancelled()) throw std::runtime_error("voice_cancelled");
        if (!MoveFileExW(temporary.c_str(), wave.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH))
            throw std::runtime_error("voice_cache_failed");
        std::ofstream stamp(text, std::ios::binary | std::ios::trunc);
        stamp << rule.message;
        return clip;
    } catch (...) {
        DeleteFileW(temporary.c_str());
        throw;
    }
}
void RemoveSpeechCache(const std::string& message) {
    const auto cache = VoiceRoot() / L"cache";
    for (int voice = 0; voice < 2; ++voice) {
        for (bool legacy : {false, true}) {
            const auto name = CacheName(message, voice, legacy);
            const auto signature = cache / (name+L".txt");
            std::ifstream input(signature, std::ios::binary);
            const std::string stored((std::istreambuf_iterator<char>(input)), {});
            input.close();
            if (stored != message) continue;
            std::error_code ignored;
            std::filesystem::remove(cache / (name+L".wav"), ignored);
            if (!ignored) std::filesystem::remove(signature, ignored);
        }
    }
}
bool PlaySpeech(const AudioClip& clip, const Settings& settings, const Cancel& cancelled) {
    if (cancelled()) return false;
    UINT device = WAVE_MAPPER;
    if (!settings.outputDevice.empty()) {
        const auto devices = PlaybackDevices();
        auto found = std::find_if(devices.begin()+1, devices.end(), [&](const auto& item) { return item.id == settings.outputDevice; });
        if (found == devices.end()) throw std::runtime_error("audio_device_missing");
        device = found->index;
    }
    auto samples = clip.samples;
    for (auto& value : samples) value = int16_t(int(value)*settings.volume/100);
    WAVEFORMATEX format{WAVE_FORMAT_PCM, 1, clip.sampleRate, clip.sampleRate*2, 2, 16, 0};
    HANDLE completed = CreateEventW(nullptr, FALSE, FALSE, nullptr);
    if (!completed) throw std::runtime_error("audio_open_failed");
    HWAVEOUT output{};
    if (waveOutOpen(&output, device, &format, DWORD_PTR(completed), 0, CALLBACK_EVENT) != MMSYSERR_NOERROR) {
        CloseHandle(completed); throw std::runtime_error("audio_open_failed");
    }
    WAVEHDR header{};
    header.lpData = reinterpret_cast<char*>(samples.data());
    header.dwBufferLength = DWORD(samples.size()*sizeof(int16_t));
    const bool prepared = waveOutPrepareHeader(output, &header, sizeof(header)) == MMSYSERR_NOERROR;
    bool success = prepared && waveOutWrite(output, &header, sizeof(header)) == MMSYSERR_NOERROR;
    const uint64_t started = GetTickCount64();
    while (success && !(header.dwFlags & WHDR_DONE)) {
        WaitForSingleObject(completed, 20);
        if (cancelled() || GetTickCount64()-started > 16000) success = false;
    }
    waveOutReset(output);
    if (prepared) waveOutUnprepareHeader(output, &header, sizeof(header));
    waveOutClose(output); CloseHandle(completed);
    if (!success && !cancelled()) throw std::runtime_error("audio_play_failed");
    return success;
}
}
