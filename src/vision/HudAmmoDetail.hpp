#pragma once
#include "vision/ContourTypes.hpp"
#include "AsheAmmoTemplates.hpp"
#include <algorithm>
#include <array>
#include <bit>
#include <cmath>
#include <span>
#include <vector>

// Shared segmentation and observed HUD digit glyphs. Extracted unchanged from
// the validated Ashe detector; hero-specific anchors remain separate.
namespace V2::Vision::HudAmmoDetail {
struct Piece { int left, top, right, bottom, pixels; };
inline int Width(const Piece& p) { return p.right-p.left; }
inline int Height(const Piece& p) { return p.bottom-p.top; }

struct HudMask {
    int left=0, top=0, width=0, height=0;
    std::vector<std::uint8_t> pixels;
    bool At(int x,int y) const {
        return x>=left && y>=top && x<left+width && y<top+height &&
            pixels[static_cast<size_t>(y-top)*width+x-left]!=0;
    }
};

inline std::array<std::uint64_t,24> Normalize(const HudMask& mask,const Piece& p,int width) {
    std::array<std::uint64_t,24> rows{};
    for(int y=0;y<24;++y)for(int x=0;x<width;++x) {
        const int sx=p.left+(2*x+1)*Width(p)/(2*width);
        const int sy=p.top+(2*y+1)*Height(p)/48;
        if(mask.At(sx,sy))rows[y]|=std::uint64_t{1}<<x;
    }
    return rows;
}

inline double Match(const std::array<std::uint64_t,24>& rows,const Piece& p,
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
inline Digit Recognize(const HudMask& mask,const Piece& p,
    std::span<const AsheAmmoDetail::GlyphTemplate> extraTemplates = {}) {
    const auto rows=Normalize(mask,p,16);
    std::array<double,10> scores{};
    for(const auto& model:AsheAmmoDetail::kTemplates)
        if(model.digit>=0)scores[model.digit]=(std::max)(scores[model.digit],Match(rows,p,model));
    for(const auto& model:extraTemplates)
        if(model.digit>=0)scores[model.digit]=(std::max)(scores[model.digit],Match(rows,p,model));
    int best=0;
    for(int i=1;i<10;++i)if(scores[i]>scores[best])best=i;
    double second=0;
    for(int i=0;i<10;++i)if(i!=best)second=(std::max)(second,scores[i]);
    // An unobserved glyph, clipped glyph, or competing interpretation is unknown.
    if(scores[best]<0.80 || scores[best]-second<0.13)return {};
    return {best,scores[best]};
}

struct Candidate { size_t index; Digit digit; };
inline bool Prepare(const Frame& frame, HudMask& mask, std::vector<Piece>& pieces,
                    std::vector<Candidate>& digits,
                    std::span<const AsheAmmoDetail::GlyphTemplate> extraTemplates = {}) {
    if(frame.width<640 || frame.height<360 || frame.width>8192 || frame.height>8192 ||
        static_cast<size_t>(frame.width)*frame.height>33554432 ||
        frame.pixels.size()!=static_cast<size_t>(frame.width)*frame.height*4)return false;
    // Search a broad lower-right region; do not assume the image's dimensions
    // equal the game viewport (window borders and taskbars may be captured).
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
        if(pieces.size()>1024)return false;
    }
    const int minimumHeight=(std::max)(7,frame.height/180);
    const int maximumHeight=(std::max)(16,frame.height/24);
    for(size_t i=0;i<pieces.size();++i) {
        const auto& p=pieces[i];
        if(Height(p)<minimumHeight || Height(p)>maximumHeight || Width(p)>Height(p)*1.2)continue;
        digits.push_back({i,Recognize(mask,p,extraTemplates)});
    }
    return true;
}
}
