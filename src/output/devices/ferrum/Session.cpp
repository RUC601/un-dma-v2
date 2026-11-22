#include <Windows.h>
#include "Session.hpp"
#include "Protocol.hpp"
#include <utility>

namespace V2::Output {
namespace {
constexpr uint64_t kButtonPollIntervalMs = 60;
constexpr uint64_t kButtonPollTimeoutMs = 30;
}

void FerrumSession::Reset() {
    KmTextSession::Reset();
    buttonPollPending = false;
    buttonPollNeedsSync = false;
    buttonPollStartedMs = 0;
    nextButtonPollMs = 0;
    buttonPollIndex = 0;
    buttonPollBits = 0;
}

bool FerrumSession::Identity(std::string_view response) const {
    return KmTextCommands::IdentityMatches(response, FerrumProtocol::Identity);
}

bool FerrumSession::OnConnected(SerialChannel& channel) {
    channel.Purge();
    if (!Identity(QueryLocked(channel, "km.version()\r\n", true))) return false;
    if (!ConfirmMousePresence(channel)) return false;
    keyboardReady = true;
    maskReady = true;
    const bool configured = channel.Write("km.buttons(1)\r\n");
    if (configured) MarkIdentityConfirmed();
    return configured;
}

bool FerrumSession::ConfirmMousePresence(SerialChannel& channel) {
    // A virtual COM and a Ferrum identity confirm the App, not a designated mouse.
    const auto response = QueryLocked(channel, "km.has_mouse()\r\n");
    const auto trim = [](std::string_view value) {
        const auto first = value.find_first_not_of(" \t\r\n");
        return first == std::string_view::npos ? std::string_view{} :
            value.substr(first, value.find_last_not_of(" \t\r\n") - first + 1);
    };
    auto value = trim(response);
    constexpr std::string_view echo = "km.has_mouse()";
    if (value.starts_with(echo) && value.size() > echo.size() &&
        std::string_view(" \t\r\n").find(value[echo.size()]) != std::string_view::npos)
        value = trim(value.substr(echo.size()));
    if (value == "1") return true;
    ClearPhysicalInput();
    keyboardReady = false;
    maskReady = false;
    return false;
}

void FerrumSession::RejectButtonPoll() {
    buttonPollPending = false;
    buttonPollNeedsSync = true;
    // A newer callback is already a complete physical snapshot.
    std::lock_guard lock(snapshotMutex);
    if (physical.sampledAtMs < buttonPollStartedMs) physical = {};
}

bool FerrumSession::ReadButtonReplies(SerialChannel& channel) {
    if (!ReadInputLocked(channel)) return false;
    if (buttonPollPending && GetTickCount64() - buttonPollStartedMs >= kButtonPollTimeoutMs)
        RejectButtonPoll();
    while (!responses.empty()) {
        auto body = std::move(responses.front());
        responses.pop_front();
        if (!buttonPollPending) continue;
        const auto reply = FerrumProtocol::ParseButtonReply(body, buttonPollIndex);
        if (reply == FerrumProtocol::ButtonReply::Other) continue;
        if (reply == FerrumProtocol::ButtonReply::Invalid) {
            RejectButtonPoll();
            continue;
        }
        if (reply == FerrumProtocol::ButtonReply::Pressed)
            buttonPollBits |= uint8_t(1u << buttonPollIndex);
        if (++buttonPollIndex == FerrumProtocol::ButtonQueries.size()) {
            buttonPollPending = false;
            std::lock_guard lock(snapshotMutex);
            // Preserve callbacks received during this sweep, including callbacks
            // in the same clock millisecond. The sweep began before its replies.
            if (physical.sampledAtMs < buttonPollStartedMs)
                physical = {true, buttonPollBits, buttonPollStartedMs};
        }
    }
    return true;
}

bool FerrumSession::AfterWrite(SerialChannel& channel) {
    return ReadButtonReplies(channel);
}

bool FerrumSession::Poll(SerialChannel& channel, bool running) {
    if (!ReadButtonReplies(channel)) return false;
    const auto now = GetTickCount64();
    if (!running || buttonPollPending || buttonPollNeedsSync || now < nextButtonPollMs)
        return true;
    {
        std::lock_guard lock(snapshotMutex);
        if (physical.available && now - physical.sampledAtMs < kButtonPollIntervalMs)
            return true;
    }
    std::string commands;
    for (const auto query : FerrumProtocol::ButtonQueries) {
        commands += query;
        commands += "\r\n";
    }
    buttonPollStartedMs = now;
    nextButtonPollMs = now + kButtonPollIntervalMs;
    buttonPollIndex = 0;
    buttonPollBits = 0;
    buttonPollPending = true;
    // Do not wait here: the output worker must remain free to process releases.
    return channel.Write(commands);
}

bool FerrumSession::Heartbeat(SerialChannel& channel, bool) {
    if (!ReadButtonReplies(channel)) return false;
    if (buttonPollPending) RejectButtonPoll();
    // The serial command/reply order makes identity a barrier for late numeric
    // replies. Presence must follow it too: a late pressed-button value of 1
    // must never be mistaken for km.has_mouse(). Check even with output suppressed.
    if (QueryLocked(channel, "km.version()\r\n", true).empty()) return false;
    responses.clear();
    buttonPollNeedsSync = false;
    return ConfirmMousePresence(channel);
}

std::string FerrumSession::EncodeMask(int button, bool enabled) const {
    return FerrumProtocol::Mask(button, enabled);
}

int FerrumSession::FlushInterval(OutputCommandType type) const {
    if (type == OutputCommandType::MouseMove || type == OutputCommandType::MouseAutoMove)
        return OutputQueueConfig::MouseMoveFlushIntervalMs;
    return KmTextCommands::FlushInterval(type);
}

}
