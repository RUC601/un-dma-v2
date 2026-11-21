#pragma once
#include <Windows.h>
#include <cstdint>
#include "Utils/Types.hpp"
// 相机终端采样由统一采集线程调用，线程退出时释放本地句柄。
bool PrepareAcquisitionCameraPointers(uint64_t& cameraAddr, uint64_t& projectionAddr);
bool CommitAcquisitionCameraSample(const OW::Matrix& camera, const OW::Matrix& projection,
                                   uint64_t startedMs, uint64_t completedMs, bool readOk);
bool SampleAcquisitionCamera();

// 诊断读取同一份不可变启动配置，不重复解析环境变量。
DWORD ViewMatrixPollSleepMs();
DWORD ViewMatrixScanBackoffMs();
DWORD ViewMatrixScanDueGuardMs();
uint64_t CriticalMatrixPollPeriodUs();
uint64_t CriticalMatrixSpinTailUs();
bool CriticalMatrixAboveNormalRequested();
