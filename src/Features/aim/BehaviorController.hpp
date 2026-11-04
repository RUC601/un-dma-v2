#pragma once
#include "Game/Target.hpp"
#include "Game/FlickRepeat.hpp"
#include "Game/AimStartLimiter.hpp"
#include "Game/AimHoldWindow.hpp"
#include "Game/AimReleaseFire.hpp"

// UN Behavior 控制器：worker 独占 RuntimeState；技能只使用下列计算/输出接口。
// 输入为已发布实体/矩阵和当前配置视图；输出仍经代次、身份和释放门控。
// 断连/停止结束 activation；这里不拥有 DMA 或设备连接。
namespace OW { bool RefreshAutoGameMouseSensitivity(bool force = false); }
namespace AimbotDetail {
    void LogMagneticTriggerQuantizationReset(
        const char* reason,
        bool hadState);

    struct RuntimeState {
        uint64_t entityScene=0;
        int hitbotdelaytime = 0;
        int afterdelaytime = 0;
        bool dodelay = false;
        DWORD lastFlickFireTick = 0;
        OW::FlickRepeat::State ordinaryFlick{};
        uint64_t trajectoryWaitEntityKey = 0;
        DWORD trajectoryWaitStartedTick = 0;
        std::array<OW::AimReleaseFire::PhysicalPress,3> releasePresses{};
        OW::FlickRepeat::SessionIdentity releaseSession{};
        int releaseMode=-2,releaseBehavior=-1,releaseMethod=-1;
        bool releaseConfigured=false,releaseAutomatic=false,releaseIdentified=false;
        bool releaseOwnedDown=false,releaseShotLatched=false,releaseNeedsActivationUp=false;
        bool releaseAfterKeyUp=false,releaseActivationDown=false,releasePending=false;
        DWORD releaseActionStarted=0;
        DWORD releaseAimReportTick=0;
        bool releaseChargeWaitLogged=false;
        int releaseOwnedButton=-1;
        DWORD releaseChargeStarted=0;
        std::uint64_t releaseOutputGeneration=0;
        bool trackingSessionActive = false;
        std::array<OW::AimHoldWindow::State, std::size(OW::Labels::kAimActivationKeyVks)> aimHoldWindows{};
        uint64_t trackingSessionHeroId = 0;
        int trackingSessionSlotIndex = -1;
        uint64_t trackingSessionSlotId = 0;
        int trackingSessionAimKey = 0;
        uint64_t trackingSessionGeneration = 0;
        uint64_t trackingTargetKey = 0;
        uint64_t trackingTargetActorKey = 0;
        DWORD trackingTargetMissingSinceTick = 0;
        bool trackingInDeadzone = false;
        OW::AimStartLimiterState trackingStartLimiter{};
        OW::MouseMoveQuantizationState magneticMoveQuantization{};
    };
    void ResetMagneticTriggerQuantization(
        RuntimeState& state,
        const char* reason);
    struct AimData {
        bool valid = false;
        OW::Vector3 local_angle{};
        OW::Vector3 target_angle{};
        OW::Vector3 smoothed_angle{};
        OW::Vector3 local_pos{};
    };
    bool IsZeroVector(const OW::Vector3& value);
    bool IsInputVkDown(int vk, int keySetting = -1);
    void MoveAimDelta(const OW::Vector3& current_angle, const OW::Vector3& target_angle, int move_time_ms = -1);
    bool IsPrimaryTargetActionable(OW::c_entity& target);
    AimData BuildAimData(const OW::Vector3& world_target,
                                bool accelerated,
                                float smooth,
                                float acceleration,
                                int methodOverride = -1,
                                float bezierSpeedOverride = -1.0f,
                                bool commitSmoothingOutput = true,
                                bool calculateSmoothing = true,
                                bool inheritTargetMotion = true);
    void ResetTrackingSession(RuntimeState& state);
    void PauseOrdinaryFlickMovement(RuntimeState& state);
    void EndOrdinaryFlickActivation(RuntimeState& state);
    OW::FlickRepeat::Phase OrdinaryFlickPhase(const RuntimeState& state,
                                                DWORD now = GetTickCount());
}
void aimbot_thread();
