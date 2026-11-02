#include "FileTransaction.hpp"
#include <algorithm>
#include <cstdio>
namespace V2::ConfigStorage {
        // Each caller keeps its own result so a background hero save cannot
        // overwrite the error that the menu is about to display.
        namespace { thread_local std::string gPersistenceError; }
        std::string& PersistenceError() { return gPersistenceError; }

        void BeginPersistenceOperation()
        {
            gPersistenceError.clear();
        }

        bool PersistenceFailure(const std::string& path, const char* operation,
                                DWORD error)
        {
            if (gPersistenceError.empty()) {
                char detail[256]{};
                std::snprintf(detail, sizeof(detail), "%s failed (Windows error %lu): ",
                              operation, static_cast<unsigned long>(error));
                gPersistenceError = detail + path;
            }
            return false;
        }

        bool MakeSiblingTemporaryFile(const std::string& path, std::string& temporary)
        {
            const size_t slash = path.find_last_of("\\/");
            const std::string directory = slash == std::string::npos ? "." : path.substr(0, slash);
            char name[MAX_PATH]{};
            if (!GetTempFileNameA(directory.c_str(), "unc", 0, name))
                return PersistenceFailure(path, "Create temporary file");
            temporary = name;
            return true;
        }

        bool CommitTemporaryFile(const std::string& temporary, const std::string& path)
        {
            if (MoveFileExA(temporary.c_str(), path.c_str(),
                            MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH))
                return true;
            const DWORD error = GetLastError();
            DeleteFileA(temporary.c_str());
            return PersistenceFailure(path, "Replace config", error);
        }

        bool WriteAtomicBytes(const std::string& path, const char* bytes, size_t size)
        {
            std::string temporary;
            if (!MakeSiblingTemporaryFile(path, temporary))
                return false;
            HANDLE file = CreateFileA(temporary.c_str(), GENERIC_WRITE, 0, nullptr,
                                      TRUNCATE_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
            bool ok = file != INVALID_HANDLE_VALUE;
            DWORD error = ok ? ERROR_SUCCESS : GetLastError();
            if (ok) {
                size_t offset = 0;
                while (offset < size) {
                    const DWORD chunk = static_cast<DWORD>((std::min)(size - offset, size_t(1 << 20)));
                    DWORD written = 0;
                    if (!WriteFile(file, bytes + offset, chunk, &written, nullptr) || written != chunk) {
                        ok = false;
                        error = GetLastError();
                        if (error == ERROR_SUCCESS) error = ERROR_WRITE_FAULT;
                        break;
                    }
                    offset += written;
                }
                if (ok && !FlushFileBuffers(file)) {
                    ok = false;
                    error = GetLastError();
                }
                if (!CloseHandle(file) && ok) {
                    ok = false;
                    error = GetLastError();
                }
            }
            if (!ok) {
                DeleteFileA(temporary.c_str());
                return PersistenceFailure(path, "Write config", error);
            }
            return CommitTemporaryFile(temporary, path);
        }

}
