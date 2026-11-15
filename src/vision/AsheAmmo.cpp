#include "vision/AsheAmmo.hpp"
#include "vision/ContourService.hpp"
#include "HudAmmoDetail.hpp"
#include <algorithm>
#include <array>
#include <bit>
#include <cmath>
#include <limits>
#include <vector>

namespace V2::Vision {
namespace {
using namespace HudAmmoDetail;
bool IsRifle(const HudMask& mask,const Piece& p,double& confidence) {
    const auto rows=Normalize(mask,p,64);
    confidence=0;
    for(const auto& model:AsheAmmoDetail::kTemplates)
        if(model.digit<0)confidence=(std::max)(confidence,Match(rows,p,model));
    return confidence>=0.70;
}
}

void DetectAsheAmmo(Frame& frame) {
    frame.asheAmmoRemaining=-1;
    frame.asheAmmoConfidence=0.0;
    HudMask mask;
    std::vector<Piece> pieces;
    std::vector<Candidate> digits;
    if (!Prepare(frame, mask, pieces, digits)) return;
    int recognized=-1;
    double recognizedConfidence=0;
    for(const auto& one:digits) {
        if(one.digit.value!=1)continue;
        const auto& a=pieces[one.index];const double h=Height(a);
        for(const auto& two:digits) {
            if(two.digit.value!=2)continue;
            const auto& b=pieces[two.index];
            if(b.left-a.right<h*0.12 || b.left-a.right>h*0.65 ||
                std::abs(b.bottom-a.bottom)>h*0.3 || std::abs(Height(b)-h)>h*0.3)continue;
            double rifleConfidence=0;
            for(const auto& icon:pieces) {
                if(Width(icon)<h*5.5 || Width(icon)>h*9.0 || Height(icon)<h*1.5 || Height(icon)>h*2.6 ||
                    a.left-icon.right<h*2.8 || a.left-icon.right>h*5.7 || std::abs(icon.bottom-a.bottom)>h*0.45)continue;
                double score=0;
                if(IsRifle(mask,icon,score))rifleConfidence=(std::max)(rifleConfidence,score);
            }
            if(rifleConfidence==0)continue;
            std::vector<Candidate> current;
            bool animatedOrClipped=false;
            for(const auto& digit:digits) {
                const auto& p=pieces[digit.index];
                if(p.left<a.left-h*4.1 || p.right>a.left-h*0.6 ||
                    p.bottom<a.top-h*1.5 || p.top>a.bottom+h*0.5)continue;
                // Reload digits can bounce independently. Ignoring a raised
                // leading "1" would turn a displayed "10" into a false zero.
                // Reject the whole reading until every nearby glyph settles.
                if(std::abs(p.bottom-a.bottom)>h*0.35 || std::abs(Height(p)-h)>h*0.35) {
                    animatedOrClipped=true;break;
                }
                current.push_back(digit);
            }
            if(animatedOrClipped||current.empty()||current.size()>2)continue;
            std::sort(current.begin(),current.end(),[&](const auto& x,const auto& y){return pieces[x.index].left<pieces[y.index].left;});
            const auto& last=pieces[current.back().index];
            if(a.left-last.right<h*0.75 || a.left-last.right>h*1.6)continue;
            if(current.size()==2) {
                const auto& first=pieces[current.front().index];
                if(current.front().digit.value!=1 || last.left-first.right<0 || last.left-first.right>h*0.6)continue;
            }
            int value=0;double confidence=(std::min)({one.digit.confidence,two.digit.confidence,rifleConfidence});
            for(const auto& d:current) {
                if(d.digit.value<0){value=-1;break;}
                value=value*10+d.digit.value;confidence=(std::min)(confidence,d.digit.confidence);
            }
            if(value<0||value>12)continue;
            if(recognized>=0 && recognized!=value)return;
            recognized=value;recognizedConfidence=(std::max)(recognizedConfidence,confidence);
        }
    }
    frame.asheAmmoRemaining=recognized;
    frame.asheAmmoConfidence=recognized>=0?recognizedConfidence:0;
}

AsheAmmoSnapshot ReadAsheAmmo(bool localIsAshe) {
    AsheAmmoSnapshot result;
    if(!localIsAshe)return result;
    Settings settings;
    const auto frame=GetService().Snapshot(settings);
    if(!frame)return result;
    const double nowMs=MonotonicMs();
    if(!IsFresh(*frame,settings,nowMs))return result;
    result.sequence=frame->sequence;result.epoch=frame->epoch;result.arrivalMs=frame->arrivalMs;
    result.ageMs=nowMs-frame->arrivalMs;
    if(frame->asheAmmoRemaining<0 || frame->asheAmmoRemaining>12)return result;
    result.known=true;result.remaining=frame->asheAmmoRemaining;result.confidence=frame->asheAmmoConfidence;
    return result;
}

bool RequestAsheAmmoCapture() {
    auto& service=GetService();
    const auto status=service.Status();
    if(status.state==ServiceState::Running || status.state==ServiceState::Starting)return true;
    auto settings=service.GetSettings();settings.enabled=true;
    return service.Configure(settings) && service.Start();
}
}
