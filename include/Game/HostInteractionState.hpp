#pragma once
#include <atomic>
#include <cstdint>
#include <memory>
namespace V2::HostInteraction {
enum class Source { Unavailable, VerifiedHost };
struct Sample {
    Source source=Source::Unavailable;
    uint64_t connectionEpoch=0;
    uint32_t tick=0;
    bool chatKnown=false, chatOpen=false, focusKnown=false, gameFocused=false;
};
inline std::atomic<std::shared_ptr<const Sample>> published{std::make_shared<Sample>()};
inline std::atomic<bool> paused{false};
inline void Publish(Sample value){published.store(std::make_shared<Sample>(value),std::memory_order_release);}
inline bool Fresh(const Sample& s,uint64_t epoch,uint32_t now){return s.source==Source::VerifiedHost&&s.connectionEpoch==epoch&&s.tick&&uint32_t(now-s.tick)<=250;}
inline bool ShouldPause(const Sample& s,bool onChat,bool onFocusLoss,uint64_t epoch,uint32_t now){
    if(!onChat&&!onFocusLoss)return false;
    // Enabled policies fail closed if a formerly available host sample becomes stale.
    if(!Fresh(s,epoch,now))return true;
    return (onChat&&(!s.chatKnown||s.chatOpen))||(onFocusLoss&&(!s.focusKnown||!s.gameFocused));
}
}
