#pragma once

#include <bit>
#include <cstdint>

namespace OW::RegionalLayout {
namespace World155284 {
// Native D0/D4/D8 map cores: C70716/C71016/C71916. Parent also checked
// independently at 544E6F; the getter folds its final add with the +0x28 field.
// Evidence: 03_DOWNP/202611060630/context-map-regions-20261116/static/.
constexpr uint64_t DecodeMapParent(uint64_t raw)
{
    return (std::rotr(raw + 0x1FAE62CADBFE9389ULL, 21) ^ 0x80138B3C582F8286ULL) +
           0x667BE2C557C22212ULL;
}
constexpr uint64_t DecodeMapOwner(uint64_t raw)
{
    return ((raw + 0x385FD7DE851C1CB4ULL) ^ 0xAEC84AE8B395B4ABULL) +
           0x615B0D8D079DE55BULL;
}
constexpr uint64_t DecodeMapBuckets(uint64_t raw)
{
    return std::rotr((std::rotr(raw, 44) ^ 0xFA03FD84B50A0BBDULL) +
                     0x2440492FD3A36FA1ULL, 42);
}

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
