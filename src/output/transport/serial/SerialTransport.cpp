#include "SerialTransport.hpp"
#include "output/OutputServices.hpp"
#include "Utils/Diagnostics.hpp"

#include <algorithm>
#include <chrono>
#include <iostream>
#include <thread>
#include <vector>


namespace
{
    constexpr uint64_t kPhysicalSnapshotMaxAgeMs = 100;

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

    int SerialMouseButtonDomain(const OutputQueuedSerialCommand& command,
        const V2::Output::SerialDeviceProtocol& protocol)
    {
        if (command.type == OutputCommandType::Keyboard) return 256;
        if (command.type != OutputCommandType::MouseButton) return -1;
        return protocol.ButtonDomain(command.command);
    }

    bool HasDependentOrderedSerialSafety(
        const std::deque<OutputQueuedSerialCommand>& queue,
        std::deque<OutputQueuedSerialCommand>::const_iterator candidate,
        const V2::Output::SerialDeviceProtocol& protocol)
    {
        const int domain = SerialMouseButtonDomain(*candidate, protocol);
        if (domain < 0)
            return false;

        for (auto queued = std::next(candidate); queued != queue.end(); ++queued) {
            if (SerialMouseButtonDomain(*queued, protocol) != domain)
                continue;
            return IsOrderedSerialSafety(*queued);
        }
        return false;
    }

    bool IsRequiredPredecessorForIncomingSafety(
        const std::deque<OutputQueuedSerialCommand>& queue,
        std::deque<OutputQueuedSerialCommand>::const_iterator candidate,
        const OutputQueuedSerialCommand& incoming,
        const V2::Output::SerialDeviceProtocol& protocol)
    {
        const int domain = SerialMouseButtonDomain(incoming, protocol);
        if (domain < 0 || SerialMouseButtonDomain(*candidate, protocol) != domain)
            return false;

        return std::none_of(std::next(candidate), queue.end(),
            [domain, &protocol](const OutputQueuedSerialCommand& queued) {
                return SerialMouseButtonDomain(queued, protocol) == domain;
            });
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
    protocol_.Reset();
    if (!protocol_.OnConnected(*this)) {
        CloseHandle(newHandle);
        hSerial = INVALID_HANDLE_VALUE;
        SetConnectionState(OutputConnectionState::Error);
        return false;
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
    if (!protocol_.AfterWrite(*this)) return false;
    return true;
}

bool SerialTransport::SendCommandOrFailSession(
    const std::string& command)
{
    if (SendCommandOnce(command)) return true;
    SetConnectionState(OutputConnectionState::Error);
    protocol_.ClearPhysicalInput();
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
        (type != OutputCommandType::MouseButton || !protocol_.IsButtonRelease(command))) {
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
                        commandQueue, droppable, queued, protocol_)) {
                    continue;
                }
                if (HasDependentOrderedSerialSafety(commandQueue, droppable, protocol_))
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
    for (int button = 0; button < MouseButtonCount(); ++button) {
        if (!SendCommandOrFailSession(protocol_.EncodeButton(button, false)) &&
            aggregateStatus == success) {
            aggregateStatus = err_net_tx;
        }
    }
    const int keyboardStatus = ExecuteKeyboardReport({});
    if (aggregateStatus == success) aggregateStatus = keyboardStatus;
    if (SupportsMask()) {
        for (int button = 0; button < 5; ++button) {
            if (!SendCommandOnce(protocol_.EncodeMask(button, false)) &&
                aggregateStatus == success) {
                aggregateStatus = err_net_tx;
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
        const auto flushInterval = std::chrono::milliseconds(
            protocol_.FlushInterval(command.type));
        if (elapsed < flushInterval)
            std::this_thread::sleep_for(flushInterval - elapsed);

        if (command.priority == OutputCommandPriority::Normal &&
            lifecycleState.load(std::memory_order_acquire) != LifecycleState::Running) {
            CompleteCommand(command.completion, err_queue_stopped);
            continue;
        }

        if (IsSerialOutputCommand(command.type) &&
            ShouldSuppressOutputForMenu(
                V2::Output::OutputSuppressed(), command.outputIntent)) {
            Diagnostics::Aim("serial.queue drop reason=menu_open_suppressed type=%s intent=%s",
                ToString(command.type),
                ToString(command.outputIntent));
            CompleteCommand(command.completion,err_output_suppressed);
            continue;
        }

        if (connectionState.load()!=OutputConnectionState::Connected) {
            CompleteCommand(command.completion,err_queue_stopped);continue;
        }
        const auto sendStarted = std::chrono::steady_clock::now();
        const int status=command.type==OutputCommandType::Keyboard
            ? ExecuteKeyboardReport(command.keyboardKeys,command.outputIntent==OutputIntent::SafetyRelease)
            : (SendCommandOrFailSession(command.command) ? success : err_net_tx);
        if (V2::Output::OutputVerboseLog()) {
            Diagnostics::Aim("serial.send type=%s queue_age_us=%lld send_us=%lld status=%d command=%.*s",
                ToString(command.type),
                static_cast<long long>(std::chrono::duration_cast<std::chrono::microseconds>(
                    sendStarted - command.enqueuedAt).count()),
                static_cast<long long>(std::chrono::duration_cast<std::chrono::microseconds>(
                    std::chrono::steady_clock::now() - sendStarted).count()),
                status,
                (std::max)(0, static_cast<int>(command.command.size()) - 2),
                command.command.c_str());
        }
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

        if (connectionState.load() != OutputConnectionState::Connected) continue;
        std::lock_guard lock(serialMutex);
        if (!protocol_.Heartbeat(*this, V2::Output::OutputSuppressed())) {
            SetConnectionState(OutputConnectionState::Error);
            protocol_.ClearPhysicalInput();
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

int SerialTransport::init(const std::string& portName, DWORD baud)
{
    std::lock_guard<std::mutex> lifecycleLock(lifecycleMutex);
    const LifecycleState previousLifecycle = lifecycleState.load(std::memory_order_acquire);
    if (previousLifecycle == LifecycleState::Stopping)
        return err_queue_stopped;
    if (previousLifecycle == LifecycleState::Stopped)
        lifecycleState.store(LifecycleState::Starting, std::memory_order_release);

    SetConnectionState(OutputConnectionState::Connecting);
    const auto port = normalize_port_name(protocol_.ResolvePort(portName));
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

int SerialTransport::Move(int x, int y)
{
    std::string command;
    if (!protocol_.EncodeMove(x, y, 0, command)) return err_net_cmd;
    return EnqueueCommand(command, OutputCommandType::MouseMove);
}

int SerialTransport::MoveTimed(int x, int y, int runtime)
{
    if (!protocol_.SupportsTimedMove()) return runtime > 0 ? err_net_cmd : Move(x, y);
    std::string command;
    if (!protocol_.EncodeMove(x, y, runtime, command)) return err_net_cmd;
    return EnqueueCommand(command, OutputCommandType::MouseAutoMove);
}

int SerialTransport::Button(int button, bool down)
{
    if (button < 0 || button > 4) return err_net_cmd;
    const auto command = protocol_.EncodeButton(button, down);
    if (command.empty()) return err_net_cmd;
    if (V2::Output::OutputDebugLog())
        Diagnostics::Info("[OUTPUT-SERIAL] queue button=%d down=%d", button, down ? 1 : 0);
    return EnqueueCommand(command, OutputCommandType::MouseButton, OutputIntentForState(down));
}

void SerialTransport::Click()
{
    (void)Button(0, true);
    (void)Button(0, false);
}

int SerialTransport::Left(bool down) { return Button(0, down); }
int SerialTransport::Right(bool down) { return Button(1, down); }
int SerialTransport::Middle(bool down) { return Button(2, down); }

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

bool SerialTransport::Read(std::string& bytes)
{
    char buffer[512];
    DWORD count = 0;
    if (readOverride) {
        bytes = readOverride();
        return bytes.size() <= sizeof(buffer);
    }
    COMSTAT stats{};
    DWORD errors = 0;
    if (!ClearCommError(hSerial, &errors, &stats) || errors) return false;
    if (!stats.cbInQue) {
        bytes.clear();
        return true;
    }
    if (!ReadFile(hSerial, buffer, (std::min)(DWORD(sizeof(buffer)), stats.cbInQue),
        &count, nullptr)) return false;
    bytes.assign(buffer, count);
    return true;
}

void SerialTransport::Purge()
{
    if (hSerial != INVALID_HANDLE_VALUE)
        PurgeComm(hSerial, PURGE_RXCLEAR | PURGE_TXCLEAR);
}

void SerialTransport::PollInput()
{
    if (writeOverride || connectionState.load() != OutputConnectionState::Connected) return;
    std::lock_guard lock(serialMutex);
    if (!protocol_.Poll(*this, lifecycleState.load() == LifecycleState::Running)) {
        SetConnectionState(OutputConnectionState::Error);
        protocol_.ClearPhysicalInput();
    }
}

V2::Output::PhysicalInputSnapshot SerialTransport::PhysicalInput() const
{
    auto result = protocol_.PhysicalInput();
    result.available = result.available && connectionState.load() == OutputConnectionState::Connected &&
        GetTickCount64() - result.sampledAtMs <= kPhysicalSnapshotMaxAgeMs;
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
        const auto command = protocol_.EncodeKey(u, press != 0);
        if(!SendCommandOrFailSession(command)) return err_net_tx;
        if(!press) injectedKeys.reset(u);
    }
    return success;
}

int SerialTransport::MaskMouse(uint32_t mask,std::shared_ptr<OutputCommandCompletion> completion)
{
    if(!SupportsMask() || (mask&~31u)) return err_net_cmd;
    std::string command;
    for (int button = 0; button < 5; ++button)
        command += protocol_.EncodeMask(button, (mask & (1u << button)) != 0);
    return EnqueueCommand(command,mask?OutputCommandType::MouseMask:OutputCommandType::MouseUnmask,
        mask?OutputIntent::Normal:OutputIntent::SafetyRelease,OutputCommandPriority::Normal,std::move(completion));
}
