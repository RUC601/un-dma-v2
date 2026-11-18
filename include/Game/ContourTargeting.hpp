#pragma once
#include <algorithm>
#include <cmath>
#include <limits>
#include <optional>
#include <vector>
#include "Game/ContourContact.hpp"
#include "Game/FovGeometry.hpp"
#include "Game/TriggerBoneSelection.hpp"
#include "vision/ContourTypes.hpp"

namespace OW::Contour {
inline std::uint16_t RenderSlotsForMask(SkeletonBoneMask mask) {
    std::uint16_t slots=0;
    for(std::size_t i=0;i<16;++i) if(SkeletonBoneMaskIncludesRenderSlot(mask,i)) slots|=std::uint16_t(1u<<i);
    return slots;
}
struct ProjectedBone {
    int id = 0;
    std::size_t slot = 0;
    Vector3 world{};
    Vector2 screen{};
    float supportRadiusPx = 0;
};
struct ProjectedActor {
    Identity identity{};
    std::vector<ProjectedBone> bones;
    // The sampled entity root bounds the component even when limb bones are
    // unavailable. It is never a selectable bone or contact support point.
    std::optional<Vector2> root;
};
struct Observation {
    Evidence evidence{};
    int touchDelayMs=0,lostGraceMs=0;
    bool retained = false;
    int boneId = 0;
    Vector3 boneWorld{}, aimWorld{};
    Vector2 pixel{};
    std::uint32_t component = 0;
};

inline bool Project(const Matrix& m, const Vector3& p, int width, int height, Vector2& out) {
    const float w = m.m14*p.X + m.m24*p.Y + m.m34*p.Z + m.m44;
    if (width <= 0 || height <= 0 || !std::isfinite(w) || w <= .001f) return false;
    out = {((m.m11*p.X+m.m21*p.Y+m.m31*p.Z+m.m41)/w+1.f)*.5f*width,
        (1.f-(m.m12*p.X+m.m22*p.Y+m.m32*p.Z+m.m42)/w)*.5f*height};
    return std::isfinite(out.X) && std::isfinite(out.Y) && out.X >= 0 && out.Y >= 0 &&
        out.X < width && out.Y < height;
}

// Invert the exact UN view-projection, then use the selected bone's depth.
// This preserves zoom/aspect/handedness and never substitutes a pixel delta
// for an angular delta. Reprojection is checked before exposing a point.
inline bool PixelAtBoneDepth(const Matrix& m, const Vector2& pixel, int width, int height,
                             const Vector3& bone, Vector3& out) {
    if (width <= 0 || height <= 0 || !FovGeometry::IsFiniteVector(bone)) return false;
    using namespace DirectX;
    const XMMATRIX matrix(m.m11,m.m12,m.m13,m.m14,m.m21,m.m22,m.m23,m.m24,
        m.m31,m.m32,m.m33,m.m34,m.m41,m.m42,m.m43,m.m44);
    XMVECTOR determinant{};
    const XMMATRIX inverse = XMMatrixInverse(&determinant,matrix);
    const float det = XMVectorGetX(determinant);
    if (!std::isfinite(det) || std::fabs(det) < 1e-12f) return false;
    const float w = m.m14*bone.X+m.m24*bone.Y+m.m34*bone.Z+m.m44;
    const float z = m.m13*bone.X+m.m23*bone.Y+m.m33*bone.Z+m.m43;
    if (!std::isfinite(w) || w <= .001f || !std::isfinite(z)) return false;
    const XMVECTOR p = XMVector4Transform(XMVectorSet(pixel.X*2.f/width-1.f,
        1.f-pixel.Y*2.f/height,z/w,1.f),inverse);
    const float outW = XMVectorGetW(p);
    if (!std::isfinite(outW) || std::fabs(outW) < 1e-9f) return false;
    out = {XMVectorGetX(p)/outW,XMVectorGetY(p)/outW,XMVectorGetZ(p)/outW};
    Vector2 check{};
    return FovGeometry::IsFiniteVector(out) && Project(m,out,width,height,check) &&
        check.Distance(pixel) <= .25f;
}

inline float DistanceSquared(const Vector2& a, const Vector2& b) {
    const float x=a.X-b.X,y=a.Y-b.Y; return x*x+y*y;
}

inline float ComponentDistance(const V2::Vision::Frame& frame,
    const V2::Vision::Component& component,const ProjectedActor& actor,
    std::uint16_t allowedMask, bool restrictMask, int& boneIndex, Vector2& pixel,float radius) {
    float best=(std::numeric_limits<float>::max)(); boneIndex=-1;
    if(actor.bones.empty() || frame.componentIds.size()!=frame.mask.size()) return best;
    float actorLeft=actor.bones.front().screen.X,actorRight=actorLeft;
    float actorTop=actor.bones.front().screen.Y,actorBottom=actorTop;
    for(const auto& bone:actor.bones) {
        actorLeft=(std::min)(actorLeft,bone.screen.X);actorRight=(std::max)(actorRight,bone.screen.X);
        actorTop=(std::min)(actorTop,bone.screen.Y);actorBottom=(std::max)(actorBottom,bone.screen.Y);
    }
    if(actor.root && std::isfinite(actor.root->X) && std::isfinite(actor.root->Y) &&
        actor.root->X>=0 && actor.root->X<frame.width && actor.root->Y>=0 && actor.root->Y<frame.height) {
        actorLeft=(std::min)(actorLeft,actor.root->X);actorRight=(std::max)(actorRight,actor.root->X);
        actorTop=(std::min)(actorTop,actor.root->Y);actorBottom=(std::max)(actorBottom,actor.root->Y);
    }
    // Reject unrelated huge white UI regions, including components that merely
    // pass near a bone while spanning well outside this actor's silhouette.
    if(component.bounds.left<actorLeft-radius || component.bounds.right>actorRight+radius ||
        component.bounds.top<actorTop-radius || component.bounds.bottom>actorBottom+radius) return best;
    for (std::size_t i=0;i<actor.bones.size();++i) {
        const auto& bone=actor.bones[i];
        if (restrictMask && !(allowedMask & (std::uint16_t(1) << bone.slot))) continue;
        const float localRadius=restrictMask ? (std::min)(radius,bone.supportRadiusPx) : radius;
        if(localRadius<=0) continue;
        const int left=(std::max)({0,component.bounds.left,int(std::floor(bone.screen.X-localRadius))});
        const int right=(std::min)({frame.width,component.bounds.right,int(std::ceil(bone.screen.X+localRadius))+1});
        const int top=(std::max)({0,component.bounds.top,int(std::floor(bone.screen.Y-localRadius))});
        const int bottom=(std::min)({frame.height,component.bounds.bottom,int(std::ceil(bone.screen.Y+localRadius))+1});
        for(int y=top;y<bottom;++y) for(int x=left;x<right;++x) {
            const auto index=static_cast<std::size_t>(y)*frame.width+x;
            if(index>=frame.componentIds.size()||frame.componentIds[index]!=component.id) continue;
            const Vector2 point(float(x)+.5f,float(y)+.5f);
            const float distance=DistanceSquared(bone.screen,point);
            if (distance<=localRadius*localRadius && distance<best) { best=distance; boneIndex=static_cast<int>(i); pixel=point; }
        }
    }
    return best;
}

inline void ContactSupport(const V2::Vision::Frame& frame,const V2::Vision::Component& component,
                           const ProjectedActor& actor,std::size_t selectedBone,
                           float associationRadius,float radius, bool& touching, bool& inside) {
    const Vector2 center(frame.width*.5f,frame.height*.5f);
    touching=false; inside=false;
    if(selectedBone>=actor.bones.size() || frame.componentIds.size()!=frame.mask.size()) return;
    const auto& bone=actor.bones[selectedBone];
    const float selectedDistance=DistanceSquared(center,bone.screen);
    const float localRadius=(std::min)(associationRadius,bone.supportRadiusPx);
    if(localRadius<=0 || selectedDistance>localRadius*localRadius) return;
    // A connected full-body outline is not permission to touch the feet when
    // the selected local aim point is the head. Resolve spatial ownership again
    // at the crosshair, independently of which component contains the pixel.
    for(std::size_t i=0;i<actor.bones.size();++i)
        if(i!=selectedBone && DistanceSquared(center,actor.bones[i].screen)+1.f<selectedDistance) return;
    const int cx=frame.width/2,cy=frame.height/2;
    const int rr=static_cast<int>(std::ceil(radius));
    for(int y=(std::max)(0,cy-rr);y<=(std::min)(frame.height-1,cy+rr);++y)
        for(int x=(std::max)(0,cx-rr);x<=(std::min)(frame.width-1,cx+rr);++x) {
            const auto index=static_cast<std::size_t>(y)*frame.width+x;
            if(frame.componentIds[index]==component.id &&
                DistanceSquared({x+.5f,y+.5f},center)<=radius*radius) touching=true;
        }
    if(touching || !component.bounds.Contains(cx,cy)) return;
    const int left=(std::max)(0,component.bounds.left-1),top=(std::max)(0,component.bounds.top-1);
    const int right=(std::min)(frame.width,component.bounds.right+1),bottom=(std::min)(frame.height,component.bounds.bottom+1);
    const int width=right-left,height=bottom-top;
    // Bounded exact background connectivity. A concave/open/cropped contour
    // remains outside; neither its bounding box nor four cardinal hits fills it.
    if(width<=0||height<=0||static_cast<std::uint64_t>(width)*height>262144) return;
    std::vector<std::uint8_t> visited(static_cast<std::size_t>(width)*height,0);
    std::vector<std::uint32_t> pending;
    pending.push_back(static_cast<std::uint32_t>((cy-top)*width+cx-left));
    visited[pending.front()]=1;
    for(std::size_t next=0;next<pending.size();++next) {
        const int x=left+pending[next]%width,y=top+pending[next]/width;
        if(x==left||x==right-1||y==top||y==bottom-1) return;
        for(int dy=-1;dy<=1;++dy) for(int dx=-1;dx<=1;++dx) {
            if(!dx&&!dy) continue;
            const int nx=x+dx,ny=y+dy;
            const auto local=static_cast<std::size_t>(ny-top)*width+nx-left;
            if(visited[local] || frame.componentIds[static_cast<std::size_t>(ny)*frame.width+nx]==component.id) continue;
            visited[local]=1;pending.push_back(static_cast<std::uint32_t>(local));
        }
    }
    inside=true;
}

inline Observation Associate(const V2::Vision::Frame& frame,const V2::Vision::Settings& settings,
    const Matrix& view,const std::vector<ProjectedActor>& actors,std::size_t actorIndex,
    std::uint16_t allowedSlots,double nowMs) {
    Observation result{};
    if(actorIndex>=actors.size() || !allowedSlots || frame.width<=0 || frame.height<=0) return result;
    const auto& actor=actors[actorIndex]; result.evidence.identity=actor.identity;
    result.touchDelayMs=settings.touchDelayMs;result.lostGraceMs=settings.lostGraceMs;
    result.evidence.epoch=frame.epoch;result.evidence.revision=frame.settingsRevision;
    result.evidence.sequence=frame.sequence;result.evidence.capturedMs=frame.arrivalMs;
    if(!V2::Vision::IsFresh(frame,settings,nowMs)) return result;
    result.evidence.fresh=true;
    const float maxDistance=settings.associationRadiusPx*settings.associationRadiusPx;
    float best=(std::numeric_limits<float>::max)();
    for(const auto& component:frame.components) {
        int selectedBone=-1;Vector2 selectedPixel{};
        const float distance=ComponentDistance(frame,component,actor,allowedSlots,true,selectedBone,selectedPixel,settings.associationRadiusPx);
        if(selectedBone<0 || distance>maxDistance || distance>=best) continue;
        int anyBone=-1;Vector2 anyPixel{};
        const float ownerDistance=ComponentDistance(frame,component,actor,0,false,anyBone,anyPixel,settings.associationRadiusPx);
        bool ambiguous=false;
        for(std::size_t j=0;j<actors.size();++j) {
            if(j==actorIndex || actors[j].identity==actor.identity) continue;
            int otherBone=-1;Vector2 otherPixel{};
            const float other=ComponentDistance(frame,component,actors[j],0,false,otherBone,otherPixel,settings.associationRadiusPx);
            // A shared/overlapping component cannot be assigned by array order.
            if(otherBone>=0 && other<=maxDistance && other<=ownerDistance*1.5f+64.f) {ambiguous=true;break;}
        }
        if(ambiguous) continue;
        const auto& bone=actor.bones[static_cast<std::size_t>(selectedBone)];
        Vector3 aim{};
        if(!PixelAtBoneDepth(view,selectedPixel,frame.width,frame.height,bone.world,aim)) continue;
        result.boneId=bone.id;result.boneWorld=bone.world;result.aimWorld=aim;
        result.pixel=selectedPixel;result.component=component.id;
        result.evidence.fresh=true;result.evidence.exposed=true;
        ContactSupport(frame,component,actor,static_cast<std::size_t>(selectedBone),settings.associationRadiusPx,
            settings.contactRadiusPx,result.evidence.touching,result.evidence.inside);
        best=distance;
    }
    return result;
}
} // namespace OW::Contour
