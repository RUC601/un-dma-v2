#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>
#include "Backend.hpp"
#include "Protocol.hpp"
#include "output/OutputServices.hpp"
#include "Utils/Diagnostics.hpp"
#include <algorithm>
#include <array>
#include <atomic>
#include <bitset>
#include <condition_variable>
#include <deque>
#include <mutex>
#include <thread>
#pragma comment(lib, "ws2_32.lib")

namespace V2::Output {
namespace {
void Complete(const std::shared_ptr<OutputCommandCompletion>& c, int status) { if (!c) return; { std::lock_guard lock(c->mutex); if (c->completed) return; c->status = status; c->completed = true; } c->cv.notify_all(); }
}
struct DhzBoxBackend::Impl {
    struct Job { enum Kind { Wire, Keys, Release } kind = Wire; std::string command; std::bitset<256> keys; bool safety = false; std::shared_ptr<OutputCommandCompletion> completion; uint64_t deadline = 0; };
    mutable std::mutex lifecycle, mutex, snapshotMutex;
    std::condition_variable cv;
    std::deque<Job> queue;
    std::thread worker, monitor;
    std::atomic<bool> running{false}, listening{false}, accepting{false};
    std::atomic<OutputConnectionState> state{OutputConnectionState::Disconnected};
    SOCKET socket = INVALID_SOCKET, monitorSocket = INVALID_SOCKET;
    sockaddr_in endpoint{};
    unsigned shift = 0;
    bool wsa = false;
    std::bitset<256> injected;
    PhysicalInputSnapshot snapshot{};

    int Exchange(std::string_view command, uint64_t deadline = 0) {
        if (socket == INVALID_SOCKET) return err_queue_stopped;
        const std::string wire = DhzBoxProtocol::Shift(command, shift);
        if (sendto(socket, wire.data(), static_cast<int>(wire.size()), 0, reinterpret_cast<const sockaddr*>(&endpoint), sizeof(endpoint)) != static_cast<int>(wire.size())) return err_net_tx;
        const uint64_t end = deadline ? (std::min)(deadline, GetTickCount64() + 300ull) : GetTickCount64() + 300ull;
        while (GetTickCount64() < end) {
            fd_set fds; FD_ZERO(&fds); FD_SET(socket, &fds); const auto left = end - GetTickCount64(); timeval wait{0, static_cast<long>((std::min)(left, 25ull) * 1000)};
            const int ready = select(0, &fds, nullptr, nullptr, &wait); if (ready < 0) return err_net_tx; if (!ready) continue;
            char bytes[1024]; sockaddr_in from{}; int size = sizeof(from); const int n = recvfrom(socket, bytes, sizeof(bytes), 0, reinterpret_cast<sockaddr*>(&from), &size);
            if (n > 0 && from.sin_addr.s_addr == endpoint.sin_addr.s_addr && from.sin_port == endpoint.sin_port) return success;
        }
        return err_net_rx_timeout;
    }
    int Enqueue(Job job) {
        std::lock_guard lock(mutex);
        if (!running || (!job.safety && (!accepting || state != OutputConnectionState::Connected))) return err_queue_stopped;
        if (queue.size() >= OutputQueueConfig::CommandQueueMaxSize) { if (!job.safety) return err_queue_full; auto it = std::find_if(queue.begin(), queue.end(), [](const Job& j){ return !j.safety; }); if (it == queue.end()) return err_queue_full; Complete(it->completion, err_queue_dropped); queue.erase(it); }
        if (job.kind == Job::Release) { for (auto& pending : queue) Complete(pending.completion, err_queue_dropped); queue.clear(); }
        queue.push_back(std::move(job)); cv.notify_one(); return success;
    }
    int ApplyKeys(const std::bitset<256>& desired, uint64_t deadline = 0, bool releaseOnly = false) {
        if (releaseOnly && (desired & ~injected).any()) return err_net_cmd;
        for (unsigned pass = 0; pass < 2; ++pass) for (unsigned u = 4; u < 256; ++u) if (desired[u] != injected[u] && desired[u] == bool(pass)) {
            const auto command = std::string(pass ? "keydown(" : "keyup(") + std::to_string(u) + ")";
            if (pass) injected.set(u); const int result = Exchange(command, deadline); if (result != success) return result; if (!pass) injected.reset(u);
        }
        return success;
    }
    int AllUp(uint64_t deadline) { int first = success; for (int i = 0; i < 5 && GetTickCount64() < deadline; ++i) { const int s = Exchange(DhzBoxProtocol::Button(i, false), deadline); if (first == success && s != success) first = s; } const int keys = ApplyKeys({}, deadline); if (first == success) first = keys; return first; }
    void Run() {
        while (running) { Job job; { std::unique_lock lock(mutex); cv.wait(lock, [&]{ return !running || !queue.empty(); }); if (!running) break; job = std::move(queue.front()); queue.pop_front(); }
            int result = success; if (!job.safety && (state != OutputConnectionState::Connected || !accepting)) result = err_queue_stopped; else if (!job.safety && OutputSuppressed()) result = err_output_suppressed; else if (job.kind == Job::Wire) result = Exchange(job.command); else if (job.kind == Job::Keys) result = ApplyKeys(job.keys, 0, job.safety); else result = AllUp(job.deadline);
            if (result != success && result != err_output_suppressed) { accepting = false; state = OutputConnectionState::Error; } Complete(job.completion, result);
        }
    }
    void Listen() {
        while (listening) { fd_set fds; FD_ZERO(&fds); FD_SET(monitorSocket, &fds); timeval wait{0,25000}; const int ready = select(0, &fds, nullptr, nullptr, &wait); if (ready <= 0) continue; char bytes[256]; sockaddr_in from{}; int size = sizeof(from); const int n = recvfrom(monitorSocket, bytes, sizeof(bytes)-1, 0, reinterpret_cast<sockaddr*>(&from), &size); if (n <= 0 || from.sin_addr.s_addr != endpoint.sin_addr.s_addr) continue; bytes[n] = 0; unsigned values[5]{}; char extra = 0; if (std::sscanf(bytes, "%u|%u|%u|%u|%u%c", &values[0], &values[1], &values[2], &values[3], &values[4], &extra) >= 5) { std::lock_guard lock(snapshotMutex); snapshot.available = true; snapshot.buttons = 0; for (unsigned i=0;i<5;++i) if (values[i]) snapshot.buttons |= 1u << i; snapshot.sampledAtMs = GetTickCount64(); } }
        std::lock_guard lock(snapshotMutex); snapshot = {};
    }
    void Close() { accepting = false; running = false; listening = false; cv.notify_all(); if (worker.joinable()) worker.join(); if (monitor.joinable()) monitor.join(); { std::lock_guard lock(mutex); for (auto& job : queue) Complete(job.completion, err_queue_stopped); queue.clear(); } if (socket != INVALID_SOCKET) { closesocket(socket); socket = INVALID_SOCKET; } if (monitorSocket != INVALID_SOCKET) { closesocket(monitorSocket); monitorSocket = INVALID_SOCKET; } if (wsa) { WSACleanup(); wsa = false; } state = OutputConnectionState::Disconnected; std::lock_guard lock(snapshotMutex); snapshot = {}; }
};
DhzBoxBackend::DhzBoxBackend(): impl_(std::make_unique<Impl>()) {}
DhzBoxBackend::~DhzBoxBackend() { Shutdown(); }
int DhzBoxBackend::Initialize(const std::string& ip, uint16_t port, uint8_t randomShift, uint16_t monitorPort) {
    std::lock_guard lock(impl_->lifecycle); impl_->Close(); if (!port || !InetPtonA(AF_INET, ip.c_str(), &impl_->endpoint.sin_addr)) return err_net_cmd; impl_->endpoint.sin_family = AF_INET; impl_->endpoint.sin_port = htons(port); impl_->shift = randomShift % 26;
    WSADATA data{}; if (WSAStartup(MAKEWORD(2,2), &data)) return err_creat_socket; impl_->wsa = true; impl_->socket = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP); impl_->monitorSocket = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP); if (impl_->socket == INVALID_SOCKET || impl_->monitorSocket == INVALID_SOCKET) { impl_->Close(); return err_creat_socket; }
    if (monitorPort) { sockaddr_in local{}; local.sin_family = AF_INET; local.sin_port = htons(monitorPort); if (bind(impl_->monitorSocket, reinterpret_cast<sockaddr*>(&local), sizeof(local)) == SOCKET_ERROR) { impl_->Close(); return err_creat_socket; } }
    impl_->state = OutputConnectionState::Connected; impl_->accepting = true; impl_->running = true; impl_->listening = monitorPort != 0; impl_->worker = std::thread([p=impl_.get()]{ p->Run(); }); if (impl_->listening) { impl_->monitor = std::thread([p=impl_.get()]{ p->Listen(); }); impl_->Enqueue({Impl::Job::Wire, "monitor(" + std::to_string(monitorPort) + ")"}); } return success;
}
OutputConnectionState DhzBoxBackend::GetConnectionState() const { return impl_->state; }
int DhzBoxBackend::Move(int x, int y) { if (x < -32768 || x > 32767 || y < -32768 || y > 32767) return err_net_cmd; return impl_->Enqueue({Impl::Job::Wire, DhzBoxProtocol::Move(x,y)}); }
int DhzBoxBackend::Button(int button, bool down) { auto command = DhzBoxProtocol::Button(button, down); if (command.empty()) return err_net_cmd; Impl::Job j; j.command = std::move(command); j.safety = !down; return impl_->Enqueue(std::move(j)); }
int DhzBoxBackend::Keyboard(uint8_t modifiers, const std::vector<uint8_t>& usages, OutputIntent intent) { Impl::Job j; j.kind = Impl::Job::Keys; j.safety = intent == OutputIntent::SafetyRelease; for (auto u : usages) { if (u < 4) return err_net_cmd; j.keys.set(u); } for (unsigned i=0;i<8;++i) if (modifiers & (1u<<i)) j.keys.set(224+i); return impl_->Enqueue(std::move(j)); }
int DhzBoxBackend::Mask(uint32_t, std::shared_ptr<OutputCommandCompletion>) { return err_net_cmd; }
int DhzBoxBackend::ReleaseAllOutputAndWait(std::chrono::milliseconds timeout) { impl_->accepting = false; auto completion = std::make_shared<OutputCommandCompletion>(); Impl::Job j; j.kind = Impl::Job::Release; j.safety = true; j.deadline = GetTickCount64() + static_cast<uint64_t>((std::max)(0ll, timeout.count())); j.completion = completion; const int status = impl_->Enqueue(std::move(j)); if (status != success) return status; std::unique_lock lock(completion->mutex); if (!completion->cv.wait_for(lock, timeout, [&]{ return completion->completed; })) return err_completion_timeout; if (completion->status == success) impl_->accepting = true; return completion->status; }
int DhzBoxBackend::Shutdown(std::chrono::milliseconds timeout) { std::lock_guard lock(impl_->lifecycle); int status = success; if (impl_->running) status = ReleaseAllOutputAndWait(timeout); impl_->Close(); return status; }
PhysicalInputSnapshot DhzBoxBackend::PhysicalInput() const { std::lock_guard lock(impl_->snapshotMutex); auto value = impl_->snapshot; value.available = value.available && impl_->listening && impl_->state == OutputConnectionState::Connected && GetTickCount64() - value.sampledAtMs <= 100; return value; }
DhzBoxBackend DhzBoxMgr;
}
