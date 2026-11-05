#pragma once
#include "Alerts/Settings.hpp"
#include <functional>

namespace V2::Alerts {
enum class SpeechState { Stopped, Preparing, Ready, Speaking, Failed };
struct Status {
    SpeechState state = SpeechState::Stopped;
    std::string error;
    uint64_t submitted = 0, played = 0, discarded = 0;
};
void Start();
void Stop();
Status GetStatus();
void RetrySpeech();
void Preview(const char* ruleId);
void Submit(const char* ruleId, std::function<bool()> stillCurrent);
void SkillWorker(); // Owned and drained by the process-session WorkerGroup.
}
