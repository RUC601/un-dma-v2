#include <Windows.h>
#include "Session.hpp"
#include "Protocol.hpp"
#include <chrono>
#include <thread>

namespace V2::Output {
namespace {
constexpr uint64_t kPhysicalSnapshotMaxAgeMs = 100;
constexpr uint64_t kMakcuButtonPollIntervalMs = 60;
constexpr uint64_t kMakcuTextPollTimeoutMs = 30;
constexpr unsigned kPhysicalButtonsFrame = 1;
constexpr unsigned kFirmwareVersionFrame = 2;
constexpr unsigned kFirmwareRejectedFrame = 3;
}

MakcuSession::MakcuSession()
    : KmTextSession({
        {kPhysicalButtonsFrame, std::string("\xDE\xAD\x01\x00\x55", 5), 1},
        {kFirmwareVersionFrame, std::string("\xDE\xAD\x04\x00\x04", 5), 4},
        {kFirmwareRejectedFrame, std::string("\xDE\xAD\x01\x00\x04", 5), 1}}) {}

void MakcuSession::Reset() {
    KmTextSession::Reset();
    makcuPhysicalReply.reset();
    makcuFirmwareVersion.reset();
    makcuBinaryMonitor = false;
    makcuTextNeedsSync = false;
    makcuBinaryPollStartedMs = 0;
    makcuNextButtonPollMs = 0;
    makcuButtonDialect.store(MakcuProtocol::ButtonDialect::Side);
}

bool MakcuSession::Identity(std::string_view response) const {
    return KmTextCommands::IdentityMatches(response, MakcuProtocol::Identity);
}

bool MakcuSession::OnConnected(SerialChannel& channel) {
    channel.Purge();
    if (!Identity(QueryLocked(channel, "km.version()\r\n", true))) return false;
    const auto route = QueryLocked(channel, "km.device()\r\n");
    const auto begin = route.find("R:");
    const auto end = route.find(';', begin);
    maskReady = begin != std::string::npos;
    keyboardReady = begin != std::string::npos &&
        route.substr(begin, end - begin).find('K') != std::string::npos;
    return ConfigureMakcuMonitorLocked(channel, begin != std::string::npos);
}

void MakcuSession::OnBinaryFrame(const SerialEvent& event) {
    if (event.frameId == kPhysicalButtonsFrame) {
        const auto buttons = static_cast<uint8_t>(event.body[0]);
        if (makcuBinaryMonitor) {
            if (makcuBinaryPollStartedMs != 0 && buttons < 32 &&
                GetTickCount64() - makcuBinaryPollStartedMs <= kPhysicalSnapshotMaxAgeMs)
                PublishPhysicalButtons(buttons, makcuBinaryPollStartedMs);
            makcuBinaryPollStartedMs = 0;
        } else {
            makcuPhysicalReply = buttons;
        }
    } else if (event.frameId == kFirmwareVersionFrame) {
        uint32_t version = 0;
        for (size_t i = 0; i < event.body.size(); ++i)
            version |= uint32_t(static_cast<uint8_t>(event.body[i])) << (i * 8);
        makcuFirmwareVersion = version;
    } else if (event.frameId == kFirmwareRejectedFrame) {
        makcuFirmwareVersion = 0;
    }
}

bool MakcuSession::Poll(SerialChannel& channel, bool running) {
    return ReadInputLocked(channel) && (!running || RefreshMakcuButtonsLocked(channel));
}

bool MakcuSession::Heartbeat(SerialChannel& channel, bool suppressed) {
    const bool expiredBinaryPoll = makcuBinaryPollStartedMs != 0 &&
        GetTickCount64() - makcuBinaryPollStartedMs > kPhysicalSnapshotMaxAgeMs;
    if (suppressed && !makcuTextNeedsSync && !expiredBinaryPoll) return true;
    if (QueryLocked(channel, "km.version()\r\n", true).empty()) return false;
    makcuTextNeedsSync = false;
    if (expiredBinaryPoll) makcuBinaryPollStartedMs = 0;
    return true;
}

std::string MakcuSession::EncodeMask(int button, bool enabled) const {
    return MakcuProtocol::Mask(button, enabled);
}

std::string MakcuSession::EncodeButton(int button, bool down) const {
    return MakcuProtocol::Button(button, down, makcuButtonDialect.load());
}

int MakcuSession::ButtonDomain(std::string_view command) const {
    const auto& queries = MakcuProtocol::Queries(makcuButtonDialect.load());
    for (size_t button = 0; button < queries.size(); ++button) {
        const auto prefix = queries[button].substr(0, queries[button].size() - 1);
        if (command.starts_with(prefix)) return int(button);
    }
    return -1;
}

bool MakcuSession::IsButtonRelease(std::string_view command) const {
    const auto dialect = makcuButtonDialect.load();
    for (int button = 0; button < 5; ++button)
        if (command == MakcuProtocol::Button(button, false, dialect)) return true;
    return false;
}

bool MakcuSession::ConfigureMakcuMonitorLocked(SerialChannel& channel, bool v4Route)
{
    using namespace V2::Output::MakcuProtocol;
    makcuButtonDialect.store(ButtonDialect::Side);
    // Older firmware can send bare bytes that are ambiguous in the text stream.
    if (!channel.Write("km.buttons(0)\r\n")) return false;
    if (v4Route) {
        if (!channel.Write(PhysicalButtonsRequest)) return false;
        const auto deadline = GetTickCount64() + kPhysicalSnapshotMaxAgeMs;
        while (GetTickCount64() < deadline) {
            if (!ReadInputLocked(channel, true)) return false;
            if (makcuPhysicalReply) {
                if (*makcuPhysicalReply < 32) {
                    makcuBinaryMonitor = true;
                    PublishPhysicalButtons(*makcuPhysicalReply, GetTickCount64());
                    return channel.Write("km.buttons(1)\r\n");
                }
                break;
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
    }
    if (v4Route) {
        // V4.030/031/033 already support the prefixed stream but lack the binary snapshot.
        if (!channel.Write(FirmwareVersionRequest)) return false;
        const auto deadline = GetTickCount64() + kPhysicalSnapshotMaxAgeMs;
        while (!makcuFirmwareVersion && GetTickCount64() < deadline) {
            if (!ReadInputLocked(channel, true)) return false;
            if (!makcuFirmwareVersion) std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
    }
    if (!ReadInputLocked(channel, true)) return false;
    bool complete = false;
    if (!QueryMakcuTextButtonsLocked(channel, ButtonDialect::Side, complete)) return false;
    if (!complete) {
        // Finish the rejected dialect's replies before sending another untagged sweep.
        if (!Identity(QueryLocked(channel, "km.version()\r\n", true))) return false;
        makcuTextNeedsSync = false;
        if (!QueryMakcuTextButtonsLocked(channel, ButtonDialect::Ms, complete)) return false;
        if (complete) makcuButtonDialect.store(ButtonDialect::Ms);
    }
    return !HasLegacyPrefixedButtonStream(makcuFirmwareVersion.value_or(0)) ||
        channel.Write("km.buttons(1)\r\n");
}

bool MakcuSession::QueryMakcuTextButtonsLocked(SerialChannel& channel,
    MakcuProtocol::ButtonDialect dialect, bool& complete)
{
    using namespace V2::Output::MakcuProtocol;
    complete = false;
    const auto& queries = Queries(dialect);
    std::string commands;
    for (const auto query : queries) {
        commands += query;
        commands += "\r\n";
    }
    responses.clear();
    const auto started = GetTickCount64();
    if (!channel.Write(commands)) return false;
    const auto deadline = GetTickCount64() + kMakcuTextPollTimeoutMs;
    size_t button = 0;
    uint8_t buttons = 0;
    bool rejected = false;
    while (!rejected && GetTickCount64() < deadline) {
        if (!ReadInputLocked(channel, true)) return false;
        while (!responses.empty()) {
            const auto body = std::move(responses.front());
            responses.pop_front();
            if (body.find("ERR") != std::string::npos) {
                rejected = true;
                break;
            }
            const auto state = PhysicalButton(body, button, dialect);
            if (!state) continue;
            if (*state) buttons |= uint8_t(1u << button);
            if (++button == queries.size()) {
                std::lock_guard lock(snapshotMutex);
                if (physical.sampledAtMs < started) physical = {true, buttons, GetTickCount64()};
                complete = true;
                return true;
            }
        }
        if (!rejected) std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    // Drain outstanding text replies through the next identity check before retrying.
    makcuTextNeedsSync = true;
    std::lock_guard lock(snapshotMutex);
    if (physical.sampledAtMs < started) physical = {};
    return true;
}

bool MakcuSession::RefreshMakcuButtonsLocked(SerialChannel& channel)
{
    const auto now = GetTickCount64();
    if (makcuTextNeedsSync || makcuBinaryPollStartedMs != 0 || now < makcuNextButtonPollMs)
        return true;
    {
        std::lock_guard lock(snapshotMutex);
        if (physical.available && now - physical.sampledAtMs < kMakcuButtonPollIntervalMs)
            return true;
    }
    makcuNextButtonPollMs = now + kMakcuButtonPollIntervalMs;
    if (!makcuBinaryMonitor) {
        bool complete = false;
        return QueryMakcuTextButtonsLocked(channel, makcuButtonDialect.load(), complete);
    }
    if (!channel.Write(V2::Output::MakcuProtocol::PhysicalButtonsRequest)) return false;
    makcuBinaryPollStartedMs = now;
    return true;
}


}
