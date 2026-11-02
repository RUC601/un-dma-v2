#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>
#include <bcrypt.h>
#include "output/CatBoxBackend.hpp"
#include "output/CatBoxProtocol.hpp"
#include "Utils/Config.hpp"
#include <algorithm>
#include <atomic>
#include <bitset>
#include <condition_variable>
#include <deque>
#include <mutex>
#include <thread>
#pragma comment(lib, "ws2_32.lib")
#pragma comment(lib, "bcrypt.lib")
namespace V2::Output {
using CatBoxProtocol::Command;
static void Complete(const std::shared_ptr<OutputCommandCompletion>& c, int result) {
    if (!c) return;
    { std::lock_guard lock(c->mutex); c->status = result; c->completed = true; }
    c->cv.notify_all();
}
struct CatBoxBackend::Impl {
    struct Job {
        enum Kind { Wire, Keys, Mask, Release } kind = Wire;
        Command command{};
        std::bitset<256> keys;
        uint32_t mask = 0;
        bool safety = false;
        std::shared_ptr<OutputCommandCompletion> completion;
        uint64_t deadline = 0;
    };
    std::mutex lifecycle, mutex;
    std::condition_variable cv;
    std::deque<Job> queue;
    std::thread worker, monitor;
    std::atomic<bool> running{false}, listening{false}, accepting{false};
    std::atomic<OutputConnectionState> state{OutputConnectionState::Disconnected};
    SOCKET socket = INVALID_SOCKET, monitorSocket = INVALID_SOCKET;
    sockaddr_in endpoint{};
    CatBoxProtocol::Key key{};
    bool wsa = false;
    std::bitset<256> injected;
    mutable std::mutex snapshotMutex;
    PhysicalInputSnapshot snapshot;

    int Exchange(Command c, uint64_t deadline = 0) {
        if (socket == INVALID_SOCKET) return err_queue_stopped;
        CatBoxProtocol::Key iv{};
        if (BCryptGenRandom(nullptr, iv.data(), 16, BCRYPT_USE_SYSTEM_PREFERRED_RNG) < 0) return err_net_tx;
        auto packet = CatBoxProtocol::Encode(c, key, iv);
        if (packet.empty()) return err_net_tx;
        // The published protocol has no transaction ID. Serialize one request
        // at a time and never retry a timed-out movement or button press.
        for (unsigned i=0; i<64; ++i) {
            fd_set fds; FD_ZERO(&fds); FD_SET(socket, &fds); timeval zero{};
            if (select(0, &fds, nullptr, nullptr, &zero) <= 0) break;
            char stale[1024]; recv(socket, stale, sizeof(stale), 0);
        }
        if (sendto(socket, reinterpret_cast<const char*>(packet.data()), int(packet.size()), 0,
                reinterpret_cast<const sockaddr*>(&endpoint), sizeof(endpoint)) != int(packet.size())) return err_net_tx;
        const auto end = deadline ? (std::min)(deadline, GetTickCount64() + 300) : GetTickCount64() + 300;
        while (GetTickCount64() < end) {
            fd_set fds; FD_ZERO(&fds); FD_SET(socket, &fds);
            const auto remaining = end - GetTickCount64();
            timeval wait{0, static_cast<long>((std::min)(remaining, 25ull) * 1000)};
            const int ready = select(0, &fds, nullptr, nullptr, &wait);
            if (ready < 0) return err_net_tx;
            if (!ready) continue;
            char bytes[1024]; sockaddr_in from{}; int size = sizeof(from);
            const int n = recvfrom(socket, bytes, sizeof(bytes), 0, reinterpret_cast<sockaddr*>(&from), &size);
            if (n <= 0) return err_net_tx;
            if (from.sin_addr.s_addr != endpoint.sin_addr.s_addr || from.sin_port != endpoint.sin_port) continue;
            std::vector<uint8_t> plain;
            if (!CatBoxProtocol::Decode({bytes, bytes+n}, key, plain) || plain.size() != 7 || plain[0] != c.code) continue;
            return success;
        }
        return err_net_rx_timeout;
    }
    int Enqueue(Job job) {
        std::lock_guard lock(mutex);
        if (!running || (!job.safety && (!accepting || state != OutputConnectionState::Connected))) return err_queue_stopped;
        if (queue.size() >= OutputQueueConfig::CommandQueueMaxSize) {
            if (!job.safety) return err_queue_full;
            auto it = std::find_if(queue.begin(), queue.end(), [](const Job& j){return !j.safety;});
            if (it == queue.end()) return err_queue_full;
            Complete(it->completion, err_queue_dropped); queue.erase(it);
        }
        if (job.kind == Job::Release) {
            for (auto& j : queue) Complete(j.completion, err_queue_dropped);
            queue.clear();
        }
        // FIFO preserves each accepted press/release pair. A release-all is a fence.
        queue.push_back(std::move(job)); cv.notify_one(); return success;
    }
    int ApplyKeys(const std::bitset<256>& desired, uint64_t deadline = 0, bool releaseOnly = false) {
        if(releaseOnly && (desired & ~injected).any()) return err_net_cmd;
        int first = success;
        for (int press=0; press<2; ++press) for (unsigned u=0; u<256; ++u) {
            if (desired[u] == injected[u] || desired[u] != bool(press)) continue;
            const auto code = CatBoxProtocol::HidToLinux(uint8_t(u));
            if (!code) return err_net_cmd;
            if (press) injected.set(u); // retain uncertain presses for cleanup after a partial failure
            const int result = Exchange({4, code, int16_t(press), 0}, deadline);
            if (result == success && !press) injected.reset(u);
            if (result != success && first == success) first = result;
            if (result != success) return first;
        }
        return first;
    }
    int ApplyMask(uint32_t mask, uint64_t deadline = 0) {
        int first = success;
        for (unsigned i=0; i<5; ++i) {
            const int result=Exchange({5, uint16_t(0x110+i), int16_t((mask>>i)&1), 0}, deadline);
            if (result != success && first == success) first=result;
            if (deadline && GetTickCount64() >= deadline) break;
        }
        return first;
    }
    int AllUp(uint64_t deadline) {
        int first = success;
        auto record=[&](int result){if (first==success && result!=success) first=result;};
        for (unsigned i=0;i<5 && GetTickCount64()<deadline;++i) record(Exchange({3,uint16_t(0x110+i),0,0},deadline));
        record(ApplyKeys({},deadline));
        if (GetTickCount64()<deadline) record(Exchange({6},deadline));
        if (GetTickCount64()<deadline) record(Exchange({7},deadline));
        if (GetTickCount64()>=deadline && first==success) first=err_completion_timeout;
        return first;
    }
    void Run() {
        while (running) {
            Job job;
            { std::unique_lock lock(mutex); cv.wait(lock,[&]{return !running || !queue.empty();});
              if (!running) break; job=std::move(queue.front());queue.pop_front(); }
            int result=success;
            if (!job.safety && (state!=OutputConnectionState::Connected || !accepting)) result=err_queue_stopped;
            else if (!job.safety && OW::Config::OutputSuppressedByMenu()) result=err_output_suppressed;
            else switch(job.kind) {
            case Job::Wire: result=Exchange(job.command); break;
            case Job::Keys: result=ApplyKeys(job.keys,0,job.safety); break;
            case Job::Mask: result=ApplyMask(job.mask); break;
            case Job::Release: result=AllUp(job.deadline); break;
            }
            if (result!=success && result!=err_output_suppressed) {
                accepting=false;state=OutputConnectionState::Error;
                std::lock_guard lock(snapshotMutex);snapshot={};
            }
            Complete(job.completion,result);
        }
    }
    void Listen() {
        while (listening) {
            fd_set fds;FD_ZERO(&fds);FD_SET(monitorSocket,&fds);timeval wait{0,25000};
            const int ready=select(0,&fds,nullptr,nullptr,&wait);
            if (ready<0) {listening=false;break;}
            if (!ready) continue;
            char bytes[1024];sockaddr_in from{};int size=sizeof(from);
            const int n=recvfrom(monitorSocket,bytes,sizeof(bytes),0,reinterpret_cast<sockaddr*>(&from),&size);
            if(n<=0) continue;
            if(from.sin_addr.s_addr!=endpoint.sin_addr.s_addr) continue;
            std::vector<uint8_t> plain;
            if(!CatBoxProtocol::Decode({bytes,bytes+n},key,plain) || plain.size()!=15) continue;
            const unsigned code=plain[0] | (plain[1]<<8), value=plain[2] | (plain[3]<<8);
            const unsigned keycode=plain[10] | (plain[11]<<8), keyvalue=plain[12] | (plain[13]<<8);
            if(keycode && keyvalue<=1) {
                std::lock_guard lock(snapshotMutex);
                for(unsigned u=4;u<256;++u) if(CatBoxProtocol::HidToLinux(uint8_t(u))==keycode) {
                    snapshot.keyboard.set(u,keyvalue!=0);snapshot.keyboardAvailable=true;
                    snapshot.keyboardSampledAtMs=GetTickCount64();break;
                }
            }
            if(code>=0x110 && code<=0x114 && value<=1) {
                std::lock_guard lock(snapshotMutex);
                const auto bit=1u<<(code-0x110);
                snapshot.buttons=value ? snapshot.buttons|bit : snapshot.buttons&~bit;
                snapshot.sampledAtMs=GetTickCount64();snapshot.available=true;
            }
        }
        std::lock_guard lock(snapshotMutex);snapshot={};
    }
    void Close() {
        accepting=false;running=false;listening=false;cv.notify_all();
        if(worker.joinable()) worker.join();
        if(monitor.joinable()) monitor.join();
        {std::lock_guard lock(mutex);for(auto& job:queue) Complete(job.completion,err_queue_stopped);queue.clear();}
        if(socket!=INVALID_SOCKET) {closesocket(socket);socket=INVALID_SOCKET;}
        if(monitorSocket!=INVALID_SOCKET) {closesocket(monitorSocket);monitorSocket=INVALID_SOCKET;}
        if(wsa) {WSACleanup();wsa=false;}
        state=OutputConnectionState::Disconnected;
        std::lock_guard lock(snapshotMutex);snapshot={};
    }
};
CatBoxBackend::CatBoxBackend():impl_(std::make_unique<Impl>()){}
CatBoxBackend::~CatBoxBackend(){Shutdown();}
int CatBoxBackend::Initialize(const std::string& ip,uint16_t port,const std::string& uuid,uint16_t monitorPort) {
    std::lock_guard lock(impl_->lifecycle);impl_->Close();
    if(!port || !monitorPort || monitorPort==port || !CatBoxProtocol::DeriveKey(uuid,impl_->key)) return err_net_cmd;
    impl_->endpoint={};impl_->endpoint.sin_family=AF_INET;impl_->endpoint.sin_port=htons(port);
    if(InetPtonA(AF_INET,ip.c_str(),&impl_->endpoint.sin_addr)!=1) return err_net_cmd;
    WSADATA data{};if(WSAStartup(MAKEWORD(2,2),&data)) return err_creat_socket;
    impl_->wsa=true;impl_->socket=::socket(AF_INET,SOCK_DGRAM,IPPROTO_UDP);
    impl_->monitorSocket=::socket(AF_INET,SOCK_DGRAM,IPPROTO_UDP);
    sockaddr_in local{};local.sin_family=AF_INET;local.sin_port=htons(monitorPort);
    if(impl_->socket==INVALID_SOCKET || impl_->monitorSocket==INVALID_SOCKET ||
       bind(impl_->monitorSocket,reinterpret_cast<sockaddr*>(&local),sizeof(local))==SOCKET_ERROR) {impl_->Close();return err_creat_socket;}
    int status=impl_->Exchange({1});
    if(status==success) status=impl_->Exchange({2,monitorPort});
    if(status!=success) {impl_->Close();return status;}
    // Recover keys that may have been partially transmitted before an earlier link failure.
    if(impl_->injected.any()) {status=impl_->ApplyKeys({});if(status!=success){impl_->Close();return status;}}
    impl_->state=OutputConnectionState::Connected;impl_->accepting=true;impl_->running=true;impl_->listening=true;
    impl_->worker=std::thread([this]{impl_->Run();});impl_->monitor=std::thread([this]{impl_->Listen();});
    return success;
}
OutputConnectionState CatBoxBackend::GetConnectionState() const{return impl_->state;}
int CatBoxBackend::Move(int x,int y,int duration) {
    if(x < -32768 || x>32767 || y < -32768 || y>32767 || duration<0 || duration>32767) return err_net_cmd;
    Impl::Job j;j.command={uint8_t(duration?9:8),uint16_t(duration),int16_t(x),int16_t(y)};return impl_->Enqueue(std::move(j));
}
int CatBoxBackend::Button(int button,bool down) {
    if(button<0 || button>4) return err_net_cmd;
    Impl::Job j;j.command={3,uint16_t(0x110+button),int16_t(down),0};j.safety=!down;return impl_->Enqueue(std::move(j));
}
int CatBoxBackend::Keyboard(uint8_t modifiers,const std::vector<uint8_t>& usages,OutputIntent intent) {
    Impl::Job j;j.kind=Impl::Job::Keys;j.safety=intent==OutputIntent::SafetyRelease;
    for(auto u:usages) {if(!CatBoxProtocol::HidToLinux(u)) return err_net_cmd;j.keys.set(u);}
    for(unsigned i=0;i<8;++i) if(modifiers&(1u<<i)) j.keys.set(224+i);
    return impl_->Enqueue(std::move(j));
}
int CatBoxBackend::Mask(uint32_t mask,std::shared_ptr<OutputCommandCompletion> completion) {
    if(mask&~31u) return err_net_cmd;
    Impl::Job j;j.kind=Impl::Job::Mask;j.mask=mask;j.safety=!mask;j.completion=std::move(completion);return impl_->Enqueue(std::move(j));
}
int CatBoxBackend::ReleaseAllOutputAndWait(std::chrono::milliseconds timeout) {
    if(timeout.count()<0) return err_net_cmd;
    impl_->accepting=false;
    Impl::Job j;j.kind=Impl::Job::Release;j.safety=true;j.deadline=GetTickCount64()+timeout.count();
    auto completion=std::make_shared<OutputCommandCompletion>();j.completion=completion;
    const int status=impl_->Enqueue(std::move(j));if(status!=success) return status;
    std::unique_lock lock(completion->mutex);
    if(!completion->cv.wait_for(lock,timeout,[&]{return completion->completed;})) return err_completion_timeout;
    if(completion->status==success) impl_->accepting=true;
    return completion->status;
}
int CatBoxBackend::Shutdown(std::chrono::milliseconds timeout) {
    std::lock_guard lock(impl_->lifecycle);
    int status=success;if(impl_->running) status=ReleaseAllOutputAndWait(timeout);
    impl_->Close();return status;
}
PhysicalInputSnapshot CatBoxBackend::PhysicalInput() const {
    std::lock_guard lock(impl_->snapshotMutex);auto value=impl_->snapshot;
    value.available=value.available && impl_->listening && impl_->state==OutputConnectionState::Connected &&
        GetTickCount64()-value.sampledAtMs<=100;
    value.keyboardAvailable=value.keyboardAvailable && impl_->listening &&
        impl_->state==OutputConnectionState::Connected && GetTickCount64()-value.keyboardSampledAtMs<=100;
    return value;
}
}
