#pragma once
#include "output/OutputTypes.hpp"
#include <array>
#include <cstddef>
#include <cstdint>
#include <vector>

namespace V2::Output {
    enum class MockFaultMode : int {
        None = 0,
        OutputTimeout,
        DropOutput
    };

    enum class MockEventType : int {
        Move = 0,
        AutoMove,
        Button,
        ButtonStateMask,
        Keyboard,
        MaskMouse,
        UnmaskAll
    };

    const char* ToString(MockFaultMode mode);
    const char* ToString(MockEventType type);

    struct MockEvent {
        MockEventType type = MockEventType::Move;
        unsigned long long sequence = 0;
        int x = 0;
        int y = 0;
        int runtimeMs = 0;
        int button = -1;
        bool down = false;
        uint32_t stateMask = 0;
        uint32_t mask = 0;
        unsigned char hidCode = 0;
        int status = success;
        bool attempted = false;
    };

    struct MockHardwareSnapshot {
        bool initialized = false;
        MockFaultMode faultMode = MockFaultMode::None;
        uint32_t outputMouseButtons = 0;
        uint32_t maskedButtons = 0;
        std::size_t outputKeyboardKeys = 0;
        unsigned char outputKeyboardModifierMask = 0;
        std::array<unsigned char, 10> outputKeyboardUsages{};
        unsigned long long totalEvents = 0;
        unsigned long long moveEvents = 0;
        unsigned long long buttonEvents = 0;
        unsigned long long keyboardEvents = 0;
        unsigned long long maskEvents = 0;
        std::size_t recentEventCount = 0;
    };

    MockHardwareSnapshot SnapshotMock();
    void SetMockFaultMode(MockFaultMode mode);
    void ResetMock();
    std::vector<MockEvent> RecentMockEvents();
    void SetMockPhysicalButtons(std::uint32_t buttons);
}
