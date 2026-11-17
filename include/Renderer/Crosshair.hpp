#pragma once
#include "Renderer/VisualSettings.hpp"
#include <algorithm>
#include <cmath>
#include <cstdint>

namespace V2::VisualSettings {
enum class CrosshairPrimitiveKind:uint8_t {Line,Circle};
struct CrosshairPrimitive {
    CrosshairPrimitiveKind kind=CrosshairPrimitiveKind::Line;
    float x1=0,y1=0,x2=0,y2=0,radius=0;
    bool filled=false;
};

template<class Emit>
inline void ForEachCrosshairPrimitive(const CrosshairSettings& settings,float centerX,float centerY,Emit&& emit) {
    const float half=(std::max)(1.0f,settings.size*.5f);
    const float gap=(std::max)(0.0f,settings.gap);
    const float dotRadius=(std::max)(.5f,settings.dotSize*.5f);
    auto line=[&](float x1,float y1,float x2,float y2){emit(CrosshairPrimitive{CrosshairPrimitiveKind::Line,centerX+x1,centerY+y1,centerX+x2,centerY+y2,0,false});};
    auto circle=[&](float x,float y,float radius,bool filled){emit(CrosshairPrimitive{CrosshairPrimitiveKind::Circle,centerX+x,centerY+y,0,0,radius,filled});};
    auto cross=[&](bool separated){
        const float inner=separated?(std::min)(gap,half):0.0f;
        line(-half,0,-inner,0);line(inner,0,half,0);line(0,-half,0,-inner);line(0,inner,0,half);
    };
    const int style=std::clamp(settings.style,0,int(CrosshairStyle::Count)-1);
    switch(static_cast<CrosshairStyle>(style)) {
    case CrosshairStyle::Circle: circle(0,0,half,false); break;
    case CrosshairStyle::Cross: cross(true); break;
    case CrosshairStyle::CircleAndCross: circle(0,0,half,false); cross(true); break;
    case CrosshairStyle::Dot: circle(0,0,dotRadius,true); break;
    case CrosshairStyle::Square:
        line(-half,-half,half,-half);line(half,-half,half,half);line(half,half,-half,half);line(-half,half,-half,-half);break;
    case CrosshairStyle::FourDots:
        circle(-gap-dotRadius,-gap-dotRadius,dotRadius,true);circle(gap+dotRadius,-gap-dotRadius,dotRadius,true);
        circle(-gap-dotRadius,gap+dotRadius,dotRadius,true);circle(gap+dotRadius,gap+dotRadius,dotRadius,true);break;
    case CrosshairStyle::T:
        line(-half,-half*.35f,half,-half*.35f);line(0,-half*.35f,0,half);break;
    default: break;
    }
}
}
