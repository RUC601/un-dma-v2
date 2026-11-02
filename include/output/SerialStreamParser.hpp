#pragma once
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>
namespace V2::Output {
struct SerialEvent {
    enum class Kind { Response, Buttons, Fault } kind;
    std::string body;
    uint8_t buttons = 0;
};
// Length-aware: a button bitmap may itself be NUL, CR or LF.
class SerialStreamParser {
    std::string pending_;
public:
    void Reset() { pending_.clear(); }
    std::vector<SerialEvent> Feed(std::string_view bytes) {
        std::vector<SerialEvent> events;
        pending_.append(bytes);
        if (pending_.size() > 8192) {
            Reset(); events.push_back({SerialEvent::Kind::Fault}); return events;
        }
        for (;;) {
            bool extracted = false;
            for (size_t at = pending_.find("km."); at != std::string::npos;
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
