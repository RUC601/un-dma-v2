#pragma once
#include <bit>
#include <cstdint>

// CN 155284N RVAs from 03_DOWNP/202611060630. The transforms also occur in 155284KB.
namespace OW::Cn155284 {
inline constexpr uint32_t timestamp = 0x6AEBE914, imageSize = 0x5A98000;
inline constexpr uint64_t keyByteA = 0x4A9D525, keyByteB = 0x4A9DA31;
inline constexpr uint64_t entityRoot = 0x4C71748, entityState = 0x4C71730;
inline constexpr uint64_t entityLock = 0x53BB920;
// 0x800 slots is the initial allocation, not a permanent scan bound.
inline constexpr uint64_t entityInitialBytes = 0x8000;
inline constexpr uint64_t viewRoot = 0x4C08E78, globalAdmin = 0x4DD70A0;
inline constexpr uint64_t mouseScaleX = 0x4AA456C, mouseScaleY = 0x4AA4584;
inline constexpr uint64_t pointerXor = 0xD9B0DF0D8532B5A0ULL;
inline constexpr uint64_t pointerAdd = 0x488A1F0DC13B6040ULL;

constexpr uint64_t Substitute(uint64_t value) {
    constexpr uint8_t lut[]{6,12,10,3,14,0,15,9,7,4,8,5,13,11,1,2};
    uint64_t result = 0;
    for (unsigned bit = 0; bit < 64; bit += 4)
        result |= uint64_t(lut[(value >> bit) & 15]) << bit;
    return result;
}

// 155284N scalar starts at 0x547C96. The known-answer samples below are from 154905N.
constexpr uint64_t Component(uint64_t value, uint8_t a, uint8_t b) {
    value = std::rotr((value + 0x0D030666FEB038CEULL) ^ a, 40) ^ 0x357B5A44DBADDD24ULL;
    value = std::rotr(Substitute(value) + 0x33696B953606C040ULL, 26) ^ b ^ 0x2CEFECC4CAD54C5EULL;
    value = std::rotr(Substitute(value) - 0x3BCDDB630BBCF625ULL, 25);
    value = (value + 0x3DACABA536F3D07DULL) ^ 0xBCBEF01775642D15ULL;
    return std::rotr(value, 11) ^ 0x8EC103A196A8E59EULL;
}
constexpr uint64_t Pointer(uint64_t value) { return (value ^ pointerXor) + pointerAdd; }
constexpr uint64_t Parent(uint64_t value) {
    return std::rotr((value ^ 0x340A56FA93DA6F7BULL) - 0x1CF6B5C337C2DCC1ULL, 46);
}
constexpr uint64_t Singleton(uint64_t value) {
    return (std::rotr(std::rotr(value, 1) + 0xE4A74035D2D1853DULL, 36) ^
            0x9ACE05FA7D7CF0CBULL) + 0x57A241D065E55B43ULL;
}
constexpr uint64_t Visibility(uint64_t value) {
    return std::rotr((std::rotr(value, 58) ^ 0x848ABAEDB38F2271ULL) + 0x7ED149BB3953EA7BULL, 42);
}
constexpr bool HeroId(uint64_t value) {
    return (value & 0xFFFFFFFFFFFF0000ULL) == 0x02E0000000000000ULL && (value & 0xFFFF) != 0;
}
static_assert(Visibility(0xD0169002A5D48CDFULL) == 1);
static_assert(Visibility(0xD0169032A5D48CDFULL) == 0);
static_assert(Component(0x026F5D1F90B23D785ULL, 253, 57) == 0x26781BADA40ULL);
static_assert(Component(0xFDF72CF90B2351B7ULL, 253, 57) == 0x23FFB1A46C0ULL);
static_assert(HeroId(0x02E000000000012FULL));
static_assert(!HeroId(0x02E00000437F0000ULL));
}
