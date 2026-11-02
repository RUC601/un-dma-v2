#pragma once
namespace V2::Runtime {
// 本模块独占连接/采集线程。启动提交与最终退出共享生命周期门；
// 排空完成前调用方不得关闭 DMA。重连复用同一排空路径。
void StartProcessConnectionThread();
void StopProcessConnectionThread();
void DrainProcessRuntimeWorkers(const char* reason);
// 回归检查也走真实入口，验证最终停止不能被晚到的启动撤销。
bool StartBackgroundThreads();
void ResumeConnectionOperations();
bool ThreadsStopped(); // 只能在停止/最终退出后检查
}
