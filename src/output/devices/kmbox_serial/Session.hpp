#pragma once
#include "output/transport/serial/SerialDeviceProtocol.hpp"

namespace V2::Output {

class KmBoxSerialSession final : public SerialDeviceProtocol {
public:
    std::string ResolvePort(const std::string& requested) const override;
    void Reset() override {}
    bool OnConnected(SerialChannel&) override { return true; }
    bool AfterWrite(SerialChannel&) override { return true; }
    bool Poll(SerialChannel&, bool) override { return true; }
    bool Heartbeat(SerialChannel&, bool) override { return true; }
    bool SupportsKeyboard() const override { return false; }
    bool SupportsMask() const override { return false; }
    // KMBox B+ interprets km.move(x,y,third) as a segment count, while the
    // runtime API's third argument is milliseconds. Treat it as unsupported
    // so timed requests safely fall back to a plain relative move.
    bool SupportsTimedMove() const override { return false; }
    PhysicalInputSnapshot PhysicalInput() const override { return {}; }
    void ClearPhysicalInput() override {}
    bool EncodeMove(int x, int y, int duration, std::string& command) const override;
    std::string EncodeButton(int button, bool down) const override;
    std::string EncodeKey(unsigned, bool) const override { return {}; }
    std::string EncodeMask(int, bool) const override { return {}; }
    int ButtonDomain(std::string_view command) const override;
    bool IsButtonRelease(std::string_view command) const override;
    int FlushInterval(OutputCommandType type) const override;
};

}
