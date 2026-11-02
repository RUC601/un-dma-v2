#include "Memory/Memory.h"
#include "Utils/RuntimeTrace.hpp"
#include "Utils/Diagnostics.hpp"

#include <thread>
#include <mutex>
#include <algorithm>
#include <cctype>
#include <cwctype>
#include <chrono>
#include <limits>
#include <memory>
#include <sstream>
#include <fstream>
#include <filesystem>

/* ------------------------------------------------------------------ */
/*  Helpers                                                            */
/* ------------------------------------------------------------------ */

#define _is_invalid(v)     if ((v) == NULL) return false
#define _is_invalid_r(v,r) if ((v) == NULL) return (r)

namespace {

	std::mutex g_runtimeLibraryMutex;
	std::mutex g_fixCr3Mutex;

	std::wstring Utf8ToWide(const std::string& text)
	{
		if (text.empty())
			return {};

		const int required = MultiByteToWideChar(
			CP_UTF8, 0,
			text.data(), static_cast<int>(text.size()),
			nullptr, 0);
		if (required <= 0)
			return std::wstring(text.begin(), text.end());

		std::wstring wide(static_cast<size_t>(required), L'\0');
		MultiByteToWideChar(
			CP_UTF8, 0,
			text.data(), static_cast<int>(text.size()),
			wide.data(), required);
		return wide;
	}

	std::string BuildMemMapCachePath()
	{
		auto tempPath = std::filesystem::temp_directory_path();
		return (tempPath / ("UNDMA_DMA_mmap_" +
			std::to_string(GetCurrentProcessId()) + ".txt")).string();
	}

	std::filesystem::path GetExecutableDirectory()
	{
		char path[MAX_PATH] = {};
		const DWORD length = GetModuleFileNameA(nullptr, path, MAX_PATH);
		if (length == 0 || length == MAX_PATH)
			return std::filesystem::current_path();
		return std::filesystem::path(path).parent_path();
	}

	std::filesystem::path ResolveConfigPath(const std::string& configPath)
	{
		if (configPath.empty())
			return GetExecutableDirectory() / "config" / "dma_device.cfg";

		std::filesystem::path path(configPath);
		if (path.is_relative())
			path = std::filesystem::current_path() / path;
		return path;
	}

	std::string Trim(std::string text)
	{
		auto notSpace = [](unsigned char c) { return !std::isspace(c); };
		text.erase(text.begin(), std::find_if(text.begin(), text.end(), notSpace));
		text.erase(std::find_if(text.rbegin(), text.rend(), notSpace).base(), text.end());
		return text;
	}

	std::string ToLower(std::string text)
	{
		std::transform(text.begin(), text.end(), text.begin(),
			[](unsigned char c) { return static_cast<char>(std::tolower(c)); });
		return text;
	}

	bool StartsWithNoCase(const std::string& value, const std::string& prefix)
	{
		const std::string lowerValue = ToLower(value);
		const std::string lowerPrefix = ToLower(prefix);
		return lowerValue.size() >= lowerPrefix.size() &&
			lowerValue.compare(0, lowerPrefix.size(), lowerPrefix) == 0;
	}

	bool StartsWithNoCase(const std::string& value, const char* prefix)
	{
		return StartsWithNoCase(value, std::string(prefix ? prefix : ""));
	}

	bool ParseDmaDeviceType(const std::string& value, DmaDeviceType& deviceType)
	{
		const std::string normalized = ToLower(Trim(value));
		if (normalized == "fpga" || normalized == "ftd3xx")
		{
			deviceType = DmaDeviceType::FTD3XX;
			return true;
		}
		if (normalized == "ftd3xxwu")
		{
			deviceType = DmaDeviceType::FTD3XXWU;
			return true;
		}
		if (normalized == "rawtcp")
		{
			deviceType = DmaDeviceType::RAWTCP;
			return true;
		}
		if (normalized == "hvsavedstate")
		{
			deviceType = DmaDeviceType::HVSAVEDSTATE;
			return true;
		}
		return false;
	}

	bool TryLoadLibrary(const char* libraryName, HMODULE& handle, bool required)
	{
		if (handle)
		{
			LOG("[INFO] Runtime library already loaded: %s (%p).\n", libraryName, handle);
			return true;
		}

		handle = LoadLibraryA(libraryName);
		if (handle)
		{
			LOG("[INFO] Loaded runtime library: %s (%p).\n", libraryName, handle);
			return true;
		}

		const DWORD error = GetLastError();
		if (required)
			LOG("[ERROR] Required runtime library failed to load: %s (Win32 error %lu).\n",
				libraryName, static_cast<unsigned long>(error));
		else
			LOG("[WARN] Optional runtime library not loaded: %s (Win32 error %lu).\n",
				libraryName, static_cast<unsigned long>(error));
		return false;
	}

	/* ---- DTB / FixCr3 helpers ---- */

	std::atomic<uint64_t> cbSize{ 0x80000 };

	VOID cbAddFile(_Inout_ HANDLE h, _In_ LPSTR uszName, _In_ ULONG64 cb,
		_In_opt_ PVMMDLL_VFS_FILELIST_EXINFO pExInfo)
	{
		if (strcmp(uszName, "dtb.txt") == 0)
			cbSize.store(cb, std::memory_order_relaxed);
	}

	struct Info
	{
		uint32_t index;
		uint32_t process_id;
		uint64_t dtb;
		uint64_t kernelAddr;
		std::string name;
	};

	/* ---- LPSTR helpers (VMMDLL API uses non-const LPSTR) ---- */
	inline LPSTR ToLpstr(const char* s) { return const_cast<LPSTR>(s); }

	/* ---- Signature scan helpers ---- */

	static const char* hexdigits =
		"\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000"
		"\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000"
		"\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000"
		"\000\001\002\003\004\005\006\007\010\011\000\000\000\000\000\000"
		"\000\012\013\014\015\016\017\000\000\000\000\000\000\000\000\000"
		"\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000"
		"\000\012\013\014\015\016\017\000\000\000\000\000\000\000\000\000"
		"\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000"
		"\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000"
		"\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000"
		"\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000"
		"\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000"
		"\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000"
		"\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000"
		"\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000"
		"\000\000\000\000\000\000\000\000\000\000\000\000\000\000";

	static uint8_t GetByte(const char* hex)
	{
		const auto hi = static_cast<unsigned char>(hex[0]);
		const auto lo = static_cast<unsigned char>(hex[1]);
		return static_cast<uint8_t>((hexdigits[hi] << 4) | hexdigits[lo]);
	}

} // anonymous namespace

/* ------------------------------------------------------------------ */
/*  Construction / destruction                                         */
/* ------------------------------------------------------------------ */

Memory::~Memory()
{
	if (vHandle)
		CloseDma();
}

/* ------------------------------------------------------------------ */
/*  DMA device configuration                                           */
/* ------------------------------------------------------------------ */

const char* Memory::GetDeviceTypeName(DmaDeviceType deviceType) const
{
	switch (deviceType)
	{
	case DmaDeviceType::FTD3XX:
		return "fpga";
	case DmaDeviceType::FTD3XXWU:
		return "ftd3xxwu";
	case DmaDeviceType::RAWTCP:
		return "rawtcp";
	case DmaDeviceType::HVSAVEDSTATE:
		return "hvsavedstate";
	default:
		return "unknown";
	}
}

std::string Memory::BuildDeviceString(DmaDeviceType deviceType) const
{
	switch (deviceType)
	{
	case DmaDeviceType::FTD3XX:
	case DmaDeviceType::FTD3XXWU:
		return "fpga://algo=0";
	case DmaDeviceType::RAWTCP:
	{
		const std::string host = Trim(m_rawTcpHost);
		if (host.empty())
			return "";
		if (StartsWithNoCase(host, "tcp://"))
			return host;
		return "tcp://" + host;
	}
	case DmaDeviceType::HVSAVEDSTATE:
		return "hvsavedstate";
	default:
		return "";
	}
}

bool Memory::LoadDmaDeviceConfig(const std::string& configPath)
{
	std::lock_guard<std::mutex> lifecycleLock(m_sessionLifecycleMutex);
	const std::filesystem::path path = ResolveConfigPath(configPath);
	std::ifstream file(path);
	if (!file)
	{
		LOG("[WARN] DMA device config not found at %s. Existing settings retained.\n",
			path.string().c_str());
		return false;
	}

	DmaDeviceType parsedDeviceType = DmaDeviceType::FTD3XX;
	std::string parsedRawTcpHost;
	bool configValid = true;
	bool sawDevice = false;
	std::string line;
	int lineNumber = 0;

	while (std::getline(file, line))
	{
		lineNumber++;

		const size_t hash = line.find('#');
		if (hash != std::string::npos)
			line.erase(hash);
		const size_t semicolon = line.find(';');
		if (semicolon != std::string::npos)
			line.erase(semicolon);

		line = Trim(line);
		if (line.empty())
			continue;

		const size_t equals = line.find('=');
		if (equals == std::string::npos)
		{
			LOG("[WARN] Ignoring malformed DMA config line %d: %s\n",
				lineNumber, line.c_str());
			configValid = false;
			continue;
		}

		const std::string key = ToLower(Trim(line.substr(0, equals)));
		const std::string value = Trim(line.substr(equals + 1));

		if (key == "device")
		{
			DmaDeviceType deviceType = DmaDeviceType::FTD3XX;
			if (!ParseDmaDeviceType(value, deviceType))
			{
				LOG("[WARN] Unknown DMA device '%s' in %s line %d. Valid values: fpga, ftd3xxwu, rawtcp, hvsavedstate.\n",
					value.c_str(), path.string().c_str(), lineNumber);
				configValid = false;
				continue;
			}
			parsedDeviceType = deviceType;
			sawDevice = true;
		}
		else if (key == "host" || key == "address")
		{
			parsedRawTcpHost = value;
		}
		else
		{
			LOG("[WARN] Ignoring unknown DMA config key '%s' in %s line %d.\n",
				key.c_str(), path.string().c_str(), lineNumber);
		}
	}

	if (!sawDevice) {
		LOG("[ERROR] DMA config has no valid device= entry.\n");
		configValid = false;
	}

	if (parsedDeviceType == DmaDeviceType::RAWTCP && Trim(parsedRawTcpHost).empty())
	{
		LOG("[ERROR] DMA config selected rawtcp but did not provide host=.\n");
		configValid = false;
	}
	if (!configValid) {
		LOG("[ERROR] Invalid DMA device configuration; existing settings retained.\n");
		return false;
	}
	m_deviceType = parsedDeviceType;
	m_rawTcpHost = parsedRawTcpHost;
	m_deviceString = BuildDeviceString(m_deviceType);

	LOG("[INFO] DMA device config loaded: %s -> %s\n",
		GetDeviceTypeName(m_deviceType),
		m_deviceString.empty() ? "<invalid>" : m_deviceString.c_str());
	return configValid;
}

/* ------------------------------------------------------------------ */
/*  Library loading                                                    */
/* ------------------------------------------------------------------ */

bool Memory::EnsureRuntimeLibrariesLoaded()
{
#if defined(UNLEASHED_DMA_SESSION_SELFTEST)
	// This test target supplies every SDK symbol and links no DMA library.
	return true;
#else
	std::lock_guard<std::mutex> lock(g_runtimeLibraryMutex);

	LOG("[INFO] Loading DMA runtime libraries for device=%s.\n",
		GetDeviceTypeName(m_deviceType));

	switch (m_deviceType)
	{
	case DmaDeviceType::FTD3XX:
		TryLoadLibrary("FTD3XX.dll", hFTD3XX, false);
		break;
	case DmaDeviceType::FTD3XXWU:
		TryLoadLibrary("FTD3XXWU.dll", hFTD3XXWU, false);
		break;
	case DmaDeviceType::RAWTCP:
		TryLoadLibrary("leechcore_device_rawtcp.dll", hLEECHCORE_DEVICE_RAWTCP, false);
		break;
	case DmaDeviceType::HVSAVEDSTATE:
		TryLoadLibrary("leechcore_device_hvsavedstate.dll", hLEECHCORE_DEVICE_HVSAVEDSTATE, false);
		break;
	default:
		break;
	}

	const bool vmmLoaded = TryLoadLibrary("vmm.dll", hVMM, true);
	const bool leechCoreLoaded = TryLoadLibrary("leechcore.dll", hLEECHCORE, true);
	TryLoadLibrary("Helper64.dll", hHELPER64, false);

	if (m_deviceType != DmaDeviceType::RAWTCP)
		TryLoadLibrary("leechcore_driver.dll", hLEECHCORE_DRIVER, false);

	if (!vmmLoaded || !leechCoreLoaded)
	{
		LOG("[ERROR] Failed to load required DMA runtime libraries.\n");
		return false;
	}

	LOG("[INFO] Runtime libraries loaded successfully.\n");
	return true;
#endif
}

/* ------------------------------------------------------------------ */
/*  Memory map dump (physical)                                         */
/* ------------------------------------------------------------------ */

bool Memory::DumpMemoryMap(const std::string& outputPath, bool debug)
{
	const std::string deviceString = m_deviceString.empty()
		? BuildDeviceString(m_deviceType)
		: m_deviceString;
	if (deviceString.empty())
	{
		LOG("[ERROR] Cannot dump memory map because DMA device string is empty.\n");
		return false;
	}

	LPSTR args[] = { (LPSTR)"-device", ToLpstr(deviceString.c_str()), (LPSTR)"-waitinitialize", (LPSTR)"-norefresh", (LPSTR)"", (LPSTR)"" };
	int argc = 4;
	if (debug)
	{
		args[argc++] = (LPSTR)"-v";
		args[argc++] = (LPSTR)"-printf";
	}

	VMM_HANDLE handle = VMMDLL_Initialize(argc, args);
	if (!handle)
	{
		LOG("[ERROR] Failed to open VMM handle for memory-map dump.\n");
		return false;
	}

	PVMMDLL_MAP_PHYSMEM pPhysMemMap = NULL;
	if (!VMMDLL_Map_GetPhysMem(handle, &pPhysMemMap))
	{
		LOG("[ERROR] Failed to query physical memory map.\n");
		VMMDLL_Close(handle);
		return false;
	}

	if (pPhysMemMap->dwVersion != VMMDLL_MAP_PHYSMEM_VERSION)
	{
		LOG("[ERROR] Unsupported physical memory map version.\n");
		VMMDLL_MemFree(pPhysMemMap);
		VMMDLL_Close(handle);
		return false;
	}

	if (pPhysMemMap->cMap == 0)
	{
		LOG("[ERROR] Physical memory map is empty.\n");
		VMMDLL_MemFree(pPhysMemMap);
		VMMDLL_Close(handle);
		return false;
	}

	std::stringstream sb;
	for (DWORD i = 0; i < pPhysMemMap->cMap; i++)
	{
		sb << std::hex << pPhysMemMap->pMap[i].pa << " "
			<< (pPhysMemMap->pMap[i].pa + pPhysMemMap->pMap[i].cb - 1)
			<< std::endl;
	}

	std::ofstream nFile(outputPath);
	nFile << sb.str();
	nFile.flush();
	nFile.close();
	const bool written = nFile.good();

	VMMDLL_MemFree(pPhysMemMap);
	LOG(written ? "[INFO] Physical memory map file written successfully.\n"
		: "[ERROR] Physical memory map file could not be written.\n");
	VMMDLL_Close(handle);
	return written;
}

/* ------------------------------------------------------------------ */
/*  FPGA configuration                                                 */
/* ------------------------------------------------------------------ */

static unsigned char abort2[4] = { 0x10, 0x00, 0x10, 0x00 };

bool Memory::SetFPGA()
{
	ULONG64 qwID = 0, qwVersionMajor = 0, qwVersionMinor = 0;
	if (!VMMDLL_ConfigGet(this->vHandle, LC_OPT_FPGA_FPGA_ID, &qwID) ||
		!VMMDLL_ConfigGet(this->vHandle, LC_OPT_FPGA_VERSION_MAJOR, &qwVersionMajor) ||
		!VMMDLL_ConfigGet(this->vHandle, LC_OPT_FPGA_VERSION_MINOR, &qwVersionMinor))
	{
		LOG("[WARN] FPGA metadata query failed. Proceeding with limited diagnostics.\n");
		return false;
	}

	LOG("[INFO] VMMDLL_ConfigGet: ID=%lli VERSION=%lli.%lli\n",
		qwID, qwVersionMajor, qwVersionMinor);

	if ((qwVersionMajor >= 4) && ((qwVersionMajor >= 5) || (qwVersionMinor >= 7)))
	{
		HANDLE handle;
		LC_CONFIG config = { .dwVersion = LC_CONFIG_VERSION, .szDevice = "existing" };
		handle = LcCreate(&config);
		if (!handle)
		{
			LOG("[ERROR] Failed to create FPGA configuration handle.\n");
			return false;
		}

		LcCommand(handle, LC_CMD_FPGA_CFGREGPCIE_MARKWR | 0x002,
			4, reinterpret_cast<PBYTE>(&abort2), NULL, NULL);
		LOG("[INFO] FPGA PCIe register flag reset completed.\n");
		LcClose(handle);
	}

	return true;
}

/* ------------------------------------------------------------------ */
/*  DMA initialization                                                 */
/* ------------------------------------------------------------------ */

bool Memory::InitDma(bool memMap, bool debug)
{
	return InitDma(m_deviceType, memMap, debug);
}

bool Memory::InitDma(DmaDeviceType deviceType, bool memMap, bool debug)
{
	std::lock_guard<std::mutex> lifecycleLock(m_sessionLifecycleMutex);
	if (m_cancelPending.load(std::memory_order_acquire))
		return false;
	if (DMA_INITIALIZED)
		return true;

	m_deviceType = deviceType;
	m_deviceString = BuildDeviceString(m_deviceType);
	if (m_deviceString.empty())
	{
		LOG("[ERROR] DMA device '%s' is missing required configuration.\n",
			GetDeviceTypeName(m_deviceType));
		return false;
	}

	if (!EnsureRuntimeLibrariesLoaded())
		return false;

	LOG("[INFO] DMA subsystem initialization started with device=%s (%s).\n",
		GetDeviceTypeName(m_deviceType), m_deviceString.c_str());

	bool useMemMap = memMap;
	while (!m_cancelPending.load(std::memory_order_acquire))
	{
		// Argument storage must survive until VMMDLL_Initialize has returned.
		std::string mapPath;
		LPSTR args[] = {
			(LPSTR)"", (LPSTR)"-device", ToLpstr(m_deviceString.c_str()), (LPSTR)"-norefresh",
			(LPSTR)"", (LPSTR)"", (LPSTR)"", (LPSTR)""
		};
		DWORD argc = 4;
		if (debug)
		{
			args[argc++] = (LPSTR)"-v";
			args[argc++] = (LPSTR)"-printf";
		}

		if (useMemMap)
		{
			mapPath = BuildMemMapCachePath();
			// The target host has a different boot lifecycle from this computer.
			// An explicit map request always obtains a new host map.
			LOG("[INFO] Physical memory map acquisition started.\n");
			if (this->DumpMemoryMap(mapPath, debug))
			{
				args[argc++] = (LPSTR)"-memmap";
				args[argc++] = ToLpstr(mapPath.c_str());
			}
			else
				LOG("[WARN] Memory map acquisition failed. Continuing without memory map.\n");
		}

		if (m_cancelPending.load(std::memory_order_acquire))
			return false;
		{
            RuntimeTrace::SdkSpan activity(RuntimeTrace::SdkPhase::Initialize);
            this->vHandle = VMMDLL_Initialize(argc, args);
            activity.Result(this->vHandle!=nullptr);
        }
		if (this->vHandle)
			break;

		if (useMemMap)
		{
			useMemMap = false;
			LOG("[WARN] DMA initialization with memory map failed; retrying without.\n");
			continue;
		}

		LOG("[ERROR] DMA initialization failed. Verify %s device availability.\n",
			GetDeviceTypeName(m_deviceType));
		return false;
	}

	if (m_cancelPending.load(std::memory_order_acquire)) {
		if (vHandle) VMMDLL_Close(vHandle);
		vHandle = nullptr;
		return false;
	}

	if (m_deviceType == DmaDeviceType::FTD3XX || m_deviceType == DmaDeviceType::FTD3XXWU)
	{
		ULONG64 FPGA_ID = 0, DEVICE_ID = 0;
		VMMDLL_ConfigGet(this->vHandle, LC_OPT_FPGA_FPGA_ID, &FPGA_ID);
		VMMDLL_ConfigGet(this->vHandle, LC_OPT_FPGA_DEVICE_ID, &DEVICE_ID);
		LOG("[INFO] FPGA identifier: %llu\n", FPGA_ID);
		LOG("[INFO] Device identifier: %llu\n", DEVICE_ID);

		if (!this->SetFPGA())
		{
			LOG("[ERROR] FPGA configuration step failed.\n");
			VMMDLL_Close(this->vHandle);
			this->vHandle = nullptr;
			return false;
		}
	}
	else
	{
		LOG("[INFO] Skipping FPGA-specific configuration for device=%s.\n",
			GetDeviceTypeName(m_deviceType));
	}

	// The active session owns cache maintenance. Never disable automatic
	// process refresh unless we can independently validate process lifetime.
	if (!InitializeProcessMonitor()) {
		LOG("[ERROR] Uncached process monitor unavailable; check matching info.db/symbol support.\n");
		VMMDLL_Close(vHandle);
		vHandle = nullptr;
		return false;
	}
	DMA_INITIALIZED = true;
	m_sessionAccess.Open();
	LOG("[INFO] DMA subsystem initialization completed successfully.\n");
	return true;
}

/* ------------------------------------------------------------------ */
/*  Attach to process                                                  */
/* ------------------------------------------------------------------ */

bool Memory::AttachToProcess(const std::string& process_name, bool applyCr3Fix)
{
	auto session = AcquireSession();
	if (!session)
		return false;
	std::lock_guard<std::recursive_mutex> processLock(m_processInfoMutex);
	if (m_cancelPending.load(std::memory_order_acquire))
		return false;
	if (!DMA_INITIALIZED)
	{
		LOG("[ERROR] AttachToProcess called before DMA initialization.\n");
		return false;
	}

	auto to_lower = [](std::string s) {
		std::transform(s.begin(), s.end(), s.begin(),
			[](unsigned char c) { return static_cast<char>(std::tolower(c)); });
		return s;
	};

	auto strip_exe = [&](std::string s) {
		std::string lower = to_lower(s);
		if (lower.size() > 4 && lower.substr(lower.size() - 4) == ".exe")
			return s.substr(0, s.size() - 4);
		return s;
	};

	auto canAccessProbeModules = [&](DWORD pid, const std::string& targetProcessName) -> bool {
		auto hasModule = [&](const std::string& moduleName) -> bool {
			return !moduleName.empty() &&
				VMMDLL_ProcessGetModuleBaseU(this->vHandle, pid, ToLpstr(moduleName.c_str())) != 0;
		};

		const std::string targetLower = to_lower(targetProcessName);
		const std::string targetBase = strip_exe(targetProcessName);
		const std::string targetBaseLower = to_lower(targetBase);
		const bool isCs2Target =
			targetLower == "cs2.exe" ||
			targetLower == "cs2" ||
			targetBaseLower == "cs2";

		if (isCs2Target)
			return hasModule("client.dll") || hasModule("engine2.dll");

		return hasModule(targetProcessName) ||
			(targetBase != targetProcessName && hasModule(targetBase));
	};

	const std::string target_name = to_lower(process_name);
	if (PROCESS_INITIALIZED && to_lower(current_process.process_name) == target_name)
	{
		if (applyCr3Fix && this->vHandle)
			VMMDLL_ConfigSet(this->vHandle, VMMDLL_OPT_REFRESH_ALL, 1);
		if (!applyCr3Fix || canAccessProbeModules(current_process.PID, process_name))
			return true;

		LOG("[INFO] Re-attaching %s because probe modules are not yet accessible.\n",
			process_name.c_str());
		PROCESS_INITIALIZED = false;
		m_currentPid.store(0, std::memory_order_release);
		current_process = {};
	}

	PROCESS_INITIALIZED = false;
	m_currentPid.store(0, std::memory_order_release);
	current_process = {};

	current_process.PID = GetPidFromName(process_name);
	if (!current_process.PID)
		return false;
	m_currentPid.store(current_process.PID, std::memory_order_release);

	current_process.process_name = process_name;

	if (applyCr3Fix)
	{
		if (!FixCr3())
			LOG("[WARN] CR3 remediation was not confirmed for %s.\n", process_name.c_str());
		else
			LOG("[INFO] CR3 remediation completed for %s.\n", process_name.c_str());
	}

	if (m_cancelPending.load(std::memory_order_acquire))
		return false;

	if (this->vHandle)
		VMMDLL_ConfigSet(this->vHandle, VMMDLL_OPT_REFRESH_ALL, 1);

	current_process.base_address = GetBaseDaddy(process_name);
	if (!current_process.base_address)
	{
		LOG("[WARN] Unable to resolve module base address for %s during attach. "
			"Continuing with PID-only attach.\n", process_name.c_str());
	}

	current_process.base_size = GetBaseSize(process_name);
	if (!current_process.base_size)
	{
		LOG("[WARN] Unable to resolve module image size for %s during attach. "
			"Continuing with PID-only attach.\n", process_name.c_str());
	}

	VMMDLL_PROCESS_INFORMATION information{};
	information.magic = VMMDLL_PROCESS_INFORMATION_MAGIC;
	information.wVersion = VMMDLL_PROCESS_INFORMATION_VERSION;
	SIZE_T informationSize = sizeof(information);
	ProcessLifetime::Sample lifetime;
	if (!VMMDLL_ProcessGetInformation(vHandle, current_process.PID, &information, &informationSize) ||
		!ReadProcessLifetime(information.win.vaEPROCESS, lifetime) ||
		!lifetime.Alive(current_process.PID, lifetime.created)) {
		LOG("[WARN] Target process lifetime could not be established; attach deferred.\n");
		m_currentPid.store(0, std::memory_order_release);
		current_process = {};
		return false;
	}
	current_process.eprocess = information.win.vaEPROCESS;
	current_process.created = lifetime.created;
	LOG("[INFO] Target process attached.\n");
	LOG("[INFO] Process: %s\n", process_name.c_str());
	LOG("[INFO] PID: %i\n", current_process.PID);
	LOG("[INFO] Base address: 0x%llx\n", current_process.base_address);
	LOG("[INFO] Image size: 0x%llx\n", current_process.base_size);

	PROCESS_INITIALIZED = true;
	return true;
}

/* ------------------------------------------------------------------ */
/*  Init convenience wrapper                                           */
/* ------------------------------------------------------------------ */

bool Memory::Init(std::string process_name, bool memMap, bool debug)
{
	if (!InitDma(memMap, debug))
		return false;
	return AttachToProcess(process_name, true);
}

void Memory::DetachProcess()
{
	std::lock_guard<std::recursive_mutex> processLock(m_processInfoMutex);
	PROCESS_INITIALIZED = false;
	m_currentPid.store(0, std::memory_order_release);
	current_process = {};
}

/* ------------------------------------------------------------------ */
/*  Close DMA                                                          */
/* ------------------------------------------------------------------ */

void Memory::CloseDma()
{
	std::lock_guard<std::mutex> lifecycleLock(m_sessionLifecycleMutex);
	DMA_INITIALIZED.store(false, std::memory_order_release);
	{
        RuntimeTrace::SdkSpan activity(RuntimeTrace::SdkPhase::SessionDrain);
        m_sessionAccess.CloseAndDrain();activity.Result(true);
    }
	std::lock_guard<std::mutex> lock(g_fixCr3Mutex);
	DetachProcess();
	{
		std::lock_guard<std::mutex> scatterLock(m_scatterMutex);
		// Admission is closed and every lease drained before native destruction.
		m_scatterHandles.clear();
	}
	if (vHandle)
	{
		VMMDLL_Close(vHandle);
		vHandle = nullptr;
	}
	m_cr3PluginsInitialized = false;
	m_processLayout = {};
	m_systemEprocess = m_systemCreated = 0;
}

bool Memory::ReadProcessLifetime(uint64_t address, ProcessLifetime::Sample& sample) const
{
	return ProcessLifetime::ReadSample(m_processLayout, address,
		[&](uint64_t location, void* destination, size_t size) {
			DWORD actual = 0;
			return VMMDLL_MemReadEx(vHandle, 4 | VMMDLL_PID_PROCESS_WITH_KERNELMEMORY,
				location, static_cast<PBYTE>(destination), static_cast<DWORD>(size), &actual,
				VMMDLL_FLAG_NOCACHE | VMMDLL_FLAG_NOPAGING | VMMDLL_FLAG_NOPAGING_IO) != FALSE && actual == size;
		}, sample);
}

bool Memory::InitializeProcessMonitor()
{
	m_processLayout = {};
	m_systemEprocess = m_systemCreated = 0;
	DWORD pidOffset = 0, createdOffset = 0, exitedOffset = 0;
	if (!VMMDLL_PdbTypeChildOffset(vHandle, (LPSTR)"nt", (LPSTR)"_EPROCESS", (LPSTR)"UniqueProcessId", &pidOffset) ||
		!VMMDLL_PdbTypeChildOffset(vHandle, (LPSTR)"nt", (LPSTR)"_EPROCESS", (LPSTR)"CreateTime", &createdOffset) ||
		!VMMDLL_PdbTypeChildOffset(vHandle, (LPSTR)"nt", (LPSTR)"_EPROCESS", (LPSTR)"ExitTime", &exitedOffset))
		return false;
	m_processLayout = { pidOffset, createdOffset, exitedOffset };
	VMMDLL_PROCESS_INFORMATION information{};
	information.magic = VMMDLL_PROCESS_INFORMATION_MAGIC;
	information.wVersion = VMMDLL_PROCESS_INFORMATION_VERSION;
	SIZE_T size = sizeof(information);
	ProcessLifetime::Sample sample;
	if (!VMMDLL_ProcessGetInformation(vHandle, 4, &information, &size) ||
		!ReadProcessLifetime(information.win.vaEPROCESS, sample) || !sample.Alive(4, sample.created))
		return false;
	m_systemEprocess = information.win.vaEPROCESS;
	m_systemCreated = sample.created;
	LOG("[INFO] Session refresh: explicit MEM/TLB maintenance; uncached lifetime monitor ready.\n");
	return true;
}

bool Memory::RefreshProcessMetadata(const char* reason) const
{
	auto session = AcquireSession();
	if (!session || !vHandle || m_cancelPending.load(std::memory_order_acquire))
		return false;
	const auto start = std::chrono::steady_clock::now();
	const bool ok = VMMDLL_ConfigSet(vHandle, VMMDLL_OPT_REFRESH_ALL, 1) != FALSE;
	LOG("[INFO] Process metadata refresh: reason=%s ok=%d elapsed_ms=%.3f\n", reason, ok,
		std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count());
	return ok;
}

bool Memory::CheckSessionHealth() const
{
	auto session = AcquireSession();
	if (!session || !vHandle || m_cancelPending.load(std::memory_order_acquire))
		return false;
	if (!VMMDLL_ConfigSet(vHandle, VMMDLL_OPT_REFRESH_FREQ_MEM_PARTIAL, 1) ||
		!VMMDLL_ConfigSet(vHandle, VMMDLL_OPT_REFRESH_FREQ_TLB_PARTIAL, 1))
		return false;
	ProcessLifetime::Sample sample;
	return ReadProcessLifetime(m_systemEprocess, sample) && sample.Alive(4, m_systemCreated);
}

bool Memory::CheckAttachedProcessAlive() const
{
	auto session = AcquireSession();
	if (!session || !vHandle || m_cancelPending.load(std::memory_order_acquire))
		return false;
	std::lock_guard<std::recursive_mutex> processLock(m_processInfoMutex);
	ProcessLifetime::Sample sample;
	return PROCESS_INITIALIZED && ReadProcessLifetime(current_process.eprocess, sample) &&
		sample.Alive(current_process.PID, current_process.created);
}

/* ------------------------------------------------------------------ */
/*  PID lookup                                                         */
/* ------------------------------------------------------------------ */

DWORD Memory::GetPidFromName(const std::string& process_name)
{
	auto session = AcquireSession();
	if (!session)
		return 0;
	if (!vHandle || m_cancelPending.load(std::memory_order_acquire))
		return 0;
	DWORD pid = 0;
	VMMDLL_PidGetFromName(this->vHandle, (LPSTR)process_name.c_str(), &pid);
	if (pid)
		return pid;

	// Try without ".exe" suffix.
	std::string base_name = process_name;
	if (base_name.size() > 4)
	{
		std::string tail = base_name.substr(base_name.size() - 4);
		std::transform(tail.begin(), tail.end(), tail.begin(),
			[](unsigned char c) { return static_cast<char>(std::tolower(c)); });
		if (tail == ".exe")
			base_name = base_name.substr(0, base_name.size() - 4);
	}

	if (base_name != process_name)
	{
		VMMDLL_PidGetFromName(this->vHandle, (LPSTR)base_name.c_str(), &pid);
		if (pid)
			return pid;
	}

	// Enumerate all processes and score candidates.
	PVMMDLL_PROCESS_INFORMATION process_info = NULL;
	DWORD total_processes = 0;
	if (!VMMDLL_ProcessGetInformationAll(this->vHandle, &process_info, &total_processes))
		return 0;

	auto to_lower = [](std::string s) {
		std::transform(s.begin(), s.end(), s.begin(),
			[](unsigned char c) { return static_cast<char>(std::tolower(c)); });
		return s;
	};

	const std::string target_a = to_lower(process_name);
	const std::string target_b = to_lower(base_name);
	const std::string target_c = to_lower(base_name + ".exe");

	struct Candidate
	{
		DWORD pid = 0;
		int score = 0;
		bool hasProcessModule = false;
		bool hasClientModule = false;
	};

	Candidate best = {};

	for (DWORD i = 0; i < total_processes; i++)
	{
		std::string short_name = process_info[i].szName ? process_info[i].szName : "";
		std::string long_name = process_info[i].szNameLong ? process_info[i].szNameLong : "";
		short_name = to_lower(short_name);
		long_name = to_lower(long_name);

		int score = 0;
		if (short_name == target_a || short_name == target_b || short_name == target_c)
			score = 4;
		else if (long_name == target_a || long_name == target_b || long_name == target_c)
			score = 3;
		else if (short_name.find(target_a) != std::string::npos ||
			long_name.find(target_a) != std::string::npos ||
			short_name.find(target_b) != std::string::npos ||
			long_name.find(target_b) != std::string::npos ||
			short_name.find(target_c) != std::string::npos ||
			long_name.find(target_c) != std::string::npos)
			score = 1;

		if (score == 0 || process_info[i].dwPID == 0)
			continue;

		const DWORD candidatePid = process_info[i].dwPID;
		const bool hasProcessModule =
			VMMDLL_ProcessGetModuleBaseU(this->vHandle, candidatePid, ToLpstr(process_name.c_str())) != 0 ||
			(base_name != process_name &&
				VMMDLL_ProcessGetModuleBaseU(this->vHandle, candidatePid, ToLpstr(base_name.c_str())) != 0);
		const bool hasClientModule =
			VMMDLL_ProcessGetModuleBaseU(this->vHandle, candidatePid, (LPSTR)"client.dll") != 0;

		const bool better =
			best.pid == 0 ||
			score > best.score ||
			(score == best.score && hasClientModule && !best.hasClientModule) ||
			(score == best.score && hasClientModule == best.hasClientModule &&
				hasProcessModule && !best.hasProcessModule) ||
			(score == best.score && hasClientModule == best.hasClientModule &&
				hasProcessModule == best.hasProcessModule && candidatePid > best.pid);

		if (better)
		{
			best.pid = candidatePid;
			best.score = score;
			best.hasProcessModule = hasProcessModule;
			best.hasClientModule = hasClientModule;
		}
	}

	VMMDLL_MemFree(process_info);
	if (best.pid)
		return best.pid;

	// All PIDs belong to the DMA host. A local process with the same name
	// cannot repair a missing host process or a broken host connection.
	return 0;
}

/* ------------------------------------------------------------------ */
/*  PID list                                                           */
/* ------------------------------------------------------------------ */

std::vector<int> Memory::GetPidListFromName(const std::string& name)
{
	auto session = AcquireSession();
	if (!session || !vHandle)
		return {};
	PVMMDLL_PROCESS_INFORMATION process_info = NULL;
	DWORD total_processes = 0;
	std::vector<int> list = {};

	if (!VMMDLL_ProcessGetInformationAll(this->vHandle, &process_info, &total_processes))
	{
		LOG("[!] Failed to get process list\n");
		return list;
	}

	for (size_t i = 0; i < total_processes; i++)
	{
		auto process = process_info[i];
		if (process.szNameLong && strstr(process.szNameLong, name.c_str()))
			list.push_back(process.dwPID);
	}

	VMMDLL_MemFree(process_info);
	return list;
}

Memory::ProcessLookupResult Memory::FindProcessByNamePrefix(const std::string& namePrefix)
{
	auto session = AcquireSession();
	if (!session || !vHandle)
		return {};
	ProcessLookupResult result = {};
	if (namePrefix.empty())
		return result;

	PVMMDLL_PROCESS_INFORMATION process_info = NULL;
	DWORD total_processes = 0;
	if (this->vHandle &&
		VMMDLL_ProcessGetInformationAll(this->vHandle, &process_info, &total_processes))
	{
		for (DWORD i = 0; i < total_processes; i++)
		{
			const auto& process = process_info[i];
			if (process.dwPID == 0)
				continue;

			const std::string shortName = process.szName ? process.szName : "";
			const std::string longName = process.szNameLong ? process.szNameLong : "";
			if (StartsWithNoCase(shortName, namePrefix) || StartsWithNoCase(longName, namePrefix))
			{
				result.pid = process.dwPID;
				result.name = !longName.empty() ? longName : shortName;
				break;
			}
		}

		VMMDLL_MemFree(process_info);
		if (result)
			return result;
	}

	return result;
}

/* ------------------------------------------------------------------ */
/*  Module list                                                        */
/* ------------------------------------------------------------------ */

std::vector<std::string> Memory::GetModuleList(const std::string& process_name)
{
	auto session = AcquireSession();
	if (!session || !vHandle)
		return {};
	std::lock_guard<std::recursive_mutex> processLock(m_processInfoMutex);
	std::vector<std::string> list = {};
	PVMMDLL_MAP_MODULE module_info = NULL;
	if (!VMMDLL_Map_GetModuleU(this->vHandle, current_process.PID, &module_info,
		VMMDLL_MODULE_FLAG_NORMAL))
	{
		LOG("[!] Failed to get module list\n");
		return list;
	}

	for (size_t i = 0; i < module_info->cMap; i++)
	{
		auto module = module_info->pMap[i];
		list.push_back(module.uszText);
	}

	VMMDLL_MemFree(module_info);
	return list;
}

/* ------------------------------------------------------------------ */
/*  Process information                                                */
/* ------------------------------------------------------------------ */

VMMDLL_PROCESS_INFORMATION Memory::GetProcessInformation()
{
	auto session = AcquireSession();
	if (!session || !vHandle)
		return {};
	std::lock_guard<std::recursive_mutex> processLock(m_processInfoMutex);
	VMMDLL_PROCESS_INFORMATION info = {};
	SIZE_T process_information = sizeof(VMMDLL_PROCESS_INFORMATION);
	ZeroMemory(&info, sizeof(VMMDLL_PROCESS_INFORMATION));
	info.magic = VMMDLL_PROCESS_INFORMATION_MAGIC;
	info.wVersion = VMMDLL_PROCESS_INFORMATION_VERSION;

	if (!VMMDLL_ProcessGetInformation(this->vHandle, current_process.PID,
		&info, &process_information))
	{
		LOG("[!] Failed to find process information\n");
		return {};
	}

	LOG("[+] Found process information\n");
	return info;
}

/* ------------------------------------------------------------------ */
/*  Module base address / size                                         */
/* ------------------------------------------------------------------ */

size_t Memory::GetBaseDaddy(std::string module_name)
{
	auto session = AcquireSession();
	if (!session || !vHandle)
		return 0;
	std::lock_guard<std::recursive_mutex> processLock(m_processInfoMutex);
	ULONG64 base = VMMDLL_ProcessGetModuleBaseU(this->vHandle,
		current_process.PID, module_name.data());
	if (base)
	{
		LOG("[INFO] Base address resolved for %s at 0x%p.\n",
			module_name.c_str(), (void*)base);
		return static_cast<size_t>(base);
	}

	// Fallback: map lookup by wide-string name.
	std::wstring str(module_name.begin(), module_name.end());
	PVMMDLL_MAP_MODULEENTRY module_info = nullptr;
	if (VMMDLL_Map_GetModuleFromNameW(this->vHandle, current_process.PID,
		const_cast<LPWSTR>(str.c_str()), &module_info, VMMDLL_MODULE_FLAG_NORMAL))
	{
		LOG("[INFO] Base address resolved for %s at 0x%p.\n",
			module_name.c_str(), reinterpret_cast<void*>(module_info->vaBase));
		const auto result = module_info->vaBase;
		VMMDLL_MemFree(module_info);
		return result;
	}

	return 0;
}

size_t Memory::GetBaseSize(std::string module_name)
{
	auto session = AcquireSession();
	if (!session || !vHandle)
		return 0;
	std::lock_guard<std::recursive_mutex> processLock(m_processInfoMutex);
	std::wstring str(module_name.begin(), module_name.end());
	PVMMDLL_MAP_MODULEENTRY module_info = nullptr;
	auto bResult = VMMDLL_Map_GetModuleFromNameW(this->vHandle,
		current_process.PID, const_cast<LPWSTR>(str.c_str()),
		&module_info, VMMDLL_MODULE_FLAG_NORMAL);
	if (bResult)
	{
		LOG("[INFO] Image size resolved for %s at 0x%llx.\n",
			module_name.c_str(),
			static_cast<unsigned long long>(module_info->cbImageSize));
		const auto result = module_info->cbImageSize;
		VMMDLL_MemFree(module_info);
		return result;
	}
	return 0;
}

/* ------------------------------------------------------------------ */
/*  CR3 fix                                                            */
/* ------------------------------------------------------------------ */

bool Memory::FixCr3()
{
	auto session = AcquireSession();
	if (!session || !vHandle)
		return false;
	std::lock_guard<std::recursive_mutex> processLock(m_processInfoMutex);
	std::lock_guard<std::mutex> lock(g_fixCr3Mutex);

	auto to_lower = [](std::string s) {
		std::transform(s.begin(), s.end(), s.begin(),
			[](unsigned char c) { return static_cast<char>(std::tolower(c)); });
		return s;
	};

	auto strip_exe = [&](std::string s) {
		std::string lower = to_lower(s);
		if (lower.size() > 4 && lower.substr(lower.size() - 4) == ".exe")
			return s.substr(0, s.size() - 4);
		return s;
	};

	auto canAccessProbeModules = [&](DWORD pid) -> bool {
		auto hasModule = [&](const std::string& moduleName) -> bool {
			if (moduleName.empty()) return false;
			uint64_t base = VMMDLL_ProcessGetModuleBaseU(this->vHandle, pid, ToLpstr(moduleName.c_str()));
			if (base == 0) return false;

			IMAGE_DOS_HEADER dos = { 0 };
			DWORD read_size = 0;
			if (!VMMDLL_MemReadEx(this->vHandle, pid, base, (PBYTE)&dos,
				sizeof(IMAGE_DOS_HEADER), &read_size, VMMDLL_FLAG_NOCACHE))
				return false;
			return dos.e_magic == 0x5A4D;
		};

		const std::string targetLower = to_lower(current_process.process_name);
		const std::string targetBase = strip_exe(current_process.process_name);
		const std::string targetBaseLower = to_lower(targetBase);
		const bool isCs2Target =
			targetLower == "cs2.exe" ||
			targetLower == "cs2" ||
			targetBaseLower == "cs2";

		if (isCs2Target)
			return hasModule("client.dll") || hasModule("engine2.dll");

		return hasModule(current_process.process_name) ||
			(targetBase != current_process.process_name && hasModule(targetBase));
	};

	if (this->vHandle)
		VMMDLL_ConfigSet(this->vHandle, VMMDLL_OPT_REFRESH_ALL, 1);

	if (canAccessProbeModules(current_process.PID))
		return true;

	if (!m_cr3PluginsInitialized)
	{
		if (!VMMDLL_InitializePlugins(this->vHandle))
		{
			LOG("[ERROR] VMMDLL_InitializePlugins call failed.\n");
			return false;
		}
		m_cr3PluginsInitialized = true;
	}

	for (unsigned wait = 0; wait < 5; ++wait) {
		if (m_cancelPending.load(std::memory_order_acquire)) return false;
		std::this_thread::sleep_for(std::chrono::milliseconds(100));
	}

	const auto progressDeadline = std::chrono::steady_clock::now() + std::chrono::seconds(30);
	bool progressReady = false;
	while (!m_cancelPending.load(std::memory_order_acquire) &&
		std::chrono::steady_clock::now() < progressDeadline)
	{
		BYTE bytes[4] = { 0 };
		DWORD i = 0;
		auto nt = VMMDLL_VfsReadW(this->vHandle,
			const_cast<LPWSTR>(L"\\misc\\procinfo\\progress_percent.txt"),
			bytes, 3, &i, 0);
		if (nt == VMMDLL_STATUS_SUCCESS && atoi(reinterpret_cast<LPSTR>(bytes)) == 100)
		{
			progressReady = true;
			break;
		}
		std::this_thread::sleep_for(std::chrono::milliseconds(100));
	}

	if (!progressReady)
	{
		LOG("[WARN] CR3 plugin progress stopped or timed out.\n");
		return false;
	}

	VMMDLL_VFS_FILELIST2 VfsFileList;
	VfsFileList.dwVersion = VMMDLL_VFS_FILELIST_VERSION;
	VfsFileList.h = 0;
	VfsFileList.pfnAddDirectory = 0;
	VfsFileList.pfnAddFile = cbAddFile;
	cbSize.store(0x80000, std::memory_order_relaxed);

	if (!VMMDLL_VfsListU(this->vHandle,
		const_cast<LPSTR>("\\misc\\procinfo\\"), &VfsFileList))
		return false;

	const uint64_t dtbTextSize = cbSize.load(std::memory_order_relaxed);
	if (dtbTextSize == 0 || dtbTextSize > 16ULL * 1024ULL * 1024ULL)
	{
		LOG("[WARN] Invalid CR3 DTB list size: %llu.\n",
			static_cast<unsigned long long>(dtbTextSize));
		return false;
	}

	const size_t buffer_size = static_cast<size_t>(dtbTextSize) + 1u;
	std::vector<BYTE> bytes(buffer_size, 0);
	DWORD j = 0;
	auto nt = VMMDLL_VfsReadW(this->vHandle,
		const_cast<LPWSTR>(L"\\misc\\procinfo\\dtb.txt"),
		bytes.data(), static_cast<DWORD>(buffer_size - 1u), &j, 0);
	if (nt != VMMDLL_STATUS_SUCCESS)
		return false;

	std::vector<uint64_t> possible_dtbs = {};
	const size_t textSize = std::min(static_cast<size_t>(j), buffer_size - 1u);
	std::string lines(reinterpret_cast<char*>(bytes.data()), textSize);
	std::istringstream iss(lines);
	std::string line = "";

	while (std::getline(iss, line))
	{
		Info info = {};
		std::istringstream info_ss(line);
		if (info_ss >> std::hex >> info.index >> std::dec >> info.process_id
			>> std::hex >> info.dtb >> info.kernelAddr >> info.name)
		{
			if (info.process_id == 0)
				possible_dtbs.push_back(info.dtb);
			if (current_process.process_name.find(info.name) != std::string::npos)
				possible_dtbs.push_back(info.dtb);
		}
	}

	for (size_t i = 0; i < possible_dtbs.size(); i++)
	{
		if (m_cancelPending.load(std::memory_order_acquire)) return false;
		auto dtb = possible_dtbs[i];
		VMMDLL_ConfigSet(this->vHandle, VMMDLL_OPT_PROCESS_DTB | current_process.PID, dtb);
		VMMDLL_ConfigSet(this->vHandle, VMMDLL_OPT_REFRESH_ALL, 1);
		if (canAccessProbeModules(current_process.PID))
		{
			LOG("[INFO] DTB remediation completed.\n");
			return true;
		}
	}

	LOG("[WARN] DTB remediation did not find a valid candidate.\n");
	VMMDLL_ConfigSet(this->vHandle, VMMDLL_OPT_PROCESS_DTB | current_process.PID, 0);
	VMMDLL_ConfigSet(this->vHandle, VMMDLL_OPT_REFRESH_ALL, 1);
	return false;
}

/* ------------------------------------------------------------------ */
/*  Dump memory (PE)                                                   */
/* ------------------------------------------------------------------ */

bool Memory::DumpMemory(uintptr_t address, std::string path)
{
	LOG("[!] Memory dumping currently does not rebuild the IAT table, "
		"imports will be missing from the dump.\n");

	IMAGE_DOS_HEADER dos{};
	if (!Read(address, &dos, sizeof(IMAGE_DOS_HEADER)))
		return false;

	if (dos.e_magic != 0x5A4D)
	{
		LOG("[-] Invalid PE Header\n");
		return false;
	}

	if (dos.e_lfanew <= 0 || dos.e_lfanew > 0x100000)
	{
		LOG("[-] Invalid PE header offset\n");
		return false;
	}

	IMAGE_NT_HEADERS64 nt{};
	if (!Read(address + static_cast<uintptr_t>(dos.e_lfanew), &nt, sizeof(IMAGE_NT_HEADERS64)))
		return false;

	if (nt.Signature != IMAGE_NT_SIGNATURE || nt.OptionalHeader.Magic != IMAGE_NT_OPTIONAL_HDR64_MAGIC)
	{
		LOG("[-] Failed signature check\n");
		return false;
	}

	const size_t target_size = nt.OptionalHeader.SizeOfImage;
	if (target_size == 0 || target_size > static_cast<size_t>(std::numeric_limits<DWORD>::max()))
	{
		LOG("[-] Invalid PE image size\n");
		return false;
	}
	if (static_cast<size_t>(dos.e_lfanew) + sizeof(IMAGE_NT_HEADERS64) > target_size)
	{
		LOG("[-] PE headers exceed image bounds\n");
		return false;
	}

	const size_t sectionHeadersOffset =
		static_cast<size_t>(dos.e_lfanew) +
		FIELD_OFFSET(IMAGE_NT_HEADERS64, OptionalHeader) +
		nt.FileHeader.SizeOfOptionalHeader;
	const size_t sectionHeadersSize =
		static_cast<size_t>(nt.FileHeader.NumberOfSections) * sizeof(IMAGE_SECTION_HEADER);
	if (sectionHeadersOffset > target_size || sectionHeadersSize > target_size - sectionHeadersOffset)
	{
		LOG("[-] PE section headers exceed image bounds\n");
		return false;
	}

	auto target = std::unique_ptr<uint8_t[]>(new uint8_t[target_size]);

	if (!Read(address, target.get(), target_size))
		return false;

	auto nt_header = reinterpret_cast<PIMAGE_NT_HEADERS64>(target.get() + dos.e_lfanew);
	auto sections = reinterpret_cast<PIMAGE_SECTION_HEADER>(target.get() + sectionHeadersOffset);

	for (size_t i = 0; i < nt.FileHeader.NumberOfSections; i++, sections++)
	{
		LOG("[!] Rewriting file offsets at 0x%08X size 0x%08X\n",
			sections->VirtualAddress, sections->Misc.VirtualSize);
		sections->PointerToRawData = sections->VirtualAddress;
		sections->SizeOfRawData = sections->Misc.VirtualSize;
	}

	const auto& debugDirectory = nt_header->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_DEBUG];
	if (debugDirectory.VirtualAddress != 0 &&
		target_size >= sizeof(IMAGE_DEBUG_DIRECTORY) &&
		static_cast<size_t>(debugDirectory.VirtualAddress) <= target_size - sizeof(IMAGE_DEBUG_DIRECTORY))
	{
		auto debug = reinterpret_cast<PIMAGE_DEBUG_DIRECTORY>(target.get() + debugDirectory.VirtualAddress);
		debug->PointerToRawData = debug->AddressOfRawData;
	}

	const auto widePath = Utf8ToWide(path);
	const auto dumped_file = CreateFileW(widePath.c_str(), GENERIC_WRITE, 0, NULL,
		CREATE_ALWAYS, FILE_ATTRIBUTE_COMPRESSED, NULL);
	if (dumped_file == INVALID_HANDLE_VALUE)
	{
		LOG("[!] Failed creating file: %i\n", GetLastError());
		return false;
	}

	if (!WriteFile(dumped_file, target.get(), static_cast<DWORD>(target_size), NULL, NULL))
	{
		LOG("[!] Failed writing file: %i\n", GetLastError());
		CloseHandle(dumped_file);
		return false;
	}

	LOG("[+] Successfully dumped memory at %s\n", path.c_str());
	CloseHandle(dumped_file);
	return true;
}

/* ------------------------------------------------------------------ */
/*  Signature scan                                                     */
/* ------------------------------------------------------------------ */

uint64_t Memory::FindSignature(const char* signature, uint64_t range_start,
	uint64_t range_end, int PID)
{
	auto session = AcquireSession();
	if (!session || !vHandle || m_cancelPending.load(std::memory_order_acquire))
		return 0;
	if (!signature || signature[0] == '\0' || range_start >= range_end)
		return 0;

	if (PID == 0)
		PID = m_currentPid.load(std::memory_order_acquire);

	std::vector<uint8_t> buffer(range_end - range_start);
	if (!VMMDLL_MemReadEx(this->vHandle, PID, range_start,
		buffer.data(), static_cast<DWORD>(buffer.size()), 0, VMMDLL_FLAG_NOCACHE))
		return 0;

	const char* pat = signature;
	uint64_t first_match = 0;
	for (uint64_t i = range_start; i < range_end; i++)
	{
		if (*pat == '?' || buffer[i - range_start] == GetByte(pat))
		{
			if (!first_match)
				first_match = i;

			if (!pat[2])
				break;

			pat += 3;
		}
		else
		{
			if (first_match)
			{
				i = first_match;            // backtrack so next iteration tries first_match+1
				first_match = 0;
			}
			pat = signature;
		}
	}

	return first_match;
}

/* ------------------------------------------------------------------ */
/*  Direct read                                                        */
/* ------------------------------------------------------------------ */

static constexpr DWORD VMM_READ_FLAGS =
	VMMDLL_FLAG_NOCACHE |
	VMMDLL_FLAG_NOPAGING |
	VMMDLL_FLAG_ZEROPAD_ON_FAIL |
	VMMDLL_FLAG_NOPAGING_IO;

bool Memory::Read(uintptr_t address, void* buffer, size_t size) const
{
	auto session = AcquireSession();
	const auto start = std::chrono::steady_clock::now();
	if (!session || !vHandle || m_cancelPending.load(std::memory_order_acquire))
	{
		Diagnostics::RecordDmaRead(false, std::chrono::steady_clock::duration::zero());
		return false;
	}

	DWORD read_size = 0;
	RuntimeTrace::SdkSpan activity(RuntimeTrace::SdkPhase::DirectRead,address);
	const bool readOk = VMMDLL_MemReadEx(this->vHandle, m_currentPid.load(std::memory_order_acquire), address,
		static_cast<PBYTE>(buffer), static_cast<DWORD>(size),
		&read_size, VMM_READ_FLAGS) != FALSE;
	const bool success = readOk && read_size == size;
	activity.Result(success);
	const auto elapsedUs = std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now()-start).count();
	if(elapsedUs>=2000)RuntimeTrace::Emit(RuntimeTrace::SlowRead,elapsedUs,address,size,
		static_cast<uint64_t>(Diagnostics::ScopedDmaCallsite::Current()),nullptr,0);
	Diagnostics::RecordDmaRead(success, std::chrono::steady_clock::now() - start);

	if (!readOk)
	{
		LOG("[!] Failed to read Memory at 0x%p\n", (void*)address);
		return false;
	}
	return success;
}

bool Memory::ReadFreshExact(uintptr_t address, void* buffer, size_t size) const
{
	auto session = AcquireSession();
	const auto start = std::chrono::steady_clock::now();
	if (!session || !vHandle || !buffer || !size || size > MAXDWORD ||
		m_cancelPending.load(std::memory_order_acquire))
		return false;
	DWORD actual = 0;
	RuntimeTrace::SdkSpan activity(RuntimeTrace::SdkPhase::FreshRead,address);
	const bool success = VMMDLL_MemReadEx(vHandle, m_currentPid.load(std::memory_order_acquire), address,
		static_cast<PBYTE>(buffer), static_cast<DWORD>(size), &actual,
		VMMDLL_FLAG_NOCACHE | VMMDLL_FLAG_NOPAGING | VMMDLL_FLAG_NOPAGING_IO) != FALSE && actual == size;
	activity.Result(success);
	Diagnostics::RecordDmaRead(success, std::chrono::steady_clock::now() - start);
	return success;
}

bool Memory::Read(uintptr_t address, void* buffer, size_t size, int pid) const
{
	auto session = AcquireSession();
	const auto start = std::chrono::steady_clock::now();
	if (!session || !vHandle || m_cancelPending.load(std::memory_order_acquire))
	{
		Diagnostics::RecordDmaRead(false, std::chrono::steady_clock::duration::zero());
		return false;
	}

	DWORD read_size = 0;
	const bool readOk = VMMDLL_MemReadEx(this->vHandle, pid, address,
		static_cast<PBYTE>(buffer), static_cast<DWORD>(size),
		&read_size, VMM_READ_FLAGS) != FALSE;
	const bool success = readOk && read_size == size;
	const auto elapsedUs = std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now()-start).count();
	if(elapsedUs>=2000)RuntimeTrace::Emit(RuntimeTrace::SlowRead,elapsedUs,address,size,
		static_cast<uint64_t>(Diagnostics::ScopedDmaCallsite::Current()),nullptr,0);
	Diagnostics::RecordDmaRead(success, std::chrono::steady_clock::now() - start);

	if (!readOk)
	{
		LOG("[!] Failed to read Memory at 0x%p (PID %d)\n", (void*)address, pid);
		return false;
	}
	return success;
}

/* ------------------------------------------------------------------ */
/*  Direct write                                                       */
/* ------------------------------------------------------------------ */

bool Memory::Write(uintptr_t address, const void* buffer, size_t size) const
{
#ifdef UNLEASHED_OBSERVATION_ONLY
    return false;
#endif
	auto session = AcquireSession();
	if (!session || m_cancelPending.load(std::memory_order_acquire))
		return false;
	_is_invalid(vHandle);

	if (!VMMDLL_MemWrite(this->vHandle, m_currentPid.load(std::memory_order_acquire), address,
		reinterpret_cast<PBYTE>(const_cast<void*>(buffer)),
		static_cast<DWORD>(size)))
	{
		LOG("[!] Failed to write Memory at 0x%p\n", (void*)address);
		return false;
	}
	return true;
}

bool Memory::Write(uintptr_t address, const void* buffer, size_t size, int pid) const
{
#ifdef UNLEASHED_OBSERVATION_ONLY
    return false;
#endif
	auto session = AcquireSession();
	if (!session || m_cancelPending.load(std::memory_order_acquire))
		return false;
	_is_invalid(vHandle);

	if (!VMMDLL_MemWrite(this->vHandle, pid, address,
		reinterpret_cast<PBYTE>(const_cast<void*>(buffer)),
		static_cast<DWORD>(size)))
	{
		LOG("[!] Failed to write Memory at 0x%p (PID %d)\n", reinterpret_cast<void*>(address), pid);
		return false;
	}
	return true;
}

/* ------------------------------------------------------------------ */
/*  Cache prefetch                                                     */
/* ------------------------------------------------------------------ */

bool Memory::PrefetchPages(const std::vector<uint64_t>& pageAddresses, int pid) const
{
	auto session = AcquireSession();
	if (!session || !vHandle || m_cancelPending.load(std::memory_order_acquire))
		return false;
	if (pageAddresses.empty())
		return true;

	const auto start = std::chrono::steady_clock::now();
	if (!vHandle)
	{
		Diagnostics::RecordDmaRead(false, std::chrono::steady_clock::duration::zero());
		return false;
	}

	if (pid == 0)
		pid = m_currentPid.load(std::memory_order_acquire);
	if (pid == 0)
	{
		Diagnostics::RecordDmaRead(false, std::chrono::steady_clock::duration::zero());
		return false;
	}

	const DWORD count = static_cast<DWORD>((std::min)(
		pageAddresses.size(),
		static_cast<size_t>((std::numeric_limits<DWORD>::max)())));
	const bool ok = VMMDLL_MemPrefetchPages(
		this->vHandle,
		pid,
		const_cast<PULONG64>(pageAddresses.data()),
		count) != FALSE;
	Diagnostics::RecordDmaRead(ok, std::chrono::steady_clock::now() - start);
	return ok;
}

/* ------------------------------------------------------------------ */
/*  Scatter read                                                       */
/* ------------------------------------------------------------------ */

std::shared_ptr<Memory::ScatterState> Memory::FindScatterHandle(VMMDLL_SCATTER_HANDLE handle) const
{
    std::lock_guard<std::mutex> lock(m_scatterMutex);
    const auto it=m_scatterHandles.find(handle);
    return it==m_scatterHandles.end()?nullptr:it->second;
}

VMMDLL_SCATTER_HANDLE Memory::CreateScatterHandle() const
{
    return CreateScatterHandle(m_currentPid.load(std::memory_order_acquire));
}

VMMDLL_SCATTER_HANDLE Memory::CreateScatterHandle(int pid) const
{
    auto session=AcquireSession();
    if(!session || !vHandle || m_cancelPending.load(std::memory_order_acquire))return nullptr;
    auto state=std::make_shared<ScatterState>();
    RuntimeTrace::SdkSpan activity(RuntimeTrace::SdkPhase::ScatterCreate);
    state->native=VMMDLL_Scatter_Initialize(vHandle,pid,VMMDLL_FLAG_NOCACHE);
    activity.Result(state->native!=nullptr);
    if(!state->native || m_cancelPending.load(std::memory_order_acquire) || !SessionActivity().admissionOpen)return nullptr;
    std::lock_guard<std::mutex> lock(m_scatterMutex);
    if(m_nextScatterToken==0)return nullptr;
    const auto token=reinterpret_cast<VMMDLL_SCATTER_HANDLE>(m_nextScatterToken++);
    m_scatterHandles.emplace(token,std::move(state));
    return token;
}

void Memory::CloseScatterHandle(VMMDLL_SCATTER_HANDLE handle)
{
    auto session=AcquireSession();
    if(!session || !handle)return;
    std::shared_ptr<ScatterState> state;
    {
        std::lock_guard<std::mutex> lock(m_scatterMutex);
        const auto it=m_scatterHandles.find(handle);
        if(it==m_scatterHandles.end())return;
        state=it->second;m_scatterHandles.erase(it);
    }
    std::lock_guard<std::mutex> callLock(state->mutex);
    if(state->native)VMMDLL_Scatter_CloseHandle(std::exchange(state->native,nullptr));
}

bool Memory::AddScatterReadRequest(VMMDLL_SCATTER_HANDLE handle,uint64_t address,void* buffer,size_t size)
{
    return AddScatterReadRequest(handle,address,buffer,size,nullptr);
}

bool Memory::AddScatterReadRequest(VMMDLL_SCATTER_HANDLE handle,uint64_t address,void* buffer,size_t size,DWORD* bytesRead)
{
    if(bytesRead)*bytesRead=0;
    auto session=AcquireSession();
    if(!session || !handle || m_cancelPending.load(std::memory_order_acquire))return false;
    auto state=FindScatterHandle(handle);if(!state)return false;
    const auto started=RuntimeTrace::NowUs();
    std::lock_guard<std::mutex> callLock(state->mutex);
    const auto waited=RuntimeTrace::NowUs()-started;
    if(waited>=1000)RuntimeTrace::Emit(RuntimeTrace::ReadWait,waited,reinterpret_cast<uint64_t>(handle));
    if(!state->native || m_cancelPending.load(std::memory_order_acquire) || !SessionActivity().admissionOpen)return false;
    return VMMDLL_Scatter_PrepareEx(state->native,address,size,static_cast<PBYTE>(buffer),bytesRead)!=FALSE;
}

bool Memory::ExecuteReadScatter(VMMDLL_SCATTER_HANDLE handle,int pid)
{
    auto session=AcquireSession();
    if(!session || !handle || m_cancelPending.load(std::memory_order_acquire)) {
        Diagnostics::RecordDmaRead(false,std::chrono::steady_clock::duration::zero());return false;
    }
    auto state=FindScatterHandle(handle);if(!state)return false;
    const auto started=RuntimeTrace::NowUs();
    std::lock_guard<std::mutex> callLock(state->mutex);
    const auto waited=RuntimeTrace::NowUs()-started;
    if(waited>=1000)RuntimeTrace::Emit(RuntimeTrace::ReadWait,waited,reinterpret_cast<uint64_t>(handle));
    if(!state->native || m_cancelPending.load(std::memory_order_acquire) || !SessionActivity().admissionOpen)return false;
    const auto native=state->native;
    if(pid==0)pid=m_currentPid.load(std::memory_order_acquire);
    const auto start=std::chrono::steady_clock::now();
    const bool executeOk=[&] {
        RuntimeTrace::SdkSpan activity(RuntimeTrace::SdkPhase::ScatterExecute,reinterpret_cast<uint64_t>(handle));
        const bool ok=VMMDLL_Scatter_ExecuteRead(native)!=FALSE;activity.Result(ok);return ok;
    }();
    const auto elapsedUs=std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now()-start).count();
    if(elapsedUs>=2000)RuntimeTrace::Emit(RuntimeTrace::SlowRead,elapsedUs,0,0,static_cast<uint64_t>(Diagnostics::ScopedDmaCallsite::Current()),nullptr,0);
    // Even a cancelled in-flight execute must clear its original buffers safely.
    const bool clearOk=[&] {
        RuntimeTrace::SdkSpan activity(RuntimeTrace::SdkPhase::ScatterClear,reinterpret_cast<uint64_t>(handle));
        const bool ok=VMMDLL_Scatter_Clear(native,pid,VMMDLL_FLAG_NOCACHE)!=FALSE;activity.Result(ok);return ok;
    }();
    Diagnostics::RecordDmaRead(executeOk && clearOk,std::chrono::steady_clock::now()-start);
    return executeOk && clearOk;
}

/* ------------------------------------------------------------------ */
/*  Export table                                                       */
/* ------------------------------------------------------------------ */

uintptr_t Memory::GetExportTableAddress(std::string import,
	std::string process, std::string module)
{
	auto session = AcquireSession();
	if (!session || !vHandle)
		return 0;
	PVMMDLL_MAP_EAT eat_map = NULL;
	PVMMDLL_MAP_EATENTRY export_entry = NULL;
	bool result = VMMDLL_Map_GetEATU(this->vHandle,
		this->GetPidFromName(process),
		const_cast<LPSTR>(module.c_str()), &eat_map);
	if (!result)
	{
		LOG("[!] Failed to get Export Table\n");
		return 0;
	}

	if (eat_map->dwVersion != VMMDLL_MAP_EAT_VERSION)
	{
		VMMDLL_MemFree(eat_map);
		LOG("[!] Invalid VMM Map Version\n");
		return 0;
	}

	uintptr_t addr = 0;
	for (DWORD i = 0; i < eat_map->cMap; i++)
	{
		export_entry = eat_map->pMap + i;
		if (strcmp(export_entry->uszFunction, import.c_str()) == 0)
		{
			addr = export_entry->vaFunction;
			break;
		}
	}

	VMMDLL_MemFree(eat_map);
	return addr;
}

/* ------------------------------------------------------------------ */
/*  Import table                                                       */
/* ------------------------------------------------------------------ */

uintptr_t Memory::GetImportTableAddress(std::string import,
	std::string process, std::string module)
{
	auto session = AcquireSession();
	if (!session || !vHandle)
		return 0;
	PVMMDLL_MAP_IAT iat_map = NULL;
	PVMMDLL_MAP_IATENTRY import_entry = NULL;
	bool result = VMMDLL_Map_GetIATU(this->vHandle,
		this->GetPidFromName(process),
		const_cast<LPSTR>(module.c_str()), &iat_map);
	if (!result)
	{
		LOG("[!] Failed to get Import Table\n");
		return 0;
	}

	if (iat_map->dwVersion != VMMDLL_MAP_IAT_VERSION)
	{
		VMMDLL_MemFree(iat_map);
		LOG("[!] Invalid VMM Map Version\n");
		return 0;
	}

	uintptr_t addr = 0;
	for (DWORD i = 0; i < iat_map->cMap; i++)
	{
		import_entry = iat_map->pMap + i;
		if (strcmp(import_entry->uszFunction, import.c_str()) == 0)
		{
			addr = import_entry->vaFunction;
			break;
		}
	}

	VMMDLL_MemFree(iat_map);
	return addr;
}
