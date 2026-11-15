#pragma once
#include "output/protocols/KmTextSession.hpp"
#include <optional>

namespace V2::Output {

class MakcuSession final : public KmTextSession {
    std::optional<uint8_t> makcuPhysicalReply;
    std::optional<uint32_t> makcuFirmwareVersion;
    bool makcuBinaryMonitor = false;
    bool makcuTextNeedsSync = false;
    uint64_t makcuBinaryPollStartedMs = 0;
    uint64_t makcuNextButtonPollMs = 0;

    bool Identity(std::string_view response) const override;
    void OnBinaryFrame(const SerialEvent& event) override;
    bool DrainTextPollForIdentity() const override { return makcuTextNeedsSync; }
    bool ConfigureMakcuMonitorLocked(SerialChannel& channel, bool v4Route);
    bool QueryMakcuTextButtonsLocked(SerialChannel& channel);
    bool RefreshMakcuButtonsLocked(SerialChannel& channel);
public:
    friend class ::OutputProtocolSelfTestAccess;
    MakcuSession();
    void Reset() override;
    bool OnConnected(SerialChannel& channel) override;
    bool Poll(SerialChannel& channel, bool running) override;
    bool Heartbeat(SerialChannel& channel, bool suppressed) override;
    std::string EncodeMask(int button, bool enabled) const override;
};

}
