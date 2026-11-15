#include <Windows.h>
#include "KmTextSession.hpp"
#include <chrono>
#include <thread>

namespace V2::Output {

void KmTextSession::Reset() {
    parser.Reset();
    responses.clear();
    keyboardReady = false;
    maskReady = false;
    ClearPhysicalInput();
}

bool KmTextSession::ReadInputLocked(SerialChannel& channel, bool allowDeviceError) {
    std::string bytes;
    if (!channel.Read(bytes)) return false;
    for (auto& event : parser.Feed(bytes)) {
        if (event.kind == SerialEvent::Kind::Fault) return false;
        if (event.kind == SerialEvent::Kind::Buttons) {
            PublishPhysicalButtons(event.buttons, GetTickCount64());
        } else if (event.kind == SerialEvent::Kind::BinaryFrame) {
            OnBinaryFrame(event);
        } else {
            if (!allowDeviceError && event.body.find("ERR") != std::string::npos)
                return false;
            if (responses.size() >= 32) responses.pop_front();
            responses.push_back(std::move(event.body));
        }
    }
    return true;
}

std::string KmTextSession::QueryLocked(SerialChannel& channel,
    const std::string& command, bool expectIdentity) {
    const bool drainTextPoll = expectIdentity && DrainTextPollForIdentity();
    if (!ReadInputLocked(channel, drainTextPoll)) return {};
    responses.clear();
    if (!channel.Write(command)) return {};
    const auto deadline = GetTickCount64() + 500;
    while (GetTickCount64() < deadline) {
        if (!ReadInputLocked(channel, drainTextPoll)) return {};
        while (!responses.empty()) {
            auto body = std::move(responses.front());
            responses.pop_front();
            auto echo = command;
            while (!echo.empty() && (echo.back() == '\r' || echo.back() == '\n'))
                echo.pop_back();
            if (body == echo || body == echo + "\r\n") continue;
            if (expectIdentity && !Identity(body)) continue;
            return body;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    return {};
}

void KmTextSession::PublishPhysicalButtons(uint8_t buttons, uint64_t sampledAtMs) {
    std::lock_guard lock(snapshotMutex);
    physical = {true, buttons, sampledAtMs};
}

PhysicalInputSnapshot KmTextSession::PhysicalInput() const {
    std::lock_guard lock(snapshotMutex);
    return physical;
}

void KmTextSession::ClearPhysicalInput() {
    std::lock_guard lock(snapshotMutex);
    physical = {};
}

bool KmTextSession::AfterWrite(SerialChannel& channel) {
    return ReadInputLocked(channel);
}

bool KmTextSession::Poll(SerialChannel& channel, bool) {
    return ReadInputLocked(channel);
}

bool KmTextSession::Heartbeat(SerialChannel& channel, bool suppressed) {
    return suppressed || !QueryLocked(channel, "km.version()\r\n", true).empty();
}

bool KmTextSession::EncodeMove(int x, int y, int duration, std::string& command) const {
    if (duration > 0 || x < -32768 || x > 32767 || y < -32768 || y > 32767)
        return false;
    command = KmTextCommands::Move(x, y);
    return true;
}

std::string KmTextSession::EncodeButton(int button, bool down) const {
    return KmTextCommands::Button(button, down);
}

std::string KmTextSession::EncodeKey(unsigned usage, bool down) const {
    return KmTextCommands::Key(usage, down);
}

int KmTextSession::ButtonDomain(std::string_view command) const {
    return KmTextCommands::ButtonDomain(command);
}

bool KmTextSession::IsButtonRelease(std::string_view command) const {
    return KmTextCommands::IsButtonRelease(command);
}

int KmTextSession::FlushInterval(OutputCommandType type) const {
    return KmTextCommands::FlushInterval(type);
}

}
