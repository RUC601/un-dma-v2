#pragma once
#include <algorithm>
#include <chrono>
#include <cstdint>
#include <Windows.h>
#include <stdexcept>

namespace V2::Acquisition {
inline constexpr int kTargetHz = 300;
using Clock = std::chrono::steady_clock;
inline auto Period() {
    return std::chrono::duration_cast<Clock::duration>(
        std::chrono::duration<double>(1.0 / kTargetHz));
}
inline void Wait(Clock::time_point& next, Clock::duration period) {
    struct Timer {
        HANDLE value=CreateWaitableTimerExW(nullptr,nullptr,CREATE_WAITABLE_TIMER_HIGH_RESOLUTION,TIMER_ALL_ACCESS);
        Timer() { if(!value)throw std::runtime_error("Acquisition timer creation failed"); }
        ~Timer() { CloseHandle(value); }
    };
    thread_local Timer timer;
    const auto now = Clock::now();
    if (now < next) {
        LARGE_INTEGER due;
        due.QuadPart=-(std::max)(int64_t{1},std::chrono::duration_cast<std::chrono::duration<int64_t,std::ratio<1,10000000>>>(next-now).count());
        if(!SetWaitableTimer(timer.value,&due,0,nullptr,nullptr,FALSE))
            throw std::runtime_error("Acquisition timer scheduling failed");
        WaitForSingleObject(timer.value,INFINITE);
    }
    else if (now - next >= period)
        next = now;
    next += period;
}
}
