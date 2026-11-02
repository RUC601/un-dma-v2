#pragma once
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>
#include <cstdio>
#include <vector>
#include <string>
#include <atomic>
#include <mutex>
#include <unordered_map>
#include <memory>
#include "Memory/DmaSessionAccess.hpp"
#include "Memory/ProcessLifetime.hpp"
#include <TlHelp32.h>
#include "leechcore.h"
#include "vmmdll.h"

#ifndef LOG
#define LOG(...) printf(__VA_ARGS__)
#endif

enum class DmaDeviceType
{
	FTD3XX = 0,
	FTD3XXWU,
	RAWTCP,
	HVSAVEDSTATE
};

class Memory
{
private:
	struct CurrentProcessInformation
	{
		int PID = 0;
		size_t base_address = 0;
		size_t base_size = 0;
		std::string process_name = "";
		uint64_t eprocess = 0;
		uint64_t created = 0;
	};

	CurrentProcessInformation current_process{};

	std::atomic<bool> DMA_INITIALIZED{ false };
	std::atomic<bool> PROCESS_INITIALIZED{ false };
	std::atomic<bool> m_cancelPending{ false };
	std::atomic<int> m_currentPid{ 0 };
	mutable DmaSessionAccess m_sessionAccess;
	std::mutex m_sessionLifecycleMutex;
	mutable std::recursive_mutex m_processInfoMutex;
	mutable std::mutex m_scatterMutex;
	struct ScatterState {
		std::mutex mutex;
		VMMDLL_SCATTER_HANDLE native = nullptr;
		~ScatterState() { if (native) VMMDLL_Scatter_CloseHandle(native); }
	};
	mutable std::unordered_map<VMMDLL_SCATTER_HANDLE, std::shared_ptr<ScatterState>> m_scatterHandles;
	std::shared_ptr<ScatterState> FindScatterHandle(VMMDLL_SCATTER_HANDLE handle) const;
	mutable uintptr_t m_nextScatterToken = 1;
	bool m_cr3PluginsInitialized = false;
	ProcessLifetime::Layout m_processLayout{};
	uint64_t m_systemEprocess = 0;
	uint64_t m_systemCreated = 0;
	bool InitializeProcessMonitor();
	bool ReadProcessLifetime(uint64_t address, ProcessLifetime::Sample& sample) const;

	HMODULE hVMM = nullptr;
	HMODULE hFTD3XX = nullptr;
	HMODULE hFTD3XXWU = nullptr;
	HMODULE hLEECHCORE = nullptr;
	HMODULE hLEECHCORE_DEVICE_RAWTCP = nullptr;
	HMODULE hLEECHCORE_DEVICE_HVSAVEDSTATE = nullptr;
	HMODULE hLEECHCORE_DRIVER = nullptr;
	HMODULE hHELPER64 = nullptr;

	DmaDeviceType m_deviceType = DmaDeviceType::FTD3XX;
	std::string m_rawTcpHost = "";
	std::string m_deviceString = "fpga://algo=0";

	bool EnsureRuntimeLibrariesLoaded();
	bool DumpMemoryMap(const std::string& outputPath, bool debug = false);
	bool SetFPGA();
	std::string BuildDeviceString(DmaDeviceType deviceType) const;
	const char* GetDeviceTypeName(DmaDeviceType deviceType) const;

public:
	struct ProcessLookupResult
	{
		DWORD pid = 0;
		std::string name = "";

		explicit operator bool() const { return pid != 0; }
	};

	Memory() = default;
	~Memory();

	// -- Initialization --
	bool LoadDmaDeviceConfig(const std::string& configPath = "");
	DmaDeviceType GetDmaDeviceType() const { return m_deviceType; }
	const std::string& GetDmaDeviceString() const { return m_deviceString; }
	bool InitDma(DmaDeviceType deviceType, bool memMap = true, bool debug = false);
	bool InitDma(bool memMap = true, bool debug = false);
	bool AttachToProcess(const std::string& process_name, bool applyCr3Fix = true);
	bool Init(std::string process_name, bool memMap = true, bool debug = false);
	void DetachProcess();
	void CloseDma();
	DmaSessionAccess::Lease AcquireSession() const { return m_sessionAccess.Acquire(); }
	DmaSessionAccess::Activity SessionActivity() const { return m_sessionAccess.Inspect(); }
	bool CheckSessionHealth() const;
	bool CheckAttachedProcessAlive() const;
	// Only with runtime workers drained: this refresh may block all DMA reads.
	bool RefreshProcessMetadata(const char* reason) const;
	bool IsDmaInitialized() const { return DMA_INITIALIZED.load(std::memory_order_acquire); }
	void CancelPendingOperations() { m_cancelPending.store(true, std::memory_order_release); }
	void ResetPendingOperations() { m_cancelPending.store(false, std::memory_order_release); }

	// -- Process information --
	bool IsProcessInitialized() const { return PROCESS_INITIALIZED.load(); }
	int GetCurrentProcessId() const { return PROCESS_INITIALIZED.load() ? m_currentPid.load() : 0; }
	DWORD GetPidFromName(const std::string& process_name);
	std::vector<int> GetPidListFromName(const std::string& name);
	ProcessLookupResult FindProcessByNamePrefix(const std::string& namePrefix);
	ProcessLookupResult FindProcessByPrefix(const std::string& prefix) { return FindProcessByNamePrefix(prefix); }
	std::vector<std::string> GetModuleList(const std::string& process_name);
	VMMDLL_PROCESS_INFORMATION GetProcessInformation();
	size_t GetBaseDaddy(std::string module_name);
	size_t GetBaseSize(std::string module_name);
	bool FixCr3();

	// -- Export / Import resolution --
	uintptr_t GetExportTableAddress(std::string import, std::string process, std::string module);
	uintptr_t GetImportTableAddress(std::string import, std::string process, std::string module);

	// -- Memory dumping / scanning --
	bool DumpMemory(uintptr_t address, std::string path);
	uint64_t FindSignature(const char* signature, uint64_t range_start, uint64_t range_end, int PID = 0);

	// -- Direct read (raw) --
	bool Read(uintptr_t address, void* buffer, size_t size) const;
	bool ReadFreshExact(uintptr_t address, void* buffer, size_t size) const;
	bool Read(uintptr_t address, void* buffer, size_t size, int pid) const;

	// -- Direct read (typed) --
	template <typename T>
	T Read(uint64_t address) const
	{
		T buffer{};
		Read(static_cast<uintptr_t>(address), reinterpret_cast<void*>(&buffer), sizeof(T));
		return buffer;
	}

	template <typename T>
	T Read(uint64_t address, int pid) const
	{
		T buffer{};
		Read(static_cast<uintptr_t>(address), reinterpret_cast<void*>(&buffer), sizeof(T), pid);
		return buffer;
	}

	// -- Direct write (raw) --
	bool Write(uintptr_t address, const void* buffer, size_t size) const;
	bool Write(uintptr_t address, const void* buffer, size_t size, int pid) const;

	// -- Direct write (typed) --
	template <typename T>
	bool Write(uint64_t address, const T& value) const
	{
		return Write(static_cast<uintptr_t>(address), reinterpret_cast<const void*>(&value), sizeof(T));
	}

	template <typename T>
	bool Write(uint64_t address, const T& value, int pid) const
	{
		return Write(static_cast<uintptr_t>(address), reinterpret_cast<const void*>(&value), sizeof(T), pid);
	}

	// -- Convenience aliases (raw) --
	bool ReadMemory(uintptr_t address, void* buffer, size_t size) const { return Read(address, buffer, size); }
	bool WriteMemory(uintptr_t address, const void* buffer, size_t size) const { return Write(address, buffer, size); }

	// -- Convenience aliases (typed) --
	template <typename T>
	bool ReadMemory(uint64_t address, T& value) const
	{
		return Read(static_cast<uintptr_t>(address), reinterpret_cast<void*>(&value), sizeof(T));
	}

	template <typename T>
	bool WriteMemory(uint64_t address, const T& value) const
	{
		return Write<T>(address, value);
	}

	// -- Cache prefetch --
	bool PrefetchPages(const std::vector<uint64_t>& pageAddresses, int pid = 0) const;

	// -- Scatter read --
	VMMDLL_SCATTER_HANDLE CreateScatterHandle() const;
	VMMDLL_SCATTER_HANDLE CreateScatterHandle(int pid) const;
	void CloseScatterHandle(VMMDLL_SCATTER_HANDLE handle);
	bool AddScatterReadRequest(VMMDLL_SCATTER_HANDLE handle, uint64_t address, void* buffer, size_t size);
	bool AddScatterReadRequest(VMMDLL_SCATTER_HANDLE handle, uint64_t address, void* buffer, size_t size, DWORD* bytesRead);

	template <typename T>
	bool AddScatterReadRequest(VMMDLL_SCATTER_HANDLE handle, uint64_t address, T* buffer)
	{
		return AddScatterReadRequest(handle, address, reinterpret_cast<void*>(buffer), sizeof(T));
	}

	template <typename T>
	bool AddScatterReadRequest(VMMDLL_SCATTER_HANDLE handle, uint64_t address, T* buffer, DWORD* bytesRead)
	{
		return AddScatterReadRequest(handle, address, reinterpret_cast<void*>(buffer), sizeof(T), bytesRead);
	}

	bool ExecuteReadScatter(VMMDLL_SCATTER_HANDLE handle, int pid = 0);

	// Raw SDK users must hold AcquireSession() for the complete call. Scatter
	// handles are Memory-owned opaque tokens and must use these wrappers only.
	VMM_HANDLE vHandle = nullptr;
};

inline Memory mem;
