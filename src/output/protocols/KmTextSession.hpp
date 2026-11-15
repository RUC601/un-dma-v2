#pragma once

#include "output/transport/serial/SerialDeviceProtocol.hpp"
#include "output/protocols/KmTextCommands.hpp"
#include "output/protocols/KmTextStreamParser.hpp"
#include <atomic>
#include <deque>
#include <mutex>

class OutputProtocolSelfTestAccess;

namespace V2::Output {

// Shared text HID dialect. Capability discovery and binary extensions belong
// to the device that owns this session.
class KmTextSession : public SerialDeviceProtocol {
protected:
    KmTextStreamParser parser;
    std::deque<std::string> responses;
    std::atomic<bool> keyboardReady{false};
    std::atomic<bool> maskReady{false};
    mutable std::mutex snapshotMutex;
    PhysicalInputSnapshot physical;

    explicit KmTextSession(std::vector<SerialBinaryFrame> frames = {})
        : parser(std::move(frames)) {}
    virtual bool Identity(std::string_view response) const = 0;
    virtual void OnBinaryFrame(const SerialEvent& event) {}
    virtual bool DrainTextPollForIdentity() const { return false; }
    bool ReadInputLocked(SerialChannel& channel, bool allowDeviceError = false);
    std::string QueryLocked(SerialChannel& channel, const std::string& command,
        bool expectIdentity = false);
    void PublishPhysicalButtons(uint8_t buttons, uint64_t sampledAtMs);

public:
    friend class ::OutputProtocolSelfTestAccess;
    void Reset() override;
    bool AfterWrite(SerialChannel& channel) override;
    bool Poll(SerialChannel& channel, bool running) override;
    bool Heartbeat(SerialChannel& channel, bool suppressed) override;
    bool SupportsKeyboard() const override { return keyboardReady.load(); }
    bool SupportsMask() const override { return maskReady.load(); }
    bool SupportsTimedMove() const override { return false; }
    PhysicalInputSnapshot PhysicalInput() const override;
    void ClearPhysicalInput() override;
    bool EncodeMove(int x, int y, int duration, std::string& command) const override;
    std::string EncodeButton(int button, bool down) const override;
    std::string EncodeKey(unsigned usage, bool down) const override;
    int ButtonDomain(std::string_view command) const override;
    bool IsButtonRelease(std::string_view command) const override;
    int FlushInterval(OutputCommandType type) const override;
};

}
