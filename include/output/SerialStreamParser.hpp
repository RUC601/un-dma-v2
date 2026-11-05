#pragma once
#include <algorithm>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>
namespace V2::Output {
struct SerialEvent {
    enum class Kind { Response, Buttons, MakcuPhysicalButtons, MakcuFirmwareVersion, Fault } kind;
    std::string body;
    uint8_t buttons = 0;
    uint32_t firmwareVersion = 0;
};
// Length-aware: a button bitmap may itself be NUL, CR or LF.
class SerialStreamParser {
    std::string pending_;
public:
    void Reset() { pending_.clear(); }
    std::vector<SerialEvent> Feed(std::string_view bytes, bool makcu = false) {
        std::vector<SerialEvent> events;
        pending_.append(bytes);
        if (pending_.size() > 8192) {
            Reset(); events.push_back({SerialEvent::Kind::Fault}); return events;
        }
        for (;;) {
            // MAKCU replies are framed separately from text and button events.
            constexpr std::string_view snapshotHeader{"\xDE\xAD\x01\x00\x55", 5};
            constexpr std::string_view firmwareHeader{"\xDE\xAD\x04\x00\x04", 5};
            constexpr std::string_view firmwareRejectHeader{"\xDE\xAD\x01\x00\x04", 5};
            const auto snapshot = makcu ? pending_.find(snapshotHeader) : std::string::npos;
            const auto firmware = makcu ? pending_.find(firmwareHeader) : std::string::npos;
            const auto firmwareReject = makcu ? pending_.find(firmwareRejectHeader) : std::string::npos;
            const auto binary = (std::min)({snapshot, firmware, firmwareReject});
            bool extracted = false;
            for (size_t at = pending_.find("km."); at < binary;
                 at = pending_.find("km.", at + 3)) {
                if (at + 4 > pending_.size()) break;
                const auto mask = static_cast<uint8_t>(pending_[at + 3]);
                if (mask >= 32) continue;
                if (at + 6 > pending_.size()) return events;
                if (pending_.compare(at + 4, 2, "\r\n") != 0) {
                    Reset(); events.push_back({SerialEvent::Kind::Fault}); return events;
                }
                events.push_back({SerialEvent::Kind::Buttons, {}, mask});
                pending_.erase(at, 6); extracted = true; break;
            }
            if (extracted) continue;
            if (binary != std::string::npos) {
                const size_t payloadSize = binary == firmware ? 4 : 1;
                const size_t payload = binary + 5;
                if (payload + payloadSize > pending_.size()) break;
                if (binary == snapshot) {
                    events.push_back({SerialEvent::Kind::MakcuPhysicalButtons, {},
                        static_cast<uint8_t>(pending_[payload])});
                } else {
                    uint32_t version = 0;
                    for (size_t i = 0; binary == firmware && i < payloadSize; ++i)
                        version |= uint32_t(static_cast<uint8_t>(pending_[payload + i])) << (i * 8);
                    events.push_back({SerialEvent::Kind::MakcuFirmwareVersion, {}, 0, version});
                }
                pending_.erase(binary, 5 + payloadSize);
                continue;
            }
            const auto prompt = pending_.find(">>> ");
            if (prompt == std::string::npos) break;
            auto body = pending_.substr(0, prompt);
            pending_.erase(0, prompt + 4);
            if (body.find_first_not_of("\r\n ") != std::string::npos)
                events.push_back({SerialEvent::Kind::Response, std::move(body)});
        }
        return events;
    }
};
}
