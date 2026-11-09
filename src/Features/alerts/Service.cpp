#include "Alerts/Service.hpp"
#include "Speech.hpp"
#include <Windows.h>
#include <atomic>
#include <condition_variable>
#include <deque>
#include <mutex>
#include <optional>
#include <thread>
#include <unordered_map>
#include <unordered_set>

namespace V2::Alerts {
namespace {
struct Request {
    std::string message;
    Settings settings;
    uint64_t deadline = 0;
    std::function<bool()> stillCurrent;
};
std::mutex serviceMutex;
std::condition_variable_any changed;
std::jthread speaker;
std::deque<Request> requests;
Status status;
bool accepting = false;
std::atomic<uint64_t> retry{0};
void State(SpeechState state, const std::string& error = {}) {
    std::lock_guard lock(serviceMutex);
    status.state = state; status.error = error;
}
std::unordered_set<std::string> Messages(const Settings& settings) {
    std::unordered_set<std::string> messages{settings.ultimateMessage};
    for (const auto& rule : settings.rules) messages.insert(rule.message);
    for (const auto& rule : settings.ultimates)
        if (!rule.message.empty()) messages.insert(rule.message);
    return messages;
}
void Run(std::stop_token stop) {
    std::optional<Settings> prepared;
    std::unordered_map<std::string, AudioClip> clips;
    std::deque<std::string> warmup;
    std::unordered_set<std::string> editedMessages, retiredMessages;
    uint64_t attemptedRetry = retry.load();
    uint64_t warmAfter = 0;
    while (!stop.stop_requested()) {
        const auto settings = GetRuntimeSettings();
        if (!prepared || *prepared != settings || attemptedRetry != retry.load()) {
            attemptedRetry = retry.load();
            const auto current = Messages(settings);
            if (prepared) {
                const auto previous = Messages(*prepared);
                for (const auto& message : previous)
                    if (!current.contains(message)) retiredMessages.insert(message);
                for (size_t i = 0; i < settings.rules.size(); ++i)
                    if (settings.rules[i].message != prepared->rules[i].message)
                        editedMessages.insert(settings.rules[i].message);
                if (settings.ultimateMessage != prepared->ultimateMessage)
                    editedMessages.insert(settings.ultimateMessage);
                for (const auto& rule : settings.ultimates)
                    if (UltimateMessage(settings, rule.hero) != UltimateMessage(*prepared, rule.hero))
                        editedMessages.insert(UltimateMessage(settings, rule.hero));
                for (const auto& rule : prepared->ultimates)
                    if (!rule.message.empty() && !UltimateForHero(settings, rule.hero))
                        editedMessages.insert(settings.ultimateMessage);
            }
            std::erase_if(retiredMessages, [&](const auto& message) { return current.contains(message); });
            std::erase_if(editedMessages, [&](const auto& message) { return !current.contains(message); });
            if (!prepared || prepared->voice != settings.voice) clips.clear();
            else std::erase_if(clips, [&](const auto& item) { return !current.contains(item.first); });
            warmup.clear();
            const auto warm = [&](const std::string& message) {
                if (!clips.contains(message) && std::find(warmup.begin(), warmup.end(), message) == warmup.end())
                    warmup.push_back(message);
            };
            for (const auto& message : editedMessages) warm(message);
            if (settings.skillEnabled)
                for (const auto& rule : settings.rules)
                    if (rule.enabled) warm(rule.message);
            if (settings.ultimateEnabled) {
                warm(settings.ultimateMessage);
                for (const auto& rule : settings.ultimates)
                    if (UltimateHeroEnabled(settings, rule.hero) && !rule.message.empty())
                        warm(rule.message);
            }
            if (warmup.empty()) State(clips.empty() ? SpeechState::Stopped : SpeechState::Ready);
            else State(SpeechState::Preparing);
            prepared = settings;
            warmAfter = GetTickCount64() + 300;
        }
        std::erase_if(editedMessages, [&](const auto& message) { return clips.contains(message); });
        if (editedMessages.empty() && !retiredMessages.empty() &&
            GetTickCount64() >= warmAfter && GetRuntimeSettings() == settings) {
            for (const auto& message : retiredMessages) RemoveSpeechCache(message);
            retiredMessages.clear();
        }
        std::optional<Request> request;
        {
            std::unique_lock lock(serviceMutex);
            if (warmup.empty() || GetTickCount64() < warmAfter)
                changed.wait_for(lock, stop, std::chrono::milliseconds(50), [] { return !requests.empty(); });
            if (!requests.empty()) { request = std::move(requests.front()); requests.pop_front(); }
        }
        if (stop.stop_requested() || (!request && (warmup.empty() || GetTickCount64() < warmAfter))) continue;
        std::string message;
        if (request) message = request->message;
        else { message = std::move(warmup.front()); warmup.pop_front(); }
        auto cancelled = [&] {
            return stop.stop_requested() || GetRuntimeSettings() != settings ||
                (request && (request->settings != settings ||
                    (request->stillCurrent && !request->stillCurrent())));
        };
        if (request && (request->deadline < GetTickCount64() || cancelled())) {
            std::lock_guard lock(serviceMutex); ++status.discarded; continue;
        }
        try {
            auto clip = clips.find(message);
            if (clip == clips.end()) {
                State(SpeechState::Preparing);
                if (!cancelled())
                    clip = clips.emplace(message, PrepareSpeech(settings, {"", true, message}, cancelled)).first;
            }
            if (!request) {
                if (!cancelled()) State(SpeechState::Ready);
                continue;
            }
            if (clip == clips.end() || cancelled() || request->deadline < GetTickCount64()) {
                std::lock_guard lock(serviceMutex); ++status.discarded; continue;
            }
            State(SpeechState::Speaking);
            const bool played = PlaySpeech(clip->second, request->settings, cancelled);
            std::lock_guard lock(serviceMutex);
            if (played) ++status.played; else ++status.discarded;
            status.state = SpeechState::Ready;
        } catch (const std::exception& error) {
            if (request) { std::lock_guard lock(serviceMutex); ++status.discarded; }
            if (cancelled()) continue;
            if (std::string_view(error.what()) == "voice_missing") warmup.clear();
            State(SpeechState::Failed, error.what());
        }
    }
    State(SpeechState::Stopped);
}
void Enqueue(std::string message, Settings settings, std::function<bool()> stillCurrent, bool preview) {
    if (message.empty()) return;
    std::lock_guard lock(serviceMutex);
    if (!accepting) return;
    if (requests.size() == 4) { requests.pop_front(); ++status.discarded; }
    requests.push_back({std::move(message), std::move(settings),
        GetTickCount64() + (preview ? 30000 : 1000), std::move(stillCurrent)});
    ++status.submitted;
    changed.notify_one();
}
}
void Start() {
    std::lock_guard lock(serviceMutex);
    if (speaker.joinable()) return;
    try {
        speaker = std::jthread([](std::stop_token stop) {
            try { Run(stop); }
            catch (const std::exception& error) { State(SpeechState::Failed, error.what()); }
        });
        accepting = true;
    } catch (const std::system_error&) {
        status.state = SpeechState::Failed;
        status.error = "voice_worker_failed";
    }
}
void Stop() {
    {
        std::lock_guard lock(serviceMutex);
        accepting = false;
        status.discarded += requests.size(); requests.clear();
    }
    if (!speaker.joinable()) return;
    speaker.request_stop(); changed.notify_all(); speaker.join();
}
Status GetStatus() { std::lock_guard lock(serviceMutex); return status; }
void RetrySpeech() { ++retry; changed.notify_all(); }
void Preview(std::string message) { Enqueue(std::move(message), GetRuntimeSettings(), {}, true); }
void Submit(const char* ruleId, std::function<bool()> stillCurrent) {
    const auto settings = GetRuntimeSettings();
    if (!settings.skillEnabled) return;
    for (const auto& rule : settings.rules)
        if (rule.id == ruleId && rule.enabled)
            Enqueue(rule.message, settings, std::move(stillCurrent), false);
}
void SubmitUltimate(uint64_t hero, std::function<bool()> stillCurrent) {
    const auto settings = GetRuntimeSettings();
    if (settings.ultimateEnabled && UltimateHeroSupported(hero) && UltimateHeroEnabled(settings, hero))
        Enqueue(UltimateMessage(settings, hero), settings, std::move(stillCurrent), false);
}
}
