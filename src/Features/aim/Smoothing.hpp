#pragma once
#include "Utils/Types.hpp"
namespace OW {
// 单个业务 worker 的轨迹状态，跨线程不共享 PID/Bezier/overshoot。
// 改变目标/Slot/会话时由控制器调用 Reset；暂停不积累 dt。
void ResetAimSmoothingState();
void HoldAimSmoothingState();
void PrimeAimSmoothingClock();
void CommitAimMotionCommand(const Vector3& acceptedAngularCommand);
void CommitAimSmoothingOutput(const Vector3& outputDelta);
Vector3 SmoothDispatchWithMethod(Vector3 local, Vector3 target, float speed, float accel,
    int methodOverride, float bezierSpeedOverride = -1.0f, bool commitOvershootStep = true);
Vector3 SmoothDispatch(Vector3 local, Vector3 target, float speed, float accel);
namespace AimSmoothingDetail { float ComputeDeltaTime(); }
}
