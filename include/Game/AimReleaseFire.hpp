#pragma once
#include <cmath>
#include <cstdint>

namespace OW::AimReleaseFire {
struct PhysicalPress {
    bool down=false, consumed=false, masked=false;
    std::uint32_t started=0;
    std::uint64_t generation=0;
    void Observe(bool held,std::uint32_t now) {
        if(held && !down) started=now;
        if(!held) {consumed=false;started=0;}
        down=held;
    }
    bool Ready() const {return down&&!consumed&&!masked;}
    void Submitted(bool accepted) {if(accepted) consumed=true;}
};
inline bool ChargeReady(float percent,float minimum,std::uint32_t elapsed,float fallbackMs) {
    if(std::isfinite(percent)&&percent>=0&&percent<=100) return percent>=minimum;
    return std::isfinite(fallbackMs)&&fallbackMs>=0&&elapsed>=fallbackMs;
}
}
