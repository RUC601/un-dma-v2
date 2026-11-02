#pragma once
#include <windows.h>
#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <mutex>
#include <thread>
#include <vector>

namespace RuntimeTrace {
enum Kind : uint32_t { CameraRead=1, CameraPublish, CameraReject, ReadWait,
    EntityPublish, EntityMissing, ActorReplace, RenderSkip, Bounds, Scan, SlowWork, RenderCameraUse, EntityReject, ProjectionJump, TopologyProbe, ProjectionRead, SlowRead, DiscoveryProbe, SdkEnter, SdkLeave, Count };
struct Event {
    uint64_t us=0, tick=0; uint32_t kind=0, reserved=0;
    uint64_t a=0,b=0,c=0,d=0; float values[16]{};
};
static_assert(sizeof(Event)==120);
struct Queue {
    std::array<Event,8192> events;
    std::atomic<uint64_t> write{0},read{0};
    std::atomic<bool> retired{false};
};
// TLS teardown touches only its owned queue, never the global registry.
struct Producer {
    std::shared_ptr<Queue> queue;
    ~Producer() { queue->retired.store(true,std::memory_order_release); }
};
inline std::array<std::atomic<uint64_t>,Count> counts{};
inline std::atomic<uint64_t> dropped{0},ioErrors{0};
inline std::atomic<bool> enabled{false},stopping{false};
inline std::mutex registryMutex;
inline std::vector<std::shared_ptr<Queue>> queues;
inline std::thread writer;
#ifdef UNLEASHED_TRACE_SELF_TEST
// Set before Start; called outside registryMutex. Only the offline target has hooks.
inline void (*writerCheckpoint)(int)=nullptr;
inline void Checkpoint(int stage) { if(writerCheckpoint)writerCheckpoint(stage); }
#else
inline void Checkpoint(int) {}
#endif
inline uint64_t NowUs() {
    return static_cast<uint64_t>(std::chrono::duration_cast<std::chrono::microseconds>(
        std::chrono::steady_clock::now().time_since_epoch()).count());
}
inline void Emit(Kind kind,uint64_t a=0,uint64_t b=0,uint64_t c=0,uint64_t d=0,
                 const float* values=nullptr,size_t n=0) {
    counts[kind].fetch_add(1,std::memory_order_relaxed);
    if(!enabled.load(std::memory_order_acquire)) return;
    thread_local Producer producer{[] {
        auto q=std::make_shared<Queue>();
        std::lock_guard<std::mutex> lock(registryMutex); queues.push_back(q); return q;
    }()};
    auto& q=*producer.queue; const auto w=q.write.load(std::memory_order_relaxed);
    if(w-q.read.load(std::memory_order_acquire)>=q.events.size()) {
        dropped.fetch_add(1,std::memory_order_relaxed); return;
    }
    Event event{}; event.us=NowUs();event.tick=GetTickCount64();event.kind=kind;
    event.a=a;event.b=b;event.c=c;event.d=d;
    if(values)std::memcpy(event.values,values,(std::min)(n,size_t{16})*sizeof(float));
    q.events[w%q.events.size()]=event;q.write.store(w+1,std::memory_order_release);
}
inline void EnsureLifetime();
enum class SdkPhase : uint64_t { Initialize=1, ScatterCreate, ScatterExecute, ScatterClear, DirectRead, FreshRead, KeyMaterial, SessionDrain };
class SdkSpan {
    inline static std::atomic<uint64_t> sequence{0};
    uint64_t id_=0,start_=0;SdkPhase phase_;bool success_=false;
public:
    explicit SdkSpan(SdkPhase phase,uint64_t object=0):phase_(phase) {
        if(!enabled.load(std::memory_order_acquire))return;
        id_=sequence.fetch_add(1,std::memory_order_relaxed)+1;start_=NowUs();
        Emit(SdkEnter,id_,uint64_t(phase_),object);
    }
    SdkSpan(const SdkSpan&)=delete;
    SdkSpan& operator=(const SdkSpan&)=delete;
    void Result(bool success){success_=success;}
    ~SdkSpan(){if(id_)Emit(SdkLeave,id_,uint64_t(phase_),NowUs()-start_,success_);}
};
inline bool Start() {
    EnsureLifetime();
    // Serial lifecycle owner only; reject a second Start before touching the file.
    if(writer.joinable())return false;
    const char* path=std::getenv("UN_DMA_TRACE_FILE");
    if(!path||!*path)return true;
    FILE* file=nullptr;if(fopen_s(&file,path,"wb")!=0||!file)return false;
    stopping=false;enabled=true;
    writer=std::thread([file] {
        for(;;) {
            const bool done=stopping.load(std::memory_order_acquire);
            Checkpoint(0);
            std::vector<std::shared_ptr<Queue>> snapshot;
            {std::lock_guard<std::mutex> lock(registryMutex);snapshot=queues;}
            Checkpoint(1);
            for(auto& q:snapshot) {
                auto r=q->read.load(std::memory_order_relaxed);
                const auto w=q->write.load(std::memory_order_acquire);
                while(r<w) {
                    const auto n=(std::min)(w-r,uint64_t(q->events.size()-r%q->events.size()));
                    if(std::fwrite(&q->events[r%q->events.size()],sizeof(Event),size_t(n),file)!=n)++ioErrors;
                    r+=n;
                }
                q->read.store(r,std::memory_order_release);
            }
            {
                std::lock_guard<std::mutex> lock(registryMutex);
                queues.erase(std::remove_if(queues.begin(),queues.end(),[](const auto& q) {
                    // Retirement acquire precedes the final cursor read; a live empty queue stays.
                    return q->retired.load(std::memory_order_acquire) &&
                        q->read.load(std::memory_order_relaxed)==q->write.load(std::memory_order_acquire);
                }),queues.end());
            }
            Checkpoint(2); // snapshot still owns queues removed from the registry
            snapshot.clear();
            if(done)break;
            std::this_thread::sleep_for(std::chrono::milliseconds(20));
        }
        if(std::fclose(file)!=0)++ioErrors;
    });
    return true;
}
// Call only after runtime producers have joined.
inline void Stop() {
    enabled=false;stopping.store(true,std::memory_order_release);
    if(writer.joinable())writer.join();
}
struct Lifetime { ~Lifetime(){Stop();} };
// Function-local registration is guaranteed when Start is called, including on MSVC.
inline void EnsureLifetime() { static Lifetime lifetime; }
}
