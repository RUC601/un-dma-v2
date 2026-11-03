#pragma once

#include <Windows.h>
#include <atomic>
#include <bitset>
#include "output/SerialStreamParser.hpp"
#include <condition_variable>
#include <deque>
#include <functional>
#include <mutex>
#include <string>
#include <thread>

#include "output/OutputTypes.hpp"
#include "output/SerialProtocol.hpp"

class SerialTransport {
public:
    enum class LifecycleState : int {
        Stopped = 0,
        Starting,
        Running,
        Stopping
    };

private:
    HANDLE hSerial = INVALID_HANDLE_VALUE;
    std::string serialPortName;
    DWORD serialBaud = 115200;
    std::atomic<V2::Output::SerialProtocol> serialProtocol{V2::Output::SerialProtocol::KmBox};

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
    V2::Output::SerialStreamParser parser;
    std::deque<std::string> responses;
    std::bitset<256> injectedKeys;
    std::atomic<bool> keyboardReady{false}, maskReady{false};
    mutable std::mutex snapshotMutex;
    V2::Output::PhysicalInputSnapshot physical;
    bool ReadInputLocked();
    bool WriteLocked(const std::string& command);
    std::string QueryLocked(const std::string& command);
    void PollInput();
    int ExecuteKeyboardReport(const std::vector<unsigned char>& keys, bool releaseOnly = false);


    void SetConnectionState(OutputConnectionState state);
    bool OpenConfiguredPort(bool AllowDuringStopping = false);
    void ClosePort();
    bool SendCommandOrFailSession(
        const std::string& command);
    bool SendCommandOnce(const std::string& command);
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
    ~SerialTransport();

    OutputConnectionState GetConnectionState() const;
    LifecycleState GetLifecycleState() const;
    bool IsConnected() const;

    int init();
    int init(const std::string& portName, DWORD baud = 115200, V2::Output::SerialProtocol protocol=V2::Output::SerialProtocol::KmBox);

    bool SupportsKeyboard() const { return keyboardReady.load(); }
    bool SupportsMask() const { return maskReady.load(); }
    V2::Output::PhysicalInputSnapshot PhysicalInput() const;
    int SendKeyboardReport(unsigned char modifiers, const std::vector<unsigned char>& usages, OutputIntent intent);
    int MaskMouse(uint32_t mask, std::shared_ptr<OutputCommandCompletion> completion = {});
    int Move(int X, int Y);

    int MoveTimed(int X, int Y, int runtime);

    void Click();

    int Left(bool down);
    int Right(bool down);
    int Middle(bool down);
    int ReleaseAllOutputAndWait(std::chrono::milliseconds timeout);
    int Shutdown(std::chrono::milliseconds timeout = std::chrono::milliseconds(500));

};

namespace V2::Output
{
    inline SerialTransport SerialMgr;
}
