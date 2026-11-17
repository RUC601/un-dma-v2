#include "Features/visuals/HitboxContour.hpp"
#include "Game/ModelShapes.hpp"
#include "Renderer/Renderer.hpp"
#include <algorithm>
#include <cfloat>
#include <cmath>
#include <vector>

namespace OverlayRenderDetail {
namespace {

struct ScreenCircle { Vector2 center{}; float radius{}; };

bool Reasonable(Vector2 p) {
    return std::isfinite(p.X) && std::isfinite(p.Y) &&
        p.X>=-OW::WX*4.f && p.X<=OW::WX*4.f &&
        p.Y>=-OW::WY*4.f && p.Y<=OW::WY*4.f;
}

}

bool DrawNativeHitboxContour(const OW::c_entity& entity,
                             const OW::Matrix& view,
                             ImU32 outline,
                             const ImVec4& fill,
                             float thickness,
                             Vector2 offset,
                             bool drawContour,
                             bool drawFill) {
    const auto& native=entity.native_hitboxes;
    if(!entity.HasCurrentModelWorldTransform() ||
       !native.Matches(entity.VelocityBase,entity.skeleton_revision) ||
       GetTickCount()-native.tick>100)return false;
    const auto delta=entity.pos-native.world.sampleAnchor;
    std::vector<ScreenCircle> samples;
    samples.reserve(native.shapes.size()*5);
    for(size_t i=0;i<native.shapes.size();++i) {
        const auto& shape=native.shapes[i];
        const auto world=[&](Vector3 p){return native.world.TransformPoint(OW::ModelShapes::Transform(p,native.poses[i]))+delta;};
        const float length=(shape.b-shape.a).Size();
        const int count=length>0.0001f?5:1;
        for(int n=0;n<count;++n) {
            const float t=count==1?0.f:float(n)/float(count-1);
            const Vector3 p=shape.a+(shape.b-shape.a)*t;
            Vector2 center{};
            if(!view.WorldToScreen(world(p),&center,Vector2(OW::WX,OW::WY)) || !Reasonable(center))continue;
            float radius=0;
            for(const Vector3 axis:{Vector3(1,0,0),Vector3(0,1,0),Vector3(0,0,1)}) {
                Vector2 edge{};
                if(view.WorldToScreen(world(p+axis*shape.radius),&edge,Vector2(OW::WX,OW::WY))) {
                    radius=(std::max)(radius,std::fabs(edge.X-center.X));
                    radius=(std::max)(radius,std::fabs(edge.Y-center.Y));
                }
                if(view.WorldToScreen(world(p-axis*shape.radius),&edge,Vector2(OW::WX,OW::WY))) {
                    radius=(std::max)(radius,std::fabs(edge.X-center.X));
                    radius=(std::max)(radius,std::fabs(edge.Y-center.Y));
                }
            }
            if(std::isfinite(radius))samples.push_back({center+offset,(std::clamp)(radius,1.5f,200.f)});
        }
    }
    if(samples.empty())return false;
    float minX=FLT_MAX,minY=FLT_MAX,maxX=-FLT_MAX,maxY=-FLT_MAX;
    for(const auto& c:samples){minX=(std::min)(minX,c.center.X-c.radius);minY=(std::min)(minY,c.center.Y-c.radius);
        maxX=(std::max)(maxX,c.center.X+c.radius);maxY=(std::max)(maxY,c.center.Y+c.radius);}
    const float width=maxX-minX,height=maxY-minY;
    if(!(width>0&&height>0)||!std::isfinite(width)||!std::isfinite(height))return false;
    const float step=(std::max)(2.f,(std::max)(height/96.f,width/64.f));
    const int cols=(std::clamp)(int(std::ceil(width/step))+1,2,160),rows=(std::clamp)(int(std::ceil(height/step))+1,2,320);
    const size_t stride=size_t(cols+1);std::vector<float> grid(stride*size_t(rows+1),-FLT_MAX);
    for(const auto& c:samples) {
        const int x0=(std::max)(0,int((c.center.X-c.radius-minX)/step)-1),x1=(std::min)(cols,int((c.center.X+c.radius-minX)/step)+1);
        const int y0=(std::max)(0,int((c.center.Y-c.radius-minY)/step)-1),y1=(std::min)(rows,int((c.center.Y+c.radius-minY)/step)+1);
        for(int y=y0;y<=y1;++y)for(int x=x0;x<=x1;++x) {
            const float px=minX+x*step,py=minY+y*step;
            grid[size_t(y)*stride+size_t(x)]=(std::max)(grid[size_t(y)*stride+size_t(x)],c.radius-std::hypot(px-c.center.X,py-c.center.Y));
        }
    }
    auto value=[&](int x,int y){return grid[size_t(y)*stride+size_t(x)];};
    auto interp=[](float a,float b){const float d=b-a;return std::fabs(d)<1e-5f?.5f:(std::clamp)(-a/d,0.f,1.f);};
    bool drew=false;
    const Render::Color contourColor((outline>>IM_COL32_R_SHIFT)&255,(outline>>IM_COL32_G_SHIFT)&255,
        (outline>>IM_COL32_B_SHIFT)&255,(outline>>IM_COL32_A_SHIFT)&255);
    auto* draw=ImGui::GetForegroundDrawList();
    for(int y=0;y<rows;++y)for(int x=0;x<cols;++x) {
        const float a=value(x,y),b=value(x+1,y),c=value(x+1,y+1),d=value(x,y+1);
        const int code=(a>0?1:0)|(b>0?2:0)|(c>0?4:0)|(d>0?8:0);if(!code)continue;
        drew=true;
        if(drawFill&&code==15)draw->AddRectFilled(ImVec2(minX+x*step,minY+y*step),ImVec2(minX+(x+1)*step,minY+(y+1)*step),ImGui::ColorConvertFloat4ToU32(fill));
        const Vector2 p[4]={{minX+x*step+interp(a,b)*step,minY+y*step},{minX+(x+1)*step,minY+y*step+interp(b,c)*step},{minX+x*step+interp(d,c)*step,minY+(y+1)*step},{minX+x*step,minY+y*step+interp(a,d)*step}};
        auto line=[&](int i,int j){if(drawContour)Render::DrawLine(p[i],p[j],contourColor,thickness);};
        static constexpr int edges[16][4]={{-1,-1,-1,-1},{3,0,-1,-1},{0,1,-1,-1},{3,1,-1,-1},{1,2,-1,-1},{3,2,0,1},{0,2,-1,-1},{3,2,-1,-1},{2,3,-1,-1},{0,2,-1,-1},{0,1,2,3},{1,2,-1,-1},{1,3,-1,-1},{0,1,-1,-1},{3,0,-1,-1},{-1,-1,-1,-1}};
        if(edges[code][0]>=0)line(edges[code][0],edges[code][1]);
        if(edges[code][2]>=0)line(edges[code][2],edges[code][3]);
    }
    return drew;
}

}
