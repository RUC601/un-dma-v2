#pragma once
#include <Windows.h>
#include <string>
#include <cstddef>
namespace V2::ConfigStorage {
// 每个调用线程独立错误；同目录临时文件 -> flush -> replace，失败不覆盖原件。
std::string& PersistenceError();
void BeginPersistenceOperation();
bool PersistenceFailure(const std::string& path, const char* operation, DWORD error=GetLastError());
bool MakeSiblingTemporaryFile(const std::string& path,std::string& temporary);
bool CommitTemporaryFile(const std::string& temporary,const std::string& path);
bool WriteAtomicBytes(const std::string& path,const char* bytes,size_t size);
}
