#pragma once

#include <Windows.h>
#include <atomic>
#include <bitset>
#include "output/transport/serial/SerialDeviceProtocol.hpp"
#include <condition_variable>
#include <deque>
#include <functional>
#include <mutex>
#include <optional>
#include <string>
#include <thread>

#include "QueueTypes.hpp"

class SerialTransport : private V2::Output::SerialChannel {
public:
    enum class LifecycleState : int {
        Stopped = 0,
        Starting,
        Running,
        Stopping
    };

private:
    V2::Output::SerialDeviceProtocol& protocol_;
    HANDLE hSerial = INVALID_HANDLE_VALUE;
    std::string serialPortName;
    DWORD serialBaud = 115200;

    std::atomic<OutputConnectionState> connectionState{ OutputConnectionState::Disconnected };
    std::atomic<LifecycleState> lifecycleState{ LifecycleState::Stopped };
    std::atomic<bool> workerRunning{ false };
    std::atomic<bool> heartbeatRunning{ false };

    std::mutex lifecycleMutex;
    std::mutex serialMutex;
    std::mutex queueMutex;
    std::condition_variable queueCv;
    std::mutex heartbeatMutex;
    std::condition_variable heartbeatCv;
    std::deque<OutputQueuedSerialCommand> commandQueue;
    std::thread queueThread;
    std::thread heartbeatThread;
    std::atomic<DWORD> queueThreadId{ 0 };
    std::atomic<DWORD> heartbeatThreadId{ 0 };
    std::function<bool(const std::string&)> writeOverride;
    std::function<bool(std::string_view, DWORD&)> writeChunkOverride;
    std::function<std::string()> readOverride;
    std::bitset<256> injectedKeys;
    bool WriteLocked(std::string_view command);
    bool Write(std::string_view bytes) override { return WriteLocked(bytes); }
    bool Read(std::string& bytes) override;
    void Purge() override;
    void PollInput();
    int ExecuteKeyboardReport(const std::vector<unsigned char>& keys, bool releaseOnly = false);

    void SetConnectionState(OutputConnectionState state);
    bool OpenConfiguredPort(bool AllowDuringStopping = false);
    void ClosePort();
    bool SendCommandOrFailSession(
        const std::string& command, bool traceLeftButton = false);
    bool SendCommandOnce(const std::string& command, bool traceLeftButton = false);
    int EnqueueCommand(
        const std::string& command,
        OutputCommandType type,
        OutputIntent outputIntent = OutputIntent::Normal,
        OutputCommandPriority priority = OutputCommandPriority::Normal,
        std::shared_ptr<OutputCommandCompletion> completion = {},
        std::vector<unsigned char> keyboardKeys = {});
    int ExecuteSafetyReleaseAll();
    bool IsOwnedWorkerThread() const;
    void StartWorkers();
    void StopWorkers();
    void CompletePendingCommands(int Status);
    void QueueWorkerLoop();
    void HeartbeatLoop();
public:
    friend class OutputProtocolSelfTestAccess;
    friend class OutputQueueSelfTestAccess;
    friend class OutputLifecycleSelfTestAccess;
    explicit SerialTransport(V2::Output::SerialDeviceProtocol& protocol) : protocol_(protocol) {}
    ~SerialTransport();

    OutputConnectionState GetConnectionState() const;
    LifecycleState GetLifecycleState() const;
    bool IsConnected() const;

    int init(const std::string& portName, DWORD baud = 115200);

    bool SupportsKeyboard() const { return protocol_.SupportsKeyboard(); }
    bool SupportsMask() const { return protocol_.SupportsMask(); }
    int MouseButtonCount() const { return protocol_.EncodeButton(4, false).empty() ? 3 : 5; }
    V2::Output::PhysicalInputSnapshot PhysicalInput() const;
    int SendKeyboardReport(unsigned char modifiers, const std::vector<unsigned char>& usages, OutputIntent intent);
    int MaskMouse(uint32_t mask, std::shared_ptr<OutputCommandCompletion> completion = {});
    int Move(int X, int Y);

    int MoveTimed(int X, int Y, int runtime);

    int Button(int button, bool down);
    void Click();

    int Left(bool down);
    int Right(bool down);
    int Middle(bool down);
    int ReleaseAllOutputAndWait(std::chrono::milliseconds timeout);
    int Shutdown(std::chrono::milliseconds timeout = std::chrono::milliseconds(500));

};
