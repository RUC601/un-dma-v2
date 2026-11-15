#pragma once
#include "output/protocols/KmTextSession.hpp"
#include <cstddef>
#include <cstdint>

namespace V2::Output {

class FerrumSession final : public KmTextSession {
    bool buttonPollPending = false;
    bool buttonPollNeedsSync = false;
    uint64_t buttonPollStartedMs = 0;
    uint64_t nextButtonPollMs = 0;
    size_t buttonPollIndex = 0;
    uint8_t buttonPollBits = 0;

    bool Identity(std::string_view response) const override;
    bool DrainTextPollForIdentity() const override { return buttonPollNeedsSync; }
    bool ConfirmMousePresence(SerialChannel& channel);
    bool ReadButtonReplies(SerialChannel& channel);
    void RejectButtonPoll();
public:
    friend class ::OutputProtocolSelfTestAccess;
    void Reset() override;
    bool OnConnected(SerialChannel& channel) override;
    bool AfterWrite(SerialChannel& channel) override;
    bool Poll(SerialChannel& channel, bool running) override;
    bool Heartbeat(SerialChannel& channel, bool suppressed) override;
    std::string EncodeMask(int button, bool enabled) const override;
    int FlushInterval(OutputCommandType type) const override;
};

}
