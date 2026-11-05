#include "output/SerialTransport.hpp"
#include "Utils/Config.hpp"
#include "output/KmBoxSerialProtocol.hpp"
#include "Utils/Diagnostics.hpp"

#include <algorithm>
#include <chrono>
#include <iostream>
#include <setupapi.h>
#include <devguid.h>
#include <thread>
#include <vector>

#pragma comment(lib, "setupapi.lib")

namespace
{
    std::atomic<unsigned long long> g_SerialOutputSendCount{ 0 };
    constexpr uint64_t kPhysicalSnapshotMaxAgeMs = 100;
    constexpr uint64_t kMakcuButtonPollIntervalMs = 60;
    constexpr uint64_t kMakcuTextPollTimeoutMs = 30;

    bool IsSerialOutputCommand(OutputCommandType type)
    {
        return type == OutputCommandType::MouseMove ||
            type == OutputCommandType::MouseAutoMove ||
            type == OutputCommandType::MouseButton || type==OutputCommandType::Keyboard || type==OutputCommandType::MouseMask || type==OutputCommandType::MouseUnmask;
    }

    bool IsSerialMouseMoveCommand(OutputCommandType type)
    {
        return type == OutputCommandType::MouseMove ||
            type == OutputCommandType::MouseAutoMove;
    }

    bool IsOrderedSerialSafety(const OutputQueuedSerialCommand& command)
    {
        return command.outputIntent == OutputIntent::SafetyRelease &&
            command.type != OutputCommandType::SafetyReleaseAll;
    }

    int SerialMouseButtonDomain(const OutputQueuedSerialCommand& command)
    {
        if (command.type == OutputCommandType::Keyboard) return 256;
        if (command.type != OutputCommandType::MouseButton)
            return -1;
        if (command.command.starts_with("km.left("))
            return 0;
        if (command.command.starts_with("km.right("))
            return 1;
        if (command.command.starts_with("km.middle("))
            return 2;
        return -1;
    }

    bool HasDependentOrderedSerialSafety(
        const std::deque<OutputQueuedSerialCommand>& queue,
        std::deque<OutputQueuedSerialCommand>::const_iterator candidate)
    {
        const int domain = SerialMouseButtonDomain(*candidate);
        if (domain < 0)
            return false;

        for (auto queued = std::next(candidate); queued != queue.end(); ++queued) {
            if (SerialMouseButtonDomain(*queued) != domain)
                continue;
            return IsOrderedSerialSafety(*queued);
        }
        return false;
    }

    bool IsRequiredPredecessorForIncomingSafety(
        const std::deque<OutputQueuedSerialCommand>& queue,
        std::deque<OutputQueuedSerialCommand>::const_iterator candidate,
        const OutputQueuedSerialCommand& incoming)
    {
        const int domain = SerialMouseButtonDomain(incoming);
        if (domain < 0 || SerialMouseButtonDomain(*candidate) != domain)
            return false;

        return std::none_of(std::next(candidate), queue.end(),
            [domain](const OutputQueuedSerialCommand& queued) {
                return SerialMouseButtonDomain(queued) == domain;
            });
    }

    int SerialFlushIntervalForCommand(OutputCommandType type)
    {
        return type == OutputCommandType::MouseButton
            ? OutputQueueConfig::MouseButtonFlushIntervalMs
            : OutputQueueConfig::CommandFlushIntervalMs;
    }

    void CompleteCommand(
        const std::shared_ptr<OutputCommandCompletion>& completion,
        int status)
    {
        if (!completion)
            return;

        {
            std::lock_guard<std::mutex> lock(completion->mutex);
            if (completion->completed)
                return;
            completion->status = status;
            completion->completed = true;
        }
        completion->cv.notify_all();
    }

    int WaitForCommand(
        const std::shared_ptr<OutputCommandCompletion>& completion,
        std::chrono::milliseconds timeout)
    {
        std::unique_lock<std::mutex> lock(completion->mutex);
        if (!completion->cv.wait_for(lock, timeout, [&completion]() {
                return completion->completed;
            })) {
            return err_completion_timeout;
        }
        return completion->status;
    }
}

// Find a COM port by device description substring
static std::string find_port(const std::string& targetDescription)
{
    HDEVINFO hDevInfo = SetupDiGetClassDevsA(&GUID_DEVCLASS_PORTS, 0, 0, DIGCF_PRESENT);
    if (hDevInfo == INVALID_HANDLE_VALUE) return "";

    SP_DEVINFO_DATA deviceInfoData;
    deviceInfoData.cbSize = sizeof(SP_DEVINFO_DATA);

    for (DWORD i = 0; SetupDiEnumDeviceInfo(hDevInfo, i, &deviceInfoData); ++i) {
        char buf[512];
        DWORD nSize = 0;

        if (SetupDiGetDeviceRegistryPropertyA(hDevInfo, &deviceInfoData, SPDRP_FRIENDLYNAME, NULL, (PBYTE)buf, sizeof(buf), &nSize) && nSize > 0) {
            buf[(std::min)(nSize, static_cast<DWORD>(sizeof(buf) - 1))] = '\0';
            std::string deviceDescription = buf;

            size_t comPos = deviceDescription.find("COM");
            size_t endPos = deviceDescription.find(")", comPos);

            if (comPos != std::string::npos && endPos != std::string::npos && deviceDescription.find(targetDescription) != std::string::npos) {
                SetupDiDestroyDeviceInfoList(hDevInfo);
                return deviceDescription.substr(comPos, endPos - comPos);
            }
        }
    }

    SetupDiDestroyDeviceInfoList(hDevInfo);
    return "";
}

// Open a serial port handle with the given baud rate
static bool open_port(HANDLE& hSerial, const char* portName, DWORD baudRate)
{
    hSerial = CreateFileA(portName, GENERIC_READ | GENERIC_WRITE, 0, 0, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, 0);

    if (hSerial == INVALID_HANDLE_VALUE) return false;

    DCB dcbSerialParams = { 0 };
    dcbSerialParams.DCBlength = sizeof(dcbSerialParams);

    if (!GetCommState(hSerial, &dcbSerialParams)) {
        CloseHandle(hSerial);
        return false;
    }

    dcbSerialParams.BaudRate = baudRate;
    dcbSerialParams.ByteSize = 8;
    dcbSerialParams.StopBits = ONESTOPBIT;
    dcbSerialParams.Parity = NOPARITY;
    dcbSerialParams.fOutxCtsFlow = FALSE;
    dcbSerialParams.fOutxDsrFlow = FALSE;
    dcbSerialParams.fOutX = dcbSerialParams.fInX = FALSE;
    dcbSerialParams.fDtrControl = DTR_CONTROL_DISABLE;
    dcbSerialParams.fRtsControl = RTS_CONTROL_DISABLE;

    if (!SetCommState(hSerial, &dcbSerialParams)) {
        CloseHandle(hSerial);
        return false;
    }

    COMMTIMEOUTS timeouts = { 0 };
    timeouts.ReadIntervalTimeout = OutputQueueConfig::CommandTimeoutMs;
    timeouts.ReadTotalTimeoutConstant = OutputQueueConfig::CommandTimeoutMs;
    timeouts.ReadTotalTimeoutMultiplier = 0;
    timeouts.WriteTotalTimeoutConstant = OutputQueueConfig::CommandTimeoutMs;
    timeouts.WriteTotalTimeoutMultiplier = 0;

    if (!SetCommTimeouts(hSerial, &timeouts)) {
        Diagnostics::Error("[OUTPUT-SERIAL] Error setting serial timeouts. GetLastError=%lu",
            static_cast<unsigned long>(GetLastError()));
        CloseHandle(hSerial);
        return false;
    }

    return true;
}

static std::string normalize_port_name(const std::string& portName)
{
    if (portName.empty() || portName.rfind("\\\\.\\", 0) == 0)
        return portName;

    if (portName.rfind("COM", 0) == 0 && portName.size() > 4)
        return "\\\\.\\" + portName;

    return portName;
}

SerialTransport::~SerialTransport()
{
    (void)Shutdown();
}

OutputConnectionState SerialTransport::GetConnectionState() const
{
    return connectionState.load(std::memory_order_acquire);
}

SerialTransport::LifecycleState SerialTransport::GetLifecycleState() const
{
    return lifecycleState.load(std::memory_order_acquire);
}

bool SerialTransport::IsConnected() const
{
    return GetConnectionState() == OutputConnectionState::Connected;
}

void SerialTransport::SetConnectionState(OutputConnectionState state)
{
    const OutputConnectionState previous = connectionState.exchange(state, std::memory_order_acq_rel);
    if (previous != state) {
        Diagnostics::Info("[OUTPUT-SERIAL] connection state %s -> %s",
            ToString(previous), ToString(state));
    }
}

bool SerialTransport::OpenConfiguredPort(bool AllowDuringStopping)
{
    const LifecycleState lifecycle = lifecycleState.load(std::memory_order_acquire);
    if (lifecycle == LifecycleState::Stopping && !AllowDuringStopping)
        return false;
    if (lifecycle == LifecycleState::Stopped)
        return false;

    if (serialPortName.empty()) {
        SetConnectionState(OutputConnectionState::Disconnected);
        return false;
    }

    std::lock_guard<std::mutex> lock(serialMutex);
    if (lifecycleState.load(std::memory_order_acquire) == LifecycleState::Stopping &&
        !AllowDuringStopping) {
        return false;
    }
    if (hSerial != INVALID_HANDLE_VALUE) {
        CloseHandle(hSerial);
        hSerial = INVALID_HANDLE_VALUE;
    }

    HANDLE newHandle = INVALID_HANDLE_VALUE;
    if (!open_port(newHandle, serialPortName.c_str(), serialBaud)) {
        Diagnostics::Error("[OUTPUT-SERIAL] Failed to open serial port %s. GetLastError=%lu",
            serialPortName.c_str(), static_cast<unsigned long>(GetLastError()));
        SetConnectionState(OutputConnectionState::Error);
        return false;
    }

    hSerial = newHandle;
    parser.Reset(); responses.clear(); keyboardReady=false; maskReady=false;
    makcuPhysicalReply.reset();
    makcuFirmwareVersion.reset();
    makcuBinaryMonitor = false;
    makcuTextNeedsSync = false;
    makcuBinaryPollStartedMs = 0;
    makcuNextButtonPollMs = 0;
    {std::lock_guard stateLock(snapshotMutex);physical={};}
    if (serialProtocol.load() != V2::Output::SerialProtocol::KmBox) {
        PurgeComm(newHandle, PURGE_RXCLEAR | PURGE_TXCLEAR);
        const auto identity=QueryLocked("km.version()\r\n", true);
        if (!V2::Output::IdentityMatches(serialProtocol.load(),identity)) {
            CloseHandle(newHandle);hSerial=INVALID_HANDLE_VALUE;
            SetConnectionState(OutputConnectionState::Error);return false;
        }
        bool monitorReady;
        if (serialProtocol.load()==V2::Output::SerialProtocol::Ferrum) {
            keyboardReady=true;maskReady=true;
            monitorReady = WriteLocked("km.buttons(1)\r\n");
        } else {
            const auto route=QueryLocked("km.device()\r\n");
            const auto begin=route.find("R:");
            const auto end=route.find(';',begin);
            maskReady=begin!=std::string::npos;
            keyboardReady=begin!=std::string::npos && route.substr(begin,end-begin).find('K')!=std::string::npos;
            monitorReady = ConfigureMakcuMonitorLocked(begin!=std::string::npos);
        }
        if (!monitorReady) {
            CloseHandle(newHandle);hSerial=INVALID_HANDLE_VALUE;
            SetConnectionState(OutputConnectionState::Error);return false;
        }
    }
    hSerial = newHandle;
    if (lifecycleState.load(std::memory_order_acquire) != LifecycleState::Stopping)
        SetConnectionState(OutputConnectionState::Connected);
    return true;
}

void SerialTransport::ClosePort()
{
    std::lock_guard<std::mutex> lock(serialMutex);
    if (hSerial != INVALID_HANDLE_VALUE) {
        CloseHandle(hSerial);
        hSerial = INVALID_HANDLE_VALUE;
    }
    SetConnectionState(OutputConnectionState::Disconnected);
}

bool SerialTransport::SendCommandOnce(const std::string& command)
{
    if (writeOverride)
        return writeOverride(command);

    std::lock_guard<std::mutex> lock(serialMutex);
    if (hSerial == INVALID_HANDLE_VALUE && !writeChunkOverride) {
        return false;
    }

    if (!WriteLocked(command)) return false;
    if (serialProtocol.load()!=V2::Output::SerialProtocol::KmBox && !ReadInputLocked()) return false;
    return true;
}

bool SerialTransport::SendCommandOrFailSession(
    const std::string& command)
{
    if (SendCommandOnce(command)) return true;
    SetConnectionState(OutputConnectionState::Error);
    {std::lock_guard lock(snapshotMutex);physical={};}
    CompletePendingCommands(err_queue_stopped);
    return false;
}

int SerialTransport::EnqueueCommand(
    const std::string& command,
    OutputCommandType type,
    OutputIntent outputIntent,
    OutputCommandPriority priority,
    std::shared_ptr<OutputCommandCompletion> completion,
    std::vector<unsigned char> keyboardKeys)
{
    if (outputIntent == OutputIntent::SafetyRelease &&
        priority != OutputCommandPriority::Safety) {
        return EnqueueCommand(
            command,
            type,
            outputIntent,
            OutputCommandPriority::Safety,
            std::move(completion),std::move(keyboardKeys));
    }

    const bool releaseAllBarrier =
        type == OutputCommandType::SafetyReleaseAll;
    if (outputIntent == OutputIntent::SafetyRelease &&
        !releaseAllBarrier && type!=OutputCommandType::Keyboard && type!=OutputCommandType::MouseUnmask &&
        (type != OutputCommandType::MouseButton ||
         (command != "km.left(0)\r\n" &&
          command != "km.right(0)\r\n" &&
          command != "km.middle(0)\r\n"))) {
        CompleteCommand(completion, err_net_cmd);
        return err_net_cmd;
    }

    OutputQueuedSerialCommand queued{};
    queued.command = command;
    queued.keyboardKeys = std::move(keyboardKeys);
    queued.type = type;
    queued.outputIntent = outputIntent;
    queued.priority = priority;
    queued.completion = std::move(completion);
    queued.enqueuedAt = std::chrono::steady_clock::now();
    const bool orderedSafety = IsOrderedSerialSafety(queued);

    std::shared_ptr<OutputCommandCompletion> droppedCompletion;
    std::vector<std::shared_ptr<OutputCommandCompletion>> cancelledCompletions;
    {
        std::unique_lock<std::mutex> lock(queueMutex);
        const LifecycleState lifecycle = lifecycleState.load(std::memory_order_acquire);
        const bool offlineTestSeam =
            lifecycle == LifecycleState::Stopped &&
            workerRunning.load(std::memory_order_acquire) &&
            static_cast<bool>(writeOverride);
        if (priority == OutputCommandPriority::Normal &&
            lifecycle != LifecycleState::Running && !offlineTestSeam) {
            const auto completionToFail = queued.completion;
            lock.unlock();
            CompleteCommand(completionToFail, err_queue_stopped);
            return err_queue_stopped;
        }
        if (priority == OutputCommandPriority::Safety &&
            !workerRunning.load(std::memory_order_acquire)) {
            const auto completionToFail = queued.completion;
            lock.unlock();
            CompleteCommand(completionToFail, err_queue_stopped);
            return err_queue_stopped;
        }

        if (releaseAllBarrier) {
            for (auto pending = commandQueue.begin(); pending != commandQueue.end();) {
                if (pending->type != OutputCommandType::SafetyReleaseAll &&
                    IsSerialOutputCommand(pending->type)) {
                    cancelledCompletions.push_back(pending->completion);
                    pending = commandQueue.erase(pending);
                } else {
                    ++pending;
                }
            }
        }

        if (commandQueue.size() >= OutputQueueConfig::CommandQueueMaxSize) {
            auto droppable = commandQueue.begin();
            for (; droppable != commandQueue.end(); ++droppable) {
                if (droppable->priority != OutputCommandPriority::Normal)
                    continue;
                if (orderedSafety && IsRequiredPredecessorForIncomingSafety(
                        commandQueue, droppable, queued)) {
                    continue;
                }
                if (HasDependentOrderedSerialSafety(commandQueue, droppable))
                    continue;
                break;
            }
            if (droppable == commandQueue.end()) {
                Diagnostics::Error("[OUTPUT-SERIAL] Command queue full; no normal command can be displaced.");
                const auto completionToFail = queued.completion;
                lock.unlock();
                CompleteCommand(completionToFail, err_queue_full);
                return err_queue_full;
            } else {
                Diagnostics::Error("[OUTPUT-SERIAL] Command queue full; dropping oldest normal command.");
                droppedCompletion = droppable->completion;
                commandQueue.erase(droppable);
            }
        }

        if (releaseAllBarrier) {
            const auto firstNormal = std::find_if(
                commandQueue.begin(), commandQueue.end(),
                [](const OutputQueuedSerialCommand& pending) {
                    return pending.priority == OutputCommandPriority::Normal;
                });
            commandQueue.insert(firstNormal, std::move(queued));
        } else if (type == OutputCommandType::MouseButton) {
            auto insertAt = commandQueue.end();
            while (insertAt != commandQueue.begin()) {
                auto previous = insertAt;
                --previous;
                if (!IsSerialMouseMoveCommand(previous->type))
                    break;
                insertAt = previous;
            }
            commandQueue.insert(insertAt, std::move(queued));
        } else {
            commandQueue.push_back(std::move(queued));
        }
    }
    for (const auto& completionToCancel : cancelledCompletions)
        CompleteCommand(completionToCancel, err_queue_dropped);
    CompleteCommand(droppedCompletion, err_queue_dropped);
    queueCv.notify_one();
    return success;
}

int SerialTransport::ExecuteSafetyReleaseAll()
{
    int aggregateStatus = success;
    const auto sendRelease = [this, &aggregateStatus](const char* command) {
        if (!SendCommandOrFailSession(command) &&
            aggregateStatus == success) {
            aggregateStatus = err_net_tx;
        }
    };

    sendRelease("km.left(0)\r\n");
    sendRelease("km.right(0)\r\n");
    sendRelease("km.middle(0)\r\n");
    if(serialProtocol.load()!=V2::Output::SerialProtocol::KmBox) {
        const int keys=ExecuteKeyboardReport({});
        if(aggregateStatus==success) aggregateStatus=keys;
        if(SupportsMask()) {
            for(int i=0;i<5;++i) {
                const auto command=serialProtocol.load()==V2::Output::SerialProtocol::Makcu
                    ? V2::Output::MakcuProtocol::Mask(i,false) : V2::Output::FerrumProtocol::Mask(i,false);
                if(!SendCommandOnce(command) && aggregateStatus==success) aggregateStatus=err_net_tx;
            }
        }
    }
    return aggregateStatus;
}

int SerialTransport::ReleaseAllOutputAndWait(std::chrono::milliseconds timeout)
{
    if (!workerRunning.load(std::memory_order_acquire))
        return err_queue_stopped;

    auto completion = std::make_shared<OutputCommandCompletion>();
    const int enqueueStatus = EnqueueCommand(
        {},
        OutputCommandType::SafetyReleaseAll,
        OutputIntent::SafetyRelease,
        OutputCommandPriority::Safety,
        completion);
    if (enqueueStatus != success)
        return enqueueStatus;
    return WaitForCommand(completion, timeout);
}

void SerialTransport::StartWorkers()
{
    if (!workerRunning.exchange(true, std::memory_order_acq_rel)) {
        queueThread = std::thread(&SerialTransport::QueueWorkerLoop, this);
    }
    if (!heartbeatRunning.exchange(true, std::memory_order_acq_rel)) {
        heartbeatThread = std::thread(&SerialTransport::HeartbeatLoop, this);
    }
}

void SerialTransport::StopWorkers()
{
    workerRunning.store(false, std::memory_order_release);
    heartbeatRunning.store(false, std::memory_order_release);
    queueCv.notify_all();
    heartbeatCv.notify_all();

    const std::thread::id current = std::this_thread::get_id();
    if (queueThread.joinable() && queueThread.get_id() != current)
        queueThread.join();
    if (heartbeatThread.joinable() && heartbeatThread.get_id() != current)
        heartbeatThread.join();
}

void SerialTransport::CompletePendingCommands(int Status)
{
    std::deque<OutputQueuedSerialCommand> pending;
    {
        std::lock_guard<std::mutex> lock(queueMutex);
        pending.swap(commandQueue);
    }
    for (const auto& command : pending)
        CompleteCommand(command.completion, Status);
}

void SerialTransport::QueueWorkerLoop()
{
    queueThreadId.store(GetCurrentThreadId(), std::memory_order_release);
    auto lastFlush = std::chrono::steady_clock::now();
    while (workerRunning.load(std::memory_order_acquire)) {
        PollInput();
        OutputQueuedSerialCommand command{};
        {
            std::unique_lock<std::mutex> lock(queueMutex);
            queueCv.wait_for(lock,
                std::chrono::milliseconds(OutputQueueConfig::CommandFlushIntervalMs),
                [this]() {
                    return !workerRunning.load(std::memory_order_acquire) || !commandQueue.empty();
                });

            if (!workerRunning.load(std::memory_order_acquire))
                break;
            if (commandQueue.empty())
                continue;

            command = std::move(commandQueue.front());
            commandQueue.pop_front();
        }

        if (command.type == OutputCommandType::SafetyReleaseAll) {
            const int status = ExecuteSafetyReleaseAll();
            CompleteCommand(command.completion, status);
            lastFlush = std::chrono::steady_clock::now();
            continue;
        }

        const auto now = std::chrono::steady_clock::now();
        const auto elapsed = now - lastFlush;
        const auto flushInterval = std::chrono::milliseconds(SerialFlushIntervalForCommand(command.type));
        if (elapsed < flushInterval)
            std::this_thread::sleep_for(flushInterval - elapsed);

        if (command.priority == OutputCommandPriority::Normal &&
            lifecycleState.load(std::memory_order_acquire) != LifecycleState::Running) {
            CompleteCommand(command.completion, err_queue_stopped);
            continue;
        }

        if (IsSerialOutputCommand(command.type) &&
            ShouldSuppressOutputForMenu(
                OW::Config::OutputSuppressedByMenu(), command.outputIntent)) {
            Diagnostics::Aim("serial.queue drop reason=menu_open_suppressed type=%s intent=%s",
                ToString(command.type),
                ToString(command.outputIntent));
            CompleteCommand(command.completion,err_output_suppressed);
            continue;
        }

        if (connectionState.load()!=OutputConnectionState::Connected) {
            CompleteCommand(command.completion,err_queue_stopped);continue;
        }
        const int status=command.type==OutputCommandType::Keyboard
            ? ExecuteKeyboardReport(command.keyboardKeys,command.outputIntent==OutputIntent::SafetyRelease)
            : (SendCommandOrFailSession(command.command) ? success : err_net_tx);
        CompleteCommand(command.completion,status);

        lastFlush = std::chrono::steady_clock::now();
    }
    queueThreadId.store(0, std::memory_order_release);
}

void SerialTransport::HeartbeatLoop()
{
    heartbeatThreadId.store(GetCurrentThreadId(), std::memory_order_release);
    while (heartbeatRunning.load(std::memory_order_acquire)) {
        {
            std::unique_lock<std::mutex> lock(heartbeatMutex);
            heartbeatCv.wait_for(lock,
                std::chrono::milliseconds(OutputQueueConfig::HeartbeatIntervalMs),
                [this]() {
                    return !heartbeatRunning.load(std::memory_order_acquire);
                });
        }
        if (!heartbeatRunning.load(std::memory_order_acquire))
            break;
        if (lifecycleState.load(std::memory_order_acquire) != LifecycleState::Running)
            continue;

        if(connectionState.load()!=OutputConnectionState::Connected) continue;
        if(serialProtocol.load()==V2::Output::SerialProtocol::KmBox) continue;
        std::lock_guard lock(serialMutex);
        const bool expiredBinaryPoll = makcuBinaryPollStartedMs != 0 &&
            GetTickCount64() - makcuBinaryPollStartedMs > kPhysicalSnapshotMaxAgeMs;
        if (OW::Config::OutputSuppressedByMenu() && !makcuTextNeedsSync && !expiredBinaryPoll)
            continue;
        if(QueryLocked("km.version()\r\n", true).empty()) {
            SetConnectionState(OutputConnectionState::Error);
            std::lock_guard stateLock(snapshotMutex);physical={};
        } else {
            makcuTextNeedsSync = false;
            if (expiredBinaryPoll) makcuBinaryPollStartedMs = 0;
        }

    }
    heartbeatThreadId.store(0, std::memory_order_release);
}

bool SerialTransport::IsOwnedWorkerThread() const
{
    const DWORD current = GetCurrentThreadId();
    return current != 0 &&
        (current == queueThreadId.load(std::memory_order_acquire) ||
         current == heartbeatThreadId.load(std::memory_order_acquire));
}

int SerialTransport::Shutdown(std::chrono::milliseconds timeout)
{
    if (IsOwnedWorkerThread()) {
        lifecycleState.store(LifecycleState::Stopping, std::memory_order_release);
        workerRunning.store(false, std::memory_order_release);
        heartbeatRunning.store(false, std::memory_order_release);
        queueCv.notify_all();
        heartbeatCv.notify_all();
        Diagnostics::Warn("[OUTPUT-SERIAL] Shutdown deferred from an owned worker thread.");
        return err_queue_stopped;
    }

    std::lock_guard<std::mutex> lifecycleLock(lifecycleMutex);
    if (lifecycleState.load(std::memory_order_acquire) == LifecycleState::Stopped)
        return success;

    lifecycleState.store(LifecycleState::Stopping, std::memory_order_release);
    heartbeatCv.notify_all();

    int cleanupStatus = success;
    if (workerRunning.load(std::memory_order_acquire))
        cleanupStatus = ReleaseAllOutputAndWait(timeout);

    StopWorkers();
    CompletePendingCommands(err_queue_stopped);
    ClosePort();
    writeOverride = {};
    lifecycleState.store(LifecycleState::Stopped, std::memory_order_release);
    Diagnostics::Info("[OUTPUT-SERIAL] lifecycle stopped. cleanup_status=%d", cleanupStatus);
    return cleanupStatus;
}

int SerialTransport::init()
{
    return init("");
}

int SerialTransport::init(const std::string& portName, DWORD baud, V2::Output::SerialProtocol protocol)
{
    std::lock_guard<std::mutex> lifecycleLock(lifecycleMutex);
    const LifecycleState previousLifecycle = lifecycleState.load(std::memory_order_acquire);
    if (previousLifecycle == LifecycleState::Stopping)
        return err_queue_stopped;
    if (previousLifecycle == LifecycleState::Stopped)
        lifecycleState.store(LifecycleState::Starting, std::memory_order_release);

    SetConnectionState(OutputConnectionState::Connecting);
    // MAKCU 必须使用明确选择的端口；不能把另一种串口设备当作候选。
    std::string port = portName.empty()
        ? (protocol!=V2::Output::SerialProtocol::KmBox ? std::string{} : normalize_port_name(find_port("USB-SERIAL CH340")))
        : normalize_port_name(portName);
    if (port.empty()) {
        Diagnostics::Error("[OUTPUT-SERIAL] Serial port not found.");
        SetConnectionState(OutputConnectionState::Disconnected);
        if (previousLifecycle == LifecycleState::Stopped)
            lifecycleState.store(LifecycleState::Stopped, std::memory_order_release);
        return -1;
    }
    CompletePendingCommands(err_queue_stopped);
    serialPortName = port;
    serialBaud = baud;
    serialProtocol.store(protocol);
    if (!OpenConfiguredPort()) {
        if (previousLifecycle == LifecycleState::Stopped)
            lifecycleState.store(LifecycleState::Stopped, std::memory_order_release);
        return -2;
    }
    if(injectedKeys.any()) {
        if(ExecuteKeyboardReport({})!=success) {ClosePort();lifecycleState.store(LifecycleState::Stopped);return err_net_tx;}
    }
    StartWorkers();
    lifecycleState.store(LifecycleState::Running, std::memory_order_release);
    return 0;
}

int SerialTransport::Move(int X, int Y)
{
    if(serialProtocol.load()!=V2::Output::SerialProtocol::KmBox && (X < -32768 || X > 32767 || Y < -32768 || Y > 32767))return err_net_cmd;
    std::string command = V2::Output::KmBoxSerialProtocol::Move(X,Y);
    return EnqueueCommand(command, OutputCommandType::MouseMove);
}

int SerialTransport::MoveTimed(int X, int Y, int runtime)
{
    // V4 文档说明额外路径参数会被忽略，不能把写入成功当作定时轨迹能力。
    if(serialProtocol.load()!=V2::Output::SerialProtocol::KmBox)return runtime>0 ? err_net_cmd : Move(X,Y);
    std::string command = V2::Output::KmBoxSerialProtocol::Move(X,Y,runtime);
    return EnqueueCommand(command, OutputCommandType::MouseAutoMove);
}

void SerialTransport::Click()
{
    std::string command = "km.left(" + std::to_string(1) + ")\r\n";
    std::string command1 = "km.left(" + std::to_string(0) + ")\r\n";
    EnqueueCommand(command, OutputCommandType::MouseButton);
    EnqueueCommand(
        command1,
        OutputCommandType::MouseButton,
        OutputIntent::SafetyRelease);
}

int SerialTransport::Left(bool down)
{
    std::string command = "km.left(" + std::to_string(down ? 1 : 0) + ")\r\n";
    if (OW::Config::outputDebugLog)
        Diagnostics::Info("[OUTPUT-SERIAL] queue output command=Left down=%d", down ? 1 : 0);
    return EnqueueCommand(command, OutputCommandType::MouseButton, OutputIntentForState(down));
}

int SerialTransport::Right(bool down)
{
    std::string command = "km.right(" + std::to_string(down ? 1 : 0) + ")\r\n";
    if (OW::Config::outputDebugLog)
        Diagnostics::Info("[OUTPUT-SERIAL] queue output command=Right down=%d", down ? 1 : 0);
    return EnqueueCommand(command, OutputCommandType::MouseButton, OutputIntentForState(down));
}

int SerialTransport::Middle(bool down)
{
    std::string command = "km.middle(" + std::to_string(down ? 1 : 0) + ")\r\n";
    return EnqueueCommand(command, OutputCommandType::MouseButton, OutputIntentForState(down));
}

bool SerialTransport::WriteLocked(std::string_view command)
{
    if (hSerial==INVALID_HANDLE_VALUE && !writeChunkOverride) return false;
    size_t sent=0;
    const auto deadline=GetTickCount64()+OutputQueueConfig::CommandTimeoutMs;
    while(sent<command.size()) {
        if(GetTickCount64()>=deadline) return false;
        DWORD count=0;
        const bool ok=writeChunkOverride ? writeChunkOverride(command.substr(sent),count)
            : WriteFile(hSerial,command.data()+sent,DWORD(command.size()-sent),&count,nullptr)!=FALSE;
        if(!ok || !count || count>command.size()-sent) return false;
        sent+=count;
    }
    return true;
}

bool SerialTransport::ReadInputLocked(bool allowDeviceError)
{
    char bytes[512];DWORD count=0;
    if(readOverride) {
        const auto data=readOverride();
        if(data.size()>sizeof(bytes)) return false;
        count=static_cast<DWORD>(data.size());std::copy(data.begin(),data.end(),bytes);
    } else {
        COMSTAT stats{};DWORD errors=0;
        if(!ClearCommError(hSerial,&errors,&stats) || errors) return false;
        if(!stats.cbInQue) return true;
        if(!ReadFile(hSerial,bytes,(std::min)(DWORD(sizeof(bytes)),stats.cbInQue),&count,nullptr)) return false;
    }
    for(auto& event:parser.Feed({bytes,count}, serialProtocol.load()==V2::Output::SerialProtocol::Makcu)) {
        if(event.kind==V2::Output::SerialEvent::Kind::Fault) return false;
        if(event.kind==V2::Output::SerialEvent::Kind::Buttons) {
            PublishPhysicalButtons(event.buttons, GetTickCount64());
        } else if (event.kind==V2::Output::SerialEvent::Kind::MakcuPhysicalButtons) {
            if (makcuBinaryMonitor) {
                if (makcuBinaryPollStartedMs != 0 && event.buttons < 32 &&
                    GetTickCount64() - makcuBinaryPollStartedMs <= kPhysicalSnapshotMaxAgeMs)
                    PublishPhysicalButtons(event.buttons, makcuBinaryPollStartedMs);
                makcuBinaryPollStartedMs = 0;
            } else {
                makcuPhysicalReply = event.buttons;
            }
        } else if (event.kind==V2::Output::SerialEvent::Kind::MakcuFirmwareVersion) {
            makcuFirmwareVersion = event.firmwareVersion;
        } else {
            if (!allowDeviceError && event.body.find("ERR")!=std::string::npos)
                return false;
            if(responses.size()>=32) responses.pop_front();
            responses.push_back(std::move(event.body));
        }
    }
    return true;
}

std::string SerialTransport::QueryLocked(const std::string& command, bool expectIdentity)
{
    const bool drainTextPoll = expectIdentity && makcuTextNeedsSync;
    if(!ReadInputLocked(drainTextPoll)) return {};
    responses.clear();
    if(!WriteLocked(command)) return {};
    const auto deadline=GetTickCount64()+500;
    while(GetTickCount64()<deadline) {
        if(!ReadInputLocked(drainTextPoll)) return {};
        while(!responses.empty()) {
            auto body=std::move(responses.front());responses.pop_front();
            auto echo=command;while(!echo.empty() && (echo.back()=='\r'||echo.back()=='\n')) echo.pop_back();
            if(body==echo || body==echo+"\r\n") continue;
            if (expectIdentity && !V2::Output::IdentityMatches(serialProtocol.load(), body)) continue;
            return body;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    return {};
}

void SerialTransport::PublishPhysicalButtons(uint8_t buttons, uint64_t sampledAtMs)
{
    std::lock_guard lock(snapshotMutex);
    physical = {true, buttons, sampledAtMs};
}

bool SerialTransport::ConfigureMakcuMonitorLocked(bool v4Route)
{
    using namespace V2::Output::MakcuProtocol;
    // Older firmware can send bare bytes that are ambiguous in the text stream.
    if (!WriteLocked("km.buttons(0)\r\n")) return false;
    if (v4Route) {
        if (!WriteLocked(PhysicalButtonsRequest)) return false;
        const auto deadline = GetTickCount64() + kPhysicalSnapshotMaxAgeMs;
        while (GetTickCount64() < deadline) {
            if (!ReadInputLocked(true)) return false;
            if (makcuPhysicalReply) {
                if (*makcuPhysicalReply < 32) {
                    makcuBinaryMonitor = true;
                    PublishPhysicalButtons(*makcuPhysicalReply, GetTickCount64());
                    return WriteLocked("km.buttons(1)\r\n");
                }
                break;
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
    }
    if (v4Route) {
        // V4.030/031/033 already support the prefixed stream but lack the binary snapshot.
        if (!WriteLocked(FirmwareVersionRequest)) return false;
        const auto deadline = GetTickCount64() + kPhysicalSnapshotMaxAgeMs;
        while (!makcuFirmwareVersion && GetTickCount64() < deadline) {
            if (!ReadInputLocked(true)) return false;
            if (!makcuFirmwareVersion) std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
    }
    if (!ReadInputLocked(true) || !RefreshMakcuButtonsLocked()) return false;
    return !HasLegacyPrefixedButtonStream(makcuFirmwareVersion.value_or(0)) ||
        WriteLocked("km.buttons(1)\r\n");
}

bool SerialTransport::QueryMakcuTextButtonsLocked()
{
    using namespace V2::Output::MakcuProtocol;
    std::string commands;
    for (const auto query : ButtonQueries) {
        commands += query;
        commands += "\r\n";
    }
    responses.clear();
    const auto started = GetTickCount64();
    if (!WriteLocked(commands)) return false;
    const auto deadline = GetTickCount64() + kMakcuTextPollTimeoutMs;
    size_t button = 0;
    uint8_t buttons = 0;
    bool rejected = false;
    while (!rejected && GetTickCount64() < deadline) {
        if (!ReadInputLocked(true)) return false;
        while (!responses.empty()) {
            const auto body = std::move(responses.front());
            responses.pop_front();
            if (body.find("ERR") != std::string::npos) {
                rejected = true;
                break;
            }
            const auto state = PhysicalButton(body, button);
            if (!state) continue;
            if (*state) buttons |= uint8_t(1u << button);
            if (++button == ButtonQueries.size()) {
                std::lock_guard lock(snapshotMutex);
                if (physical.sampledAtMs < started) physical = {true, buttons, GetTickCount64()};
                return true;
            }
        }
        if (!rejected) std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    // Drain outstanding text replies through the next identity check before retrying.
    makcuTextNeedsSync = true;
    std::lock_guard lock(snapshotMutex);
    if (physical.sampledAtMs < started) physical = {};
    return true;
}

bool SerialTransport::RefreshMakcuButtonsLocked()
{
    const auto now = GetTickCount64();
    if (makcuTextNeedsSync || makcuBinaryPollStartedMs != 0 || now < makcuNextButtonPollMs)
        return true;
    {
        std::lock_guard lock(snapshotMutex);
        if (physical.available && now - physical.sampledAtMs < kMakcuButtonPollIntervalMs)
            return true;
    }
    makcuNextButtonPollMs = now + kMakcuButtonPollIntervalMs;
    if (!makcuBinaryMonitor) return QueryMakcuTextButtonsLocked();
    if (!WriteLocked(V2::Output::MakcuProtocol::PhysicalButtonsRequest)) return false;
    makcuBinaryPollStartedMs = now;
    return true;
}

void SerialTransport::PollInput()
{
    if(writeOverride || serialProtocol.load()==V2::Output::SerialProtocol::KmBox ||
       connectionState.load()!=OutputConnectionState::Connected) return;
    std::lock_guard lock(serialMutex);
    const bool refreshMakcu = serialProtocol.load()==V2::Output::SerialProtocol::Makcu &&
        lifecycleState.load()==LifecycleState::Running;
    if (!ReadInputLocked() || (refreshMakcu && !RefreshMakcuButtonsLocked())) {
        SetConnectionState(OutputConnectionState::Error);
        std::lock_guard stateLock(snapshotMutex);physical={};
    }
}

V2::Output::PhysicalInputSnapshot SerialTransport::PhysicalInput() const
{
    std::lock_guard lock(snapshotMutex);auto result=physical;
    result.available=result.available && connectionState.load()==OutputConnectionState::Connected &&
        GetTickCount64()-result.sampledAtMs<=kPhysicalSnapshotMaxAgeMs;
    return result;
}

int SerialTransport::SendKeyboardReport(unsigned char modifiers,
    const std::vector<unsigned char>& usages, OutputIntent intent)
{
    if(!SupportsKeyboard()) return err_net_cmd;
    std::vector<unsigned char> keys=usages;
    for(auto key:keys) if(key<4 || key>=224) return err_net_cmd;
    for(unsigned i=0;i<8;++i) if(modifiers&(1u<<i)) keys.push_back(static_cast<unsigned char>(224+i));
    return EnqueueCommand({},OutputCommandType::Keyboard,intent,OutputCommandPriority::Normal,{},std::move(keys));
}

int SerialTransport::ExecuteKeyboardReport(const std::vector<unsigned char>& keys, bool releaseOnly)
{
    std::bitset<256> desired;for(auto key:keys) desired.set(key);
    if(releaseOnly && (desired & ~injectedKeys).any()) return err_net_cmd;
    for(int press=0;press<2;++press) for(unsigned u=0;u<256;++u) {
        if(desired[u]==injectedKeys[u] || desired[u]!=bool(press)) continue;
        if(press) injectedKeys.set(u);
        const auto command=std::string(press?"km.down(":"km.up(")+std::to_string(u)+")\r\n";
        if(!SendCommandOrFailSession(command)) return err_net_tx;
        if(!press) injectedKeys.reset(u);
    }
    return success;
}

int SerialTransport::MaskMouse(uint32_t mask,std::shared_ptr<OutputCommandCompletion> completion)
{
    if(!SupportsMask() || (mask&~31u)) return err_net_cmd;
    std::string command;
    for(int i=0;i<5;++i) command+=serialProtocol.load()==V2::Output::SerialProtocol::Makcu
        ? V2::Output::MakcuProtocol::Mask(i,(mask&(1u<<i))!=0)
        : V2::Output::FerrumProtocol::Mask(i,(mask&(1u<<i))!=0);
    return EnqueueCommand(command,mask?OutputCommandType::MouseMask:OutputCommandType::MouseUnmask,
        mask?OutputIntent::Normal:OutputIntent::SafetyRelease,OutputCommandPriority::Normal,std::move(completion));
}
