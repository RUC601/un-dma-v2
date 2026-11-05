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

namespace V2::Alerts {
namespace {
struct Request {
    std::string rule;
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
void Run(std::stop_token stop) {
    std::optional<Settings> prepared;
    std::unordered_map<std::string, AudioClip> clips;
    uint64_t attemptedRetry = retry.load();
    while (!stop.stop_requested()) {
        const auto settings = GetSettings();
        if (!prepared || *prepared != settings || attemptedRetry != retry.load()) {
            attemptedRetry = retry.load();
            State(SpeechState::Preparing);
            auto cancelled = [&] { return stop.stop_requested() || GetSettings() != settings; };
            try {
                // Settings are debounced so typing does not start a synthesizer for each character.
                for (int i = 0; i < 15 && !cancelled(); ++i) Sleep(20);
                if (cancelled()) continue;
                clips.clear();
                for (const auto& rule : settings.rules) clips.emplace(rule.id, PrepareSpeech(settings, rule, cancelled));
                if (cancelled()) continue;
                State(SpeechState::Ready);
            } catch (const std::exception& error) {
                if (cancelled()) continue;
                clips.clear();
                State(SpeechState::Failed, error.what());
            }
            prepared = settings;
        }
        std::optional<Request> request;
        {
            std::unique_lock lock(serviceMutex);
            changed.wait_for(lock, stop, std::chrono::milliseconds(50), [] { return !requests.empty(); });
            if (!requests.empty()) { request = std::move(requests.front()); requests.pop_front(); }
        }
        if (!request || stop.stop_requested()) continue;
        auto cancelled = [&] { return stop.stop_requested() || GetSettings() != request->settings ||
            (request->stillCurrent && !request->stillCurrent()); };
        const auto clip = clips.find(request->rule);
        if (request->deadline < GetTickCount64() || request->settings != settings || clip == clips.end() || cancelled()) {
            std::lock_guard lock(serviceMutex); ++status.discarded; continue;
        }
        try {
            State(SpeechState::Speaking);
            const bool played = PlaySpeech(clip->second, request->settings, cancelled);
            std::lock_guard lock(serviceMutex);
            if (played) ++status.played; else ++status.discarded;
            status.state = SpeechState::Ready;
        } catch (const std::exception& error) { State(SpeechState::Failed, error.what()); }
    }
    State(SpeechState::Stopped);
}
void Enqueue(const char* ruleId, std::function<bool()> stillCurrent, bool preview) {
    const auto settings = GetSettings();
    const auto rule = std::find_if(settings.rules.begin(), settings.rules.end(),
        [&](const auto& item) { return item.id == ruleId; });
    if (rule == settings.rules.end() || (!preview && (!settings.enabled || !rule->enabled))) return;
    std::lock_guard lock(serviceMutex);
    if (!accepting) return;
    if (requests.size() == 4) { requests.pop_front(); ++status.discarded; }
    requests.push_back({ruleId, settings, GetTickCount64() + (preview ? 30000 : 1000), std::move(stillCurrent)});
    ++status.submitted;
    changed.notify_one();
}
}
void Start() {
    std::lock_guard lock(serviceMutex);
    if (speaker.joinable()) return;
    speaker = std::jthread(Run);
    accepting = true;
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
void Preview(const char* ruleId) { Enqueue(ruleId, {}, true); }
void Submit(const char* ruleId, std::function<bool()> stillCurrent) { Enqueue(ruleId, std::move(stillCurrent), false); }
}
