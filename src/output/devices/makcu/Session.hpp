#pragma once
#include "output/protocols/KmTextSession.hpp"
#include "Protocol.hpp"
#include <optional>

namespace V2::Output {

class MakcuSession final : public KmTextSession {
    std::optional<uint8_t> makcuPhysicalReply;
    std::optional<uint32_t> makcuFirmwareVersion;
    bool makcuBinaryMonitor = false;
    bool makcuTextNeedsSync = false;
    uint64_t makcuBinaryPollStartedMs = 0;
    uint64_t makcuNextButtonPollMs = 0;
    std::atomic<MakcuProtocol::ButtonDialect> makcuButtonDialect{MakcuProtocol::ButtonDialect::Side};

    bool Identity(std::string_view response) const override;
    void OnBinaryFrame(const SerialEvent& event) override;
    bool DrainTextPollForIdentity() const override { return makcuTextNeedsSync; }
    bool ConfigureMakcuMonitorLocked(SerialChannel& channel, bool v4Route);
    bool QueryMakcuTextButtonsLocked(SerialChannel& channel,
        MakcuProtocol::ButtonDialect dialect, bool& complete);
    bool RefreshMakcuButtonsLocked(SerialChannel& channel);
public:
    friend class ::OutputProtocolSelfTestAccess;
    MakcuSession();
    void Reset() override;
    bool OnConnected(SerialChannel& channel) override;
    bool Poll(SerialChannel& channel, bool running) override;
    bool Heartbeat(SerialChannel& channel, bool suppressed) override;
    std::string EncodeButton(int button, bool down) const override;
    int ButtonDomain(std::string_view command) const override;
    bool IsButtonRelease(std::string_view command) const override;
    std::string EncodeMask(int button, bool enabled) const override;
};

}
