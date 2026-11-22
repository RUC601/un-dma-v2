#pragma once

#include "output/OutputTypes.hpp"
#include <string>
#include <string_view>

namespace V2::Output {

// Called only while the owning transport holds its serial I/O mutex.
class SerialChannel {
public:
    virtual ~SerialChannel() = default;
    virtual bool Write(std::string_view bytes) = 0;
    virtual bool Read(std::string& bytes) = 0;
    virtual void Purge() = 0;
};

// Each device owns one protocol instance and one transport. The transport
// schedules commands and manages the COM handle without inspecting wire bytes.
class SerialDeviceProtocol {
public:
    virtual ~SerialDeviceProtocol() = default;
    virtual std::string ResolvePort(const std::string& requested) const { return requested; }
    virtual void Reset() = 0;
    virtual bool OnConnected(SerialChannel& channel) = 0;
    virtual bool AfterWrite(SerialChannel& channel) = 0;
    virtual bool Poll(SerialChannel& channel, bool running) = 0;
    virtual bool Heartbeat(SerialChannel& channel, bool suppressed) = 0;
    virtual bool SupportsKeyboard() const = 0;
    virtual bool SupportsMask() const = 0;
    virtual bool SupportsTimedMove() const = 0;
    virtual bool IdentityConfirmed() const { return false; }
    virtual const char* IdentityTag() const { return "unconfirmed"; }
    virtual const char* CapabilitySource() const { return "unknown"; }
    virtual PhysicalInputSnapshot PhysicalInput() const = 0;
    virtual void ClearPhysicalInput() = 0;
    virtual bool EncodeMove(int x, int y, int duration, std::string& command) const = 0;
    virtual std::string EncodeButton(int button, bool down) const = 0;
    virtual std::string EncodeKey(unsigned usage, bool down) const = 0;
    virtual std::string EncodeMask(int button, bool enabled) const = 0;
    virtual int ButtonDomain(std::string_view command) const = 0;
    virtual bool IsButtonRelease(std::string_view command) const = 0;
    virtual int FlushInterval(OutputCommandType type) const = 0;
};

}
