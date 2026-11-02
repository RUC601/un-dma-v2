#pragma once
#include <algorithm>
#include <cstdint>
#include <cstring>
#include <array>

namespace ProcessLifetime {
struct Layout {
    uint32_t pid = 0, created = 0, exited = 0;
    bool Valid() const {
        return pid >= 8 && created >= 8 && exited >= 8 &&
            pid != created && pid != exited && created != exited &&
            pid % 8 == 0 && created % 8 == 0 && exited % 8 == 0 &&
            (std::max)({pid, created, exited}) <= 0x2000 - 8;
    }
};
struct Sample {
    uint64_t pid = 0, created = 0, exited = 0;
    bool Alive(uint64_t expectedPid, uint64_t expectedCreated) const {
        return expectedPid != 0 && expectedCreated != 0 &&
            pid == expectedPid && created == expectedCreated && exited == 0;
    }
};

// One bounded, uncached kernel read; never turn partial/zero-padded data into
// process-health evidence. The caller resolves this layout for the current OS.
template<class Read>
bool ReadSample(const Layout& layout, uint64_t address, Read&& read, Sample& sample) {
    sample = {};
    if (!layout.Valid() || address < 0xffff000000000000ull)
        return false;
    const uint32_t begin = (std::min)({layout.pid, layout.created, layout.exited});
    const uint32_t end = (std::max)({layout.pid, layout.created, layout.exited}) + 8;
    std::array<unsigned char, 0x2000> bytes{};
    if (!read(address + begin, bytes.data(), end - begin))
        return false;
    std::memcpy(&sample.pid, bytes.data() + layout.pid - begin, 8);
    std::memcpy(&sample.created, bytes.data() + layout.created - begin, 8);
    std::memcpy(&sample.exited, bytes.data() + layout.exited - begin, 8);
    return sample.pid != 0 && sample.created != 0;
}
}
