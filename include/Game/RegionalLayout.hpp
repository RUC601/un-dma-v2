#pragma once

#include <bit>
#include <cstdint>

namespace OW::RegionalLayout {
namespace World155284 {
constexpr uint64_t DecodeComponent(uint64_t value, uint64_t key, uint8_t byteKey)
{
    value = (std::rotr(value, 33) ^ key) + 0x061365BA92613D6EULL;
    return (std::rotr(value ^ byteKey, 3) + 0x4ACD89886EC2D109ULL) ^ 0xDE342D5317F56B5DULL;
}

constexpr uint64_t DecodeGlobalAdmin(uint64_t value)
{
    value = (std::rotr(value, 3) ^ 0x6B8F56BF98CC7D09ULL) + 0xCB85DE2B94C4D6FDULL;
    return std::rotr(value, 63) + 0x130305262FA1386AULL;
}

constexpr uint64_t DecodeGameAdmin(uint64_t value)
{
    value = std::rotr(value ^ 0xBC4ECC99AD9EC98DULL, 57) + 0x5ED15F31DC8F9139ULL;
    return std::rotr(value, 48) + 0x7AB10D2DB096D11AULL;
}
}
}
