#pragma once

#include <array>
#include <bit>
#include <cstdint>
#include <cstring>

namespace OW::RegionalLayout {

struct RegistryHeader {
    uint32_t capacity = 0;
    uint32_t count = 0;
    uint64_t root = 0;
};

template<class Read>
RegistryHeader ReadRegistry(uint64_t rootAddress, Read&& read)
{
    std::array<uint8_t, 28> bytes{};
    if (!read(rootAddress - 20, bytes.data(), bytes.size()))
        return {};
    RegistryHeader header;
    std::memcpy(&header.capacity, bytes.data(), 4);
    std::memcpy(&header.count, bytes.data() + 4, 4);
    std::memcpy(&header.root, bytes.data() + 20, 8);
    if (header.capacity < 64 || header.capacity > 32768 || header.count > header.capacity ||
        header.root <= 0x10000 || header.root >= 0x800000000000ULL || (header.root & 7))
        return {};
    return header;
}

namespace Bz154905 {
constexpr uint64_t DecodeComponent(uint64_t value, uint64_t key, uint8_t byteKey)
{
    value = std::rotr(value, 5) + 0x7F6B721FA7C1DBE3ULL;
    value = std::rotr(value, 41) + 0x3977D6FEAB9E0493ULL;
    value ^= key ^ byteKey ^ 0xA37495136B569EC0ULL;
    return std::rotr(value, 51) + 0x7DBF7AE0B4BBA3D2ULL;
}

constexpr uint64_t DecodeGlobalAdmin(uint64_t value)
{
    value = std::rotr(value, 12) + 0x57B45C981183E97CULL;
    value ^= 0x285B75A380F5D53BULL;
    return std::rotr(value, 49) - 0x6C06B55ACA9F51EFULL;
}

constexpr uint64_t DecodeGameAdmin(uint64_t value)
{
    value -= 0x3FDD7BF9272B8763ULL;
    value ^= 0x810B59A9EEEF1F0FULL;
    value = std::rotr(value, 44) - 0x5C98FDE234FD68BBULL;
    return std::rotr(value, 30);
}
}
}
