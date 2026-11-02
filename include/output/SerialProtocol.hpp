#pragma once
#include "output/MakcuProtocol.hpp"
#include "output/FerrumProtocol.hpp"
#include <string_view>
namespace V2::Output {
enum class SerialProtocol { KmBox, Makcu, Ferrum };
inline bool IdentityMatches(SerialProtocol protocol, std::string_view response) {
    if (protocol == SerialProtocol::KmBox) return true;
    while (!response.empty()) {
        const auto e = response.find_first_of("\r\n");
        auto line = response.substr(0,e);
        while (!line.empty() && line.front() == ' ') line.remove_prefix(1);
        while (!line.empty() && line.back() == ' ') line.remove_suffix(1);
        if (protocol == SerialProtocol::Makcu ? MakcuProtocol::Identity(line) : FerrumProtocol::Identity(line)) return true;
        if (e == std::string_view::npos) break;
        response.remove_prefix(e + 1);
    }
    return false;
}
}
