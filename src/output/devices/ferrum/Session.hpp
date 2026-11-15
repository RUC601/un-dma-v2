#pragma once
#include "output/protocols/KmTextSession.hpp"

namespace V2::Output {

class FerrumSession final : public KmTextSession {
    bool Identity(std::string_view response) const override;
public:
    friend class ::OutputProtocolSelfTestAccess;
    bool OnConnected(SerialChannel& channel) override;
    std::string EncodeMask(int button, bool enabled) const override;
    int FlushInterval(OutputCommandType type) const override;
};

}
