#pragma once
#include <Windows.h>
#include <cstdint>
// 相机终端采样由统一采集线程调用，线程退出时释放本地句柄。
bool SampleAcquisitionCamera();

// 诊断读取同一份不可变启动配置，不重复解析环境变量。
DWORD ViewMatrixPollSleepMs();
DWORD ViewMatrixScanBackoffMs();
DWORD ViewMatrixScanDueGuardMs();
uint64_t CriticalMatrixPollPeriodUs();
uint64_t CriticalMatrixSpinTailUs();
bool CriticalMatrixAboveNormalRequested();
