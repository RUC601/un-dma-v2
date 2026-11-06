#pragma once

#include <Windows.h>
#include <cstdint>
#include <limits>
#include "Game/Cn155284.hpp"

namespace OW::ProductImageIdentity {

// Accepted CN 155284N image headers; this identifies the profile, not a file hash.
inline constexpr uint32_t kTimestamp = Cn155284::timestamp;
inline constexpr uint32_t kImageSize = Cn155284::imageSize;

enum class Result { Accepted, Unreadable, InvalidHeaders, UnsupportedVersion };

template <typename Read>
Result ReadHeaders(uint64_t base, Read&& read, uint32_t& timestamp, uint32_t& imageSize)
{
    timestamp = imageSize = 0;
    IMAGE_DOS_HEADER dos{};
    if (!base || !read(base, &dos, sizeof(dos)))
        return Result::Unreadable;
    if (dos.e_magic != IMAGE_DOS_SIGNATURE || dos.e_lfanew <= 0 || dos.e_lfanew > 0x100000 ||
        base > (std::numeric_limits<uint64_t>::max)() - static_cast<uint64_t>(dos.e_lfanew) - sizeof(IMAGE_NT_HEADERS64))
        return Result::InvalidHeaders;
    IMAGE_NT_HEADERS64 nt{};
    if (!read(base + static_cast<uint64_t>(dos.e_lfanew), &nt, sizeof(nt)))
        return Result::Unreadable;
    if (nt.Signature != IMAGE_NT_SIGNATURE || nt.FileHeader.Machine != IMAGE_FILE_MACHINE_AMD64 ||
        nt.FileHeader.SizeOfOptionalHeader < sizeof(IMAGE_OPTIONAL_HEADER64) ||
        nt.OptionalHeader.Magic != IMAGE_NT_OPTIONAL_HDR64_MAGIC ||
        nt.OptionalHeader.SizeOfImage < static_cast<uint32_t>(dos.e_lfanew) + sizeof(nt))
        return Result::InvalidHeaders;
    timestamp = nt.FileHeader.TimeDateStamp;
    imageSize = nt.OptionalHeader.SizeOfImage;
    return Result::Accepted;
}

template <typename Read>
Result Check(uint64_t base, Read&& read, uint32_t& timestamp, uint32_t& imageSize)
{
    const auto result = ReadHeaders(base, read, timestamp, imageSize);
    if (result != Result::Accepted)
        return result;
    return timestamp == kTimestamp && imageSize == kImageSize
        ? Result::Accepted : Result::UnsupportedVersion;
}

} // namespace OW::ProductImageIdentity
