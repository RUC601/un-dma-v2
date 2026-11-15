#pragma once

#include <algorithm>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace V2::Output {

struct SerialEvent {
    enum class Kind { Response, Buttons, BinaryFrame, Fault } kind;
    std::string body;
    uint8_t buttons = 0;
    unsigned frameId = 0;
};

struct SerialBinaryFrame {
    unsigned id;
    std::string header;
    size_t payloadSize;
};

// Shared prompt/button framing. Devices register their own binary envelopes.
// A button bitmap may itself contain NUL, CR or LF.
class KmTextStreamParser {
    std::string pending_;
    std::vector<SerialBinaryFrame> binaryFrames_;
public:
    explicit KmTextStreamParser(std::vector<SerialBinaryFrame> binaryFrames = {})
        : binaryFrames_(std::move(binaryFrames)) {}

    void Reset() { pending_.clear(); }

    std::vector<SerialEvent> Feed(std::string_view bytes) {
        std::vector<SerialEvent> events;
        pending_.append(bytes);
        if (pending_.size() > 8192) {
            Reset();
            events.push_back({SerialEvent::Kind::Fault});
            return events;
        }
        for (;;) {
            size_t binary = std::string::npos;
            const SerialBinaryFrame* frame = nullptr;
            for (const auto& candidate : binaryFrames_) {
                const auto at = pending_.find(candidate.header);
                if (at < binary) {
                    binary = at;
                    frame = &candidate;
                }
            }
            bool extracted = false;
            for (size_t at = pending_.find("km."); at < binary;
                 at = pending_.find("km.", at + 3)) {
                if (at + 4 > pending_.size()) break;
                const auto mask = static_cast<uint8_t>(pending_[at + 3]);
                if (mask >= 32) continue;
                if (at + 6 > pending_.size()) return events;
                if (pending_.compare(at + 4, 2, "\r\n") != 0) {
                    Reset();
                    events.push_back({SerialEvent::Kind::Fault});
                    return events;
                }
                events.push_back({SerialEvent::Kind::Buttons, {}, mask});
                pending_.erase(at, 6);
                extracted = true;
                break;
            }
            if (extracted) continue;
            if (frame) {
                const auto payload = binary + frame->header.size();
                if (payload + frame->payloadSize > pending_.size()) break;
                events.push_back({SerialEvent::Kind::BinaryFrame,
                    pending_.substr(payload, frame->payloadSize), 0, frame->id});
                pending_.erase(binary, frame->header.size() + frame->payloadSize);
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
