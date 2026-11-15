#pragma once

#include "output/OutputTypes.hpp"

// Queue payload owned by one serial transport.
struct OutputQueuedSerialCommand {
    std::string command;
    std::vector<unsigned char> keyboardKeys;
    OutputCommandType type = OutputCommandType::Serial;
    OutputIntent outputIntent = OutputIntent::Normal;
    OutputCommandPriority priority = OutputCommandPriority::Normal;
    std::shared_ptr<OutputCommandCompletion> completion;
    std::chrono::steady_clock::time_point enqueuedAt{};
};
