#pragma once
#include <Windows.h>
#include <cstring>
#include <string>

// Current CN settings take precedence; existing CN_NE launch environments still work.
inline DWORD GetCnEnvironmentVariableA(const char* name, char* buffer, DWORD size)
{
    SetLastError(ERROR_SUCCESS);
    const DWORD length = GetEnvironmentVariableA(name, buffer, size);
    if (length != 0 || GetLastError() != ERROR_ENVVAR_NOT_FOUND ||
        std::strncmp(name, "UN_DMA_CN_", 10) != 0)
        return length;
    const std::string legacy = std::string("UN_DMA_CN_NE_") + (name + 10);
    return GetEnvironmentVariableA(legacy.c_str(), buffer, size);
}
