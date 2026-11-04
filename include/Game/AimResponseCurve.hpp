#pragma once
#include <algorithm>
#include <cmath>
#include "Game/AimSmoothingTiming.hpp"

namespace OW::AimResponseCurve {
struct Points {
    float x1 = .333f, y1 = .333f, x2 = .667f, y2 = .667f;
    bool operator==(const Points&) const = default;
};
inline Points Preset(int index) {
    if (index == 1) return {.42f, 0.f, .58f, 1.f};
    if (index == 2) return {.10f, .85f, .40f, 1.f};
    return {};
}
inline void Normalize(Points& p) {
    const Points d;
    auto sane=[](float v,float fallback) { return std::isfinite(v) ? (std::clamp)(v,0.f,1.f) : fallback; };
    p={sane(p.x1,d.x1),sane(p.y1,d.y1),sane(p.x2,d.x2),sane(p.y2,d.y2)};
}
inline float Cubic(float t,float a,float b) {
    const float u=1-t;
    return 3*u*u*t*a+3*u*t*t*b+t*t*t;
}
// The graph and controller both query y at x; t is not the horizontal input.
inline float Evaluate(Points p,float x) {
    Normalize(p);
    if (!std::isfinite(x) || x<=0) return 0;
    if (x>=1) return 1;
    float t=x;
    for(int i=0;i<8;++i) {
        const float residual=Cubic(t,p.x1,p.x2)-x;
        if(std::abs(residual)<1e-6f) return Cubic(t,p.y1,p.y2);
        const float u=1-t;
        const float derivative=3*u*u*p.x1+6*u*t*(p.x2-p.x1)+3*t*t*(1-p.x2);
        if(std::abs(derivative)<1e-6f) break;
        const float next=t-residual/derivative;
        if(next<0||next>1) break;
        t=next;
    }
    float lo=0,hi=1;
    for(int i=0;i<24;++i) {
        t=(lo+hi)*.5f;
        if(Cubic(t,p.x1,p.x2)<x) lo=t; else hi=t;
    }
    return (std::clamp)(Cubic(t,p.y1,p.y2),0.f,1.f);
}
inline float StepFactor(float error,float fov,float strength,float dt,const Points& p) {
    if(!std::isfinite(error)||!std::isfinite(fov)||!std::isfinite(strength)||
       error<=1e-7f||fov<=1e-7f||strength<=0) return 0;
    const float r=error/fov;
    const float factor=(std::clamp)(strength*Evaluate(p,(std::min)(r,1.f))/r,0.f,1.f);
    return AimSmoothingTiming::NormalizePerTickFactor(factor,dt);
}
}
