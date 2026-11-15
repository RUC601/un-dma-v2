#pragma once
#include <array>
#include <cstdint>
#include <vector>

namespace OW::EntityDiscovery {
enum class Work : uint8_t { LocalBinding, KnownBinding, Scan, PositiveBinding, None };

// A due fast lane must not consume every owner visit when acquisition slows.
// Each continuously eligible lane gets a turn within four discovery visits;
// lanes that are not due donate their turn without adding any work to a visit.
struct WorkSchedule {
    uint8_t cursor = 0;
    Work Select(bool localDue, bool knownDue, bool scanDue, bool positiveDue=false) {
        const std::array<bool, 4> due{localDue, knownDue, scanDue, positiveDue};
        for (uint8_t offset = 0; offset < due.size(); ++offset) {
            const auto index = uint8_t((cursor + offset) % due.size());
            if (!due[index]) continue;
            cursor = uint8_t((index + 1) % due.size());
            return static_cast<Work>(index);
        }
        return Work::None;
    }
};

// A dead/missing body must not wait behind every healthy controller. Alternate
// recovery and ordinary renewal so neither group can starve the other.
struct BindingSchedule {
    size_t recoveryCursor=0, regularCursor=0;
    bool recoveryTurn=true;
    size_t Select(const std::vector<uint8_t>& recovery) {
        for(unsigned pass=0;pass<2;++pass) {
            const bool wanted=pass==0?recoveryTurn:!recoveryTurn;
            auto& cursor=wanted?recoveryCursor:regularCursor;
            for(size_t n=0;n<recovery.size();++n) {
                const auto i=(cursor+n)%recovery.size();
                if(bool(recovery[i])!=wanted)continue;
                cursor=(i+1)%recovery.size();recoveryTurn=!wanted;
                return i;
            }
        }
        return recovery.size();
    }
};
}
