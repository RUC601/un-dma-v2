#include "vision/AsheAmmo.hpp"
#include "vision/ContourService.hpp"
#include "AsheAmmoTemplates.hpp"
#include <algorithm>
#include <array>
#include <bit>
#include <cmath>
#include <limits>
#include <vector>

namespace V2::Vision {
namespace {
struct Piece { int left, top, right, bottom, pixels; };
int Width(const Piece& p) { return p.right-p.left; }
int Height(const Piece& p) { return p.bottom-p.top; }

struct HudMask {
    int left=0, top=0, width=0, height=0;
    std::vector<std::uint8_t> pixels;
    bool At(int x,int y) const {
        return x>=left && y>=top && x<left+width && y<top+height &&
            pixels[static_cast<size_t>(y-top)*width+x-left]!=0;
    }
};

std::array<std::uint64_t,24> Normalize(const HudMask& mask,const Piece& p,int width) {
    std::array<std::uint64_t,24> rows{};
    for(int y=0;y<24;++y)for(int x=0;x<width;++x) {
        const int sx=p.left+(2*x+1)*Width(p)/(2*width);
        const int sy=p.top+(2*y+1)*Height(p)/48;
        if(mask.At(sx,sy))rows[y]|=std::uint64_t{1}<<x;
    }
    return rows;
}

double Match(const std::array<std::uint64_t,24>& rows,const Piece& p,
    const AsheAmmoDetail::GlyphTemplate& model) {
    const double aspect=double(Width(p))/Height(p);
    if(std::abs(aspect/model.aspect-1.0)>0.28)return 0.0;
    int intersection=0,united=0;
    for(size_t y=0;y<rows.size();++y) {
        intersection+=std::popcount(rows[y]&model.rows[y]);
        united+=std::popcount(rows[y]|model.rows[y]);
    }
    return united ? double(intersection)/united : 0.0;
}

struct Digit { int value=-1; double confidence=0.0; };
Digit Recognize(const HudMask& mask,const Piece& p) {
    const auto rows=Normalize(mask,p,16);
    std::array<double,10> scores{};
    for(const auto& model:AsheAmmoDetail::kTemplates)
        if(model.digit>=0)scores[model.digit]=(std::max)(scores[model.digit],Match(rows,p,model));
    int best=0;
    for(int i=1;i<10;++i)if(scores[i]>scores[best])best=i;
    double second=0;
    for(int i=0;i<10;++i)if(i!=best)second=(std::max)(second,scores[i]);
    // An unobserved glyph, clipped glyph, or competing interpretation is unknown.
    if(scores[best]<0.80 || scores[best]-second<0.13)return {};
    return {best,scores[best]};
}

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
    if(frame.width<640 || frame.height<360 || frame.width>8192 || frame.height>8192 ||
        static_cast<size_t>(frame.width)*frame.height>33554432 ||
        frame.pixels.size()!=static_cast<size_t>(frame.width)*frame.height*4)return;
    // Search a broad lower-right region; do not assume the image's dimensions
    // equal the game viewport (window borders and taskbars may be captured).
    HudMask mask;
    mask.left=frame.width*7/10;mask.top=frame.height*7/10;
    mask.width=frame.width-mask.left;mask.height=frame.height-mask.top;
    mask.pixels.resize(static_cast<size_t>(mask.width)*mask.height);
    for(int y=0;y<mask.height;++y)for(int x=0;x<mask.width;++x) {
        const auto at=(static_cast<size_t>(y+mask.top)*frame.width+x+mask.left)*4;
        const auto b=frame.pixels[at],g=frame.pixels[at+1],r=frame.pixels[at+2];
        const int low=(std::min)({b,g,r}),high=(std::max)({b,g,r});
        mask.pixels[static_cast<size_t>(y)*mask.width+x]=low>170 && high-low<45;
    }
    auto unvisited=mask.pixels;
    std::vector<int> pending;
    std::vector<Piece> pieces;
    pending.reserve(2048);pieces.reserve(64);
    for(int y=0;y<mask.height;++y)for(int x=0;x<mask.width;++x) {
        const int at=y*mask.width+x;
        if(!unvisited[at])continue;
        pending.clear();pending.push_back(at);unvisited[at]=0;
        Piece piece{x,y,x+1,y+1,0};
        for(size_t next=0;next<pending.size();++next) {
            const int current=pending[next],cx=current%mask.width,cy=current/mask.width;
            piece.left=(std::min)(piece.left,cx);piece.top=(std::min)(piece.top,cy);
            piece.right=(std::max)(piece.right,cx+1);piece.bottom=(std::max)(piece.bottom,cy+1);
            ++piece.pixels;
            for(int dy=-1;dy<=1;++dy)for(int dx=-1;dx<=1;++dx) {
                const int nx=cx+dx,ny=cy+dy;
                if(nx<0||ny<0||nx>=mask.width||ny>=mask.height)continue;
                const int ni=ny*mask.width+nx;
                if(unvisited[ni]){unvisited[ni]=0;pending.push_back(ni);}
            }
        }
        if(piece.pixels<12)continue;
        piece.left+=mask.left;piece.right+=mask.left;
        piece.top+=mask.top;piece.bottom+=mask.top;
        pieces.push_back(piece);
        if(pieces.size()>1024)return;
    }
    struct Candidate { size_t index; Digit digit; };
    std::vector<Candidate> digits;
    const int minimumHeight=(std::max)(7,frame.height/180);
    const int maximumHeight=(std::max)(16,frame.height/24);
    for(size_t i=0;i<pieces.size();++i) {
        const auto& p=pieces[i];
        if(Height(p)<minimumHeight || Height(p)>maximumHeight || Width(p)>Height(p)*1.2)continue;
        digits.push_back({i,Recognize(mask,p)});
    }
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
