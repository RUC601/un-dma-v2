#pragma once

#include <cstdint>

namespace OW::AsheCombo {

// KOI's recovered five-shot control flow, adapted to an immediately armed UN
// session and hold-to-scope output. Device click time is deliberately outside
// the 210/50/240 ms waits: Complete() receives the actual action completion.
inline constexpr int kScopeWaitMs = 210;
inline constexpr int kRecoveryWaitMs = 50;
inline constexpr int kFollowupWaitMs = 240;
inline constexpr int kLoopWaitMs = 1;
inline constexpr int kClickHoldMs = 12; // Current KOI V3 adapter pulse, not DLL evidence.
inline constexpr int kPostShotPitchPauseMs = 35;

enum class Stage : std::uint8_t {
    Idle, Hip, ScopeEnter, ScopedShot, ScopeExit, Followup, Wait
};
enum class Action : std::uint8_t { None, Click, ScopeDown, ScopeUp };
struct Decision {
    Action action = Action::None;
    Stage stage = Stage::Idle;
    int shot = 0;
    std::int64_t deadlineUs = 0;
    bool finished = false;
};

enum class ShotPermit : std::uint8_t { WaitForAmmo, Allow, ReserveReached };

struct AmmoGate {
    std::uint64_t submittedSequence = 0;
    std::uint64_t submittedEpoch = 0;

    ShotPermit Check(bool enabled, bool known, int remaining, int reserve,
                     std::uint64_t sequence, std::uint64_t epoch) const {
        if (!enabled) return ShotPermit::Allow;
        if (!known || remaining < 0 || remaining > 12 || !sequence || !epoch)
            return ShotPermit::WaitForAmmo;
        if (remaining <= reserve) return ShotPermit::ReserveReached;
        if (epoch == submittedEpoch && sequence <= submittedSequence)
            return ShotPermit::WaitForAmmo;
        return ShotPermit::Allow;
    }

    void Submitted(std::uint64_t sequence, std::uint64_t epoch) {
        // A click is not ammunition consumption. Keep only the image identity
        // so the next shot must consult new HUD evidence, including a same-value
        // observation when the game did not accept the previous click.
        submittedSequence = sequence;
        submittedEpoch = epoch;
    }
};

class Runner {
public:
    void Start() { *this = Runner{}; step_ = Step::Hip; stage_ = Stage::Hip; }
    void Cancel() { *this = Runner{}; }
    Stage stage() const { return stage_; }
    int shot() const { return shot_; }
    std::int64_t deadlineUs() const { return deadline_; }

    Decision Advance(std::int64_t nowUs, bool activationHeld) {
        if (pending_ != Action::None) return {Action::None, stage_, shot_, deadline_, false};
        for (;;) {
            switch (step_) {
            case Step::Idle: return {Action::None, Stage::Idle, 0, 0, true};
            case Step::Hip: return Offer(Action::Click, Stage::Hip, 1);
            case Step::EnterScope: return Offer(Action::ScopeDown, Stage::ScopeEnter, 2);
            case Step::ScopedShot: return Offer(Action::Click, Stage::ScopedShot, 2);
            case Step::ExitScope: return Offer(Action::ScopeUp, Stage::ScopeExit, 2);
            case Step::BeforeFollowup:
                // These are KOI's explicit key checkpoints. A release during a
                // committed wait does not retract its click; re-pressing before
                // this checkpoint therefore continues the same sequence.
                if (!activationHeld) { Cancel(); continue; }
                Wait(nowUs, kFollowupWaitMs, Step::FollowupShot);
                break;
            case Step::FollowupShot: return Offer(Action::Click, Stage::Followup, shot_);
            case Step::CycleTail:
                if (!activationHeld) { Cancel(); continue; }
                Wait(nowUs, kLoopWaitMs, Step::LoopTop);
                break;
            case Step::LoopTop:
                if (!activationHeld) { Cancel(); continue; }
                step_ = Step::Hip;
                break;
            case Step::Waiting:
                if (nowUs < deadline_) return {Action::None, stage_, shot_, deadline_, false};
                step_ = afterWait_;
                deadline_ = 0;
                break;
            }
        }
    }

    // Call only after the complete action succeeded. A Click is one complete
    // down/up pair; its completion is a submitted-shot proxy, not a claim that
    // the game accepted or hit the shot. Tracking consumes this submission;
    // ammunition remains owned by separately observed HUD evidence.
    void Complete(std::int64_t nowUs) {
        if (pending_ == Action::None) return;
        pending_ = Action::None;
        switch (step_) {
        case Step::Hip: step_ = Step::EnterScope; break;
        case Step::EnterScope: Wait(nowUs, kScopeWaitMs, Step::ScopedShot, Stage::ScopeEnter); break;
        case Step::ScopedShot: Wait(nowUs, kRecoveryWaitMs, Step::ExitScope, Stage::ScopedShot); break;
        case Step::ExitScope: shot_ = 3; step_ = Step::BeforeFollowup; break;
        case Step::FollowupShot:
            if (shot_ < 5) { ++shot_; step_ = Step::BeforeFollowup; }
            else Wait(nowUs, kRecoveryWaitMs, Step::CycleTail);
            break;
        default: break;
        }
    }

private:
    enum class Step : std::uint8_t {
        Idle, Hip, EnterScope, ScopedShot, ExitScope, BeforeFollowup,
        FollowupShot, CycleTail, LoopTop, Waiting
    };
    Decision Offer(Action action, Stage stage, int shot) {
        pending_ = action; stage_ = stage; shot_ = shot;
        return {action, stage_, shot_, 0, false};
    }
    void Wait(std::int64_t nowUs, int durationMs, Step next, Stage stage = Stage::Wait) {
        deadline_ = nowUs + static_cast<std::int64_t>(durationMs) * 1000; afterWait_ = next;
        step_ = Step::Waiting; stage_ = stage;
    }
    Step step_ = Step::Idle, afterWait_ = Step::Idle;
    Action pending_ = Action::None;
    Stage stage_ = Stage::Idle;
    int shot_ = 0;
    std::int64_t deadline_ = 0;
};

} // namespace OW::AsheCombo
