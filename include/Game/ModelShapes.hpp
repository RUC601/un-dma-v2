#pragma once
#include "Utils/Types.hpp"
#include <array>
#include <cmath>
#include <cstdint>
#include <span>
#include <vector>

namespace OW::ModelShapes {
struct Shape { int boneId; Vector3 a, b; float radius; };
#include "Game/TracerModelShapes.inl"
struct Model { uint64_t heroId, modelId; std::span<const Shape> shapes; std::span<const uint32_t> boneIds; };
#include "Game/HeroModelShapes.inl"
inline bool Supported(uint64_t hero) {
    for(const auto& m:kModels)if(m.heroId==hero)return true;
    return false;
}
inline int ResolveModel(uint64_t hero,const uint32_t* ids,size_t count) {
    int match=-1; size_t best=0;
    for(size_t i=0;i<std::size(kModels);++i) {
        const auto& m=kModels[i];
        if(m.heroId!=hero)continue;
        auto present=[&](uint32_t id) {
            for(size_t j=0;j<count;++j)if(static_cast<uint16_t>(ids[j])==id)return true;
            return false;
        };
        size_t bindings=0;
        for(const auto& shape:m.shapes)if(present(shape.boneId))++bindings;
        if(bindings==0)continue;
        size_t shared=0;
        for(auto id:m.boneIds)if(present(id))++shared;
        // Cosmetic bones vary by skin. Missing bindings are skipped; prefer
        // the body skeleton with the largest ID overlap, never a small prop.
        if(shared<best || m.boneIds.size()<16)continue;
        if(shared==best){match=-1;continue;}
        best=shared;match=static_cast<int>(i);
    }
    return match;
}

// Candidate runtime SQT record: orientation +0, scale +0x10, translation +0x20.
// shortcut: layout/space needs live calibration before claiming model alignment.
struct Pose {
    float q[4]{};
    float scale[4]{};
    float translation[4]{};
    bool Valid() const {
        float norm=0;
        for(float x:q) { if(!std::isfinite(x))return false; norm+=x*x; }
        if(std::fabs(norm-1.f)>.03f)return false;
        for(int i=0;i<3;++i)
            if(!std::isfinite(scale[i]) || scale[i]<=0 || scale[i]>10 ||
               !std::isfinite(translation[i]) || std::fabs(translation[i])>20)return false;
        return true;
    }
};
static_assert(sizeof(Pose)==0x30);
struct Sample {
    std::vector<Pose> poses;
    std::vector<uint8_t> valid;
    int modelIndex=-1;
    uint32_t tick=0;
    uint64_t boneData=0;
};
inline int FindBone(const uint32_t* ids,size_t count,int id) {
    for(size_t i=0;i<count;++i)
        if(static_cast<uint16_t>(ids[i])==id)return static_cast<int>(i);
    return -1;
}
inline Vector3 Cross(const Vector3& a,const Vector3& b) {
    return {a.Y*b.Z-a.Z*b.Y,a.Z*b.X-a.X*b.Z,a.X*b.Y-a.Y*b.X};
}
inline Vector3 Transform(Vector3 p,const Pose& pose) {
    p={p.X*pose.scale[0],p.Y*pose.scale[1],p.Z*pose.scale[2]};
    Vector3 q{pose.q[0],pose.q[1],pose.q[2]};
    p=p+Cross(q,p)*(2*pose.q[3])+Cross(q,Cross(q,p))*2;
    return p+Vector3(pose.translation[0],pose.translation[1],pose.translation[2]);
}

// Emits local 3D segments; the caller transforms each vertex before projection.
template<class Line> void Wire(const Shape& s,Line line) {
    constexpr int sides=12, capSteps=4;
    constexpr float pi=3.14159265358979323846f;
    Vector3 d=s.b-s.a;
    const float length=d.Size();
    const bool sphere=length<1.e-6f;
    Vector3 axis=sphere?Vector3(0,1,0):d*(1.f/length);
    Vector3 u=Cross(axis,std::fabs(axis.Y)<.9f?Vector3(0,1,0):Vector3(1,0,0));
    u=u*(1.f/u.Size());
    const Vector3 w=Cross(axis,u);
    auto radial=[&](float phi){return u*std::cos(phi)+w*std::sin(phi);};
    auto ring=[&](Vector3 c,float r){
        for(int i=0;i<sides;++i)line(c+radial(2*pi*i/sides)*r,c+radial(2*pi*(i+1)/sides)*r);
    };
    ring(s.a,s.radius);
    if(!sphere)ring(s.b,s.radius);
    for(int i=0;i<4;++i) {
        Vector3 radialAxis=radial(pi*i/2);
        if(!sphere)line(s.a+radialAxis*s.radius,s.b+radialAxis*s.radius);
        for(int end=0;end<2;++end) {
            Vector3 c=end?s.b:s.a;
            const float sign=end?1.f:-1.f;
            for(int j=0;j<capSteps;++j) {
                auto point=[&](int k){float t=pi*.5f*k/capSteps;return c+(radialAxis*std::cos(t)+axis*(sign*std::sin(t)))*s.radius;};
                line(point(j),point(j+1));
            }
        }
    }
}
}
