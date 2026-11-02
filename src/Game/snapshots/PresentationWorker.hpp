#pragma once
// worker 独占插值轨迹；scene 改变清空缓存；停止后不再提交呈现结果。
void present_interp_thread();

#include <chrono>
#include <cstdint>
inline uint64_t PresentSteadyNowUs() {
    return static_cast<uint64_t>(std::chrono::duration_cast<std::chrono::microseconds>(
        std::chrono::steady_clock::now().time_since_epoch()).count());
}
