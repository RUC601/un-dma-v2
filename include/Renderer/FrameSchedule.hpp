#pragma once

#include <algorithm>
#include <chrono>

namespace V2::Rendering {

// Keep canvas deadlines independent of menu refreshes and window messages.
class FrameSchedule {
public:
    using Clock = std::chrono::steady_clock;
    void SetLimit(int fps, Clock::time_point now) {
        fps = std::clamp(fps, 0, 2000);
        if (fps == m_limit) return;
        m_limit = fps;
        m_period = fps ? std::chrono::duration_cast<Clock::duration>(
            std::chrono::duration<double>(1.0 / fps)) : Clock::duration::zero();
        m_next = now;
    }
    bool Due(Clock::time_point now) const { return !m_limit || now >= m_next; }
    void FrameStarted(Clock::time_point now) {
        // Skip missed deadlines instead of submitting a burst after a stall.
        if (now - m_next >= m_period) m_next = now;
        m_next += m_period;
    }
    Clock::time_point Next() const { return m_next; }

private:
    int m_limit = -1;
    Clock::duration m_period{};
    Clock::time_point m_next{};
};

} // namespace V2::Rendering
