#pragma once
#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>

namespace OW::SkillRead {
// Pure 1106 RVA 0x136FF0: shared hash layout, reverse 64-entry chunks.
// A complete miss is different from an unreadable or changing table.
enum class Status { Found, Missing, Unreadable, Invalid };
struct Result { uint64_t address = 0; Status status = Status::Invalid; };
struct Header { uint64_t entries; uint32_t count; uint32_t reserved; };
struct Entry { uint32_t id; uint32_t reserved; uint64_t address; };
static_assert(sizeof(Header) == 16 && sizeof(Entry) == 16);
inline bool UserRange(uint64_t address, size_t bytes) {
    constexpr uint64_t last = 0x7FFFFFFFFFFEull;
    return bytes && address > 0x10000 && address <= last && bytes - 1 <= last - address;
}
template<class Reader>
Result FindInBucket(uint64_t bucket, uint16_t id, Reader&& read) {
    if (!id || !UserRange(bucket, sizeof(Header))) return {};
    Header header{};
    if (!read(bucket, &header, sizeof(header))) return {0, Status::Unreadable};
    if (header.count > 4096) return {};
    if (!header.count) return {0, Status::Missing};
    if (!UserRange(header.entries, size_t(header.count) * sizeof(Entry))) return {};
    auto stable = [&] {
        Header current{};
        return read(bucket, &current, sizeof(current)) &&
            current.entries == header.entries && current.count == header.count;
    };
    bool incomplete = false;
    std::array<Entry, 64> entries{};
    for (size_t remaining = header.count; remaining;) {
        const size_t count = (std::min)(remaining, entries.size());
        remaining -= count;
        if (!read(header.entries + remaining * sizeof(Entry), entries.data(), count * sizeof(Entry))) {
            incomplete = true;
            continue;
        }
        for (size_t i = count; i--;) {
            if (entries[i].id != id) continue;
            if (!stable()) return {0, Status::Unreadable};
            return UserRange(entries[i].address, 1)
                ? Result{entries[i].address, Status::Found} : Result{};
        }
    }
    return {0, incomplete || !stable() ? Status::Unreadable : Status::Missing};
}
}
