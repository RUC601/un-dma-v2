#include <Windows.h>
#include "Session.hpp"
#include "Protocol.hpp"

namespace V2::Output {

bool FerrumSession::Identity(std::string_view response) const {
    return KmTextCommands::IdentityMatches(response, FerrumProtocol::Identity);
}

bool FerrumSession::OnConnected(SerialChannel& channel) {
    channel.Purge();
    if (!Identity(QueryLocked(channel, "km.version()\r\n", true))) return false;
    keyboardReady = true;
    maskReady = true;
    return channel.Write("km.buttons(1)\r\n");
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
