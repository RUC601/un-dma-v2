#pragma once
#include <windows.h>
namespace V2::Output {
int InitializeOutputFromConfig();
int ReleaseOutputMouseStateForShutdown(const char* reason, DWORD timeoutMs = 500);
int ShutdownOutputRuntime(const char* reason, DWORD timeoutMs = 500);
}
