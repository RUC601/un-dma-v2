#pragma once
#include <algorithm>
#include <cctype>
#include <cstdint>
#include <limits>
#include <span>
#include <string_view>
#include <vector>

namespace DmaSignature {
inline std::vector<int> Parse(std::string_view text) {
    std::vector<int> bytes;
    auto hex = [](char c) -> int {
        if (c >= '0' && c <= '9') return c - '0';
        if (c >= 'a' && c <= 'f') return c - 'a' + 10;
        if (c >= 'A' && c <= 'F') return c - 'A' + 10;
        return -1;
    };
    while (!text.empty()) {
        while (!text.empty() && std::isspace(static_cast<unsigned char>(text.front()))) text.remove_prefix(1);
        if (text.empty()) break;
        size_t length = 0;
        while (length < text.size() && !std::isspace(static_cast<unsigned char>(text[length]))) ++length;
        const auto token = text.substr(0, length);
        if (token == "?" || token == "??") bytes.push_back(-1);
        else if (length == 2 && hex(token[0]) >= 0 && hex(token[1]) >= 0)
            bytes.push_back(hex(token[0]) * 16 + hex(token[1]));
        else return {};
        text.remove_prefix(length);
    }
    return bytes;
}

// read returns the exact contiguous prefix obtained, or zero on failure.
template<class Read, class Cancel>
uint64_t Scan(std::string_view signature, uint64_t begin, uint64_t end, Read read, Cancel cancelled) {
    const auto pattern = Parse(signature);
    if (begin >= end || pattern.empty() || pattern.size() > end - begin ||
        pattern.size() > std::numeric_limits<uint32_t>::max() / 2) return 0;
    const size_t window = std::max(size_t(64 * 1024), pattern.size() * 2);
    std::vector<uint8_t> data(static_cast<size_t>(std::min<uint64_t>(window, end - begin)));
    const size_t step = data.size() - pattern.size() + 1;
    for (uint64_t at = begin; at < end && !cancelled();) {
        const size_t requested = static_cast<size_t>(std::min<uint64_t>(data.size(), end - at));
        const size_t actual = std::min(requested, read(at, data.data(), requested));
        if (actual >= pattern.size()) {
            for (size_t i = 0; i <= actual - pattern.size(); ++i) {
                size_t j = 0;
                for (; j < pattern.size(); ++j)
                    if (pattern[j] >= 0 && data[i + j] != pattern[j]) break;
                if (j == pattern.size()) return at + i;
            }
        }
        if (end - at <= step) break;
        at += step;
    }
    return 0;
}
}
