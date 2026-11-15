#pragma once
#include <array>
#include <cstdint>

namespace OW::EntityDiscovery {
enum class Work : uint8_t { LocalBinding, KnownBinding, Scan, None };

// A due fast lane must not consume every owner visit when acquisition slows.
// Each continuously eligible lane gets a turn within three discovery visits;
// lanes that are not due donate their turn without adding any work to a visit.
struct WorkSchedule {
    uint8_t cursor = 0;
    Work Select(bool localDue, bool knownDue, bool scanDue) {
        const std::array<bool, 3> due{localDue, knownDue, scanDue};
        for (uint8_t offset = 0; offset < due.size(); ++offset) {
            const auto index = uint8_t((cursor + offset) % due.size());
            if (!due[index]) continue;
            cursor = uint8_t((index + 1) % due.size());
            return static_cast<Work>(index);
        }
        return Work::None;
    }
};
}
