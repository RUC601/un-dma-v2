#pragma once
#include <windows.h>
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <string>
#include <vector>
#include "Game/RuntimeState.hpp"
#include "Game/InputOrchestrator.hpp"
#include "Game/CalibrationPolicy.hpp"
#include "output/OutputRuntime.hpp"
#include "Utils/Config.hpp"
#include "Utils/ProcessConnection.hpp"
#include "output/Quantization.hpp"
#include "output/MotionTelemetry.hpp"

// 业务动作入口：设备代次与连接代次在分发前再次复核，失败不当作成功。
namespace OW {
    bool DmaKeyToMouseButton(uint32_t key, int& button);

    int ClampOutputAutomoveRuntimeMs(int moveTimeMs);

    const char* OutputTransportName();

    bool ShouldSuppressOutput(
        const char* action,
        OutputIntent intent = OutputIntent::Normal);

    int EnqueueOutputPixelMoveForGeneration(
        int pixelX,
        int pixelY,
        int automoveRuntimeMs,
        std::uint64_t expectedGeneration,
        std::uint64_t expectedConnectionEpoch = 0);

    int EnqueueOutputPixelMove(
        int pixelX,
        int pixelY,
        int automoveRuntimeMs);

    MouseMoveQuantizationState& DefaultMouseMoveQuantizationState();

    MouseMoveQuantizationResult SendMouseMove(
        const Vector3& delta,
        int moveTimeMs,
        const MouseMoveQuantizationOptions& quantization);

    void SendMouseMove(const Vector3& delta, int moveTimeMs = -1);

    void SendMouseMove(float deltaX, float deltaY, int moveTimeMs = -1);

    enum class GameAction : uint8_t {
        PrimaryFire = 0,
        SecondaryFire,
        MiddleMouse,
        Ability1,
        Ability2,
        Ultimate,
        Melee,
        MoveForward
    };

    enum class ActionOutputStatus : uint8_t {
        Sent = 0,
        Disabled,
        Suppressed,
        UnsupportedTransport,
        InvalidAction,
        TransportError
    };

    bool ActionOutputSucceeded(ActionOutputStatus status);

    ActionOutputStatus UnavailableActionOutputStatus();

    std::string DefaultMouseButtonOwnerKey(OutputOwnerSource ownerSource, int button);

    const char* GameActionOwnerKey(GameAction action);

    ActionOutputStatus SetOwnedOutputControl(
        const std::string& ownerKey,
        OutputOwnerSource ownerSource,
        OutputControl control,
        bool down,
        const char* outputName,
        bool requireKeyboardOutput = false,
        OutputUnownedControlCallback ownerlessRelease = {},
        std::uint64_t expectedGeneration = 0,
        bool requireNewPress = false);

    ActionOutputStatus SendMouseButtonActionState(
        int button,
        bool down,
        OutputOwnerSource ownerSource = OutputOwnerSource::GlobalAim,
        const char* ownerKey = nullptr,
        std::uint64_t expectedGeneration = 0,
        bool requireNewPress = false);

    ActionOutputStatus ReleaseMouseButtonActionStateOrPhysicalFallback(
        int button,
        OutputOwnerSource ownerSource,
        const char* ownerKey,
        std::uint64_t expectedGeneration,
        bool* physicalFallbackReleased = nullptr);

    ActionOutputStatus ReleaseMouseButtonActionStateForGeneration(
        int button,
        OutputOwnerSource ownerSource,
        const char* ownerKey,
        std::uint64_t expectedGeneration);

    void SendMouseButton(
        int button,
        bool down,
        OutputOwnerSource ownerSource = OutputOwnerSource::GlobalAim,
        const char* ownerKey = nullptr);

    unsigned char GameActionKeyboardHid(GameAction action);

    ActionOutputStatus SetActionStateForGeneration(
        GameAction action,
        bool down,
        std::uint64_t expectedGeneration);

    ActionOutputStatus SetActionState(GameAction action, bool down);

    ActionOutputStatus PulseAction(GameAction action, DWORD durationMs = 10);

    void ForceReleaseMouseButtons();

    void ForceReleaseMouseButton(int button);

    bool SendMouseButtonStateMask(uint32_t stateMask, bool force = false);

    bool MaskPhysicalMouseButtons(uint32_t mask);

    bool UnmaskPhysicalMouseButtons();


}
