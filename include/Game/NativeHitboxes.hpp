#pragma once
#include <Windows.h>
#include <array>
#include <cstring>
#include <vector>
#include "Game/ModelShapes.hpp"
#include "Game/ModelWorldTransform.hpp"
#include "Game/SkeletonRead.hpp"
#include "Game/GeometryPosePlan.hpp"

namespace OW::NativeHitboxes {
inline bool Pointer(uint64_t p) { return p>=0x10000 && p<0x800000000000 && !(p&7); }
template<class T> T Field(const void* p,size_t at) { T v{};std::memcpy(&v,static_cast<const uint8_t*>(p)+at,sizeof(v));return v; }
struct Layout {
    uint64_t modelBase=0,revision=0,root=0,spheres=0,capsules=0;
    uint32_t sphereCount=0,capsuleCount=0;
    std::array<uint8_t,48> header{};
    // A read plan only. Current content is reread and its binding is resolved
    // against current, complete pose ranges before publishing each sample.
    std::vector<uint16_t> boneIndices;
    bool valid=false;
    bool Matches(uint64_t model,uint64_t rev) const { return valid && model==modelBase && rev && rev==revision; }
};
inline bool CollectBindings(std::vector<uint16_t>& out, std::span<const uint8_t> spheres,
                            std::span<const uint8_t> capsules, size_t boneCount) {
    out.clear();
    if (!boneCount || boneCount > Skeleton::MaxBones || spheres.size()%64 || capsules.size()%80) return false;
    const auto collect=[&](std::span<const uint8_t> bytes,size_t stride,size_t at) {
        for(size_t offset=0;offset<bytes.size();offset+=stride) {
            const auto index=Field<uint16_t>(bytes.data()+offset,at);
            if(index>=boneCount)return false;
            out.push_back(index);
        }
        return true;
    };
    if(!collect(spheres,64,48) || !collect(capsules,80,64)) { out.clear(); return false; }
    std::sort(out.begin(),out.end());
    out.erase(std::unique(out.begin(),out.end()),out.end());
    return true;
}
template<class Read> bool LoadLayout(Layout& out,uint64_t model,uint64_t revision,size_t boneCount,Read&& read) {
    out={};out.modelBase=model;out.revision=revision;
    if(!model || !revision || !read(model+0xC8,&out.root,8) || !Pointer(out.root) ||
       !read(out.root,out.header.data(),out.header.size()))return false;
    out.spheres=Field<uint64_t>(out.header.data(),8);out.capsules=Field<uint64_t>(out.header.data(),16);
    out.sphereCount=Field<uint32_t>(out.header.data(),40);out.capsuleCount=Field<uint32_t>(out.header.data(),44);
    out.valid=out.sphereCount<=128 && out.capsuleCount<=128 &&
        (!out.sphereCount || Pointer(out.spheres)) && (!out.capsuleCount || Pointer(out.capsules));
    if(!out.valid)return false;
    std::vector<uint8_t> spheres(size_t(out.sphereCount)*64),capsules(size_t(out.capsuleCount)*80);
    out.valid=(!spheres.size() || read(out.spheres,spheres.data(),spheres.size())) &&
        (!capsules.size() || read(out.capsules,capsules.data(),capsules.size())) &&
        CollectBindings(out.boneIndices,spheres,capsules,boneCount);
    return out.valid;
}
struct Sample {
    uint64_t modelBase=0,root=0,skeletonRevision=0;
    uint32_t tick=0;
    ModelWorld::Sample world{};
    std::vector<ModelShapes::Shape> shapes;
    std::vector<ModelShapes::Pose> poses;
    bool Matches(uint64_t model,uint64_t revision) const {
        return tick && world.valid && model==modelBase && revision && revision==skeletonRevision && !shapes.empty() && shapes.size()==poses.size();
    }
};
struct Capture {
    Layout layout{};
    uint64_t observedRoot=0;
    std::array<uint8_t,48> observedHeader{};
    std::vector<uint8_t> spheres,capsules;
    DWORD rootBytes=0,headerBytes=0,sphereBytes=0,capsuleBytes=0;
    bool prepared=false;
    void Reset(const Layout& value) {
        layout=value;observedRoot=0;observedHeader={};
        rootBytes=headerBytes=sphereBytes=capsuleBytes=0;prepared=false;
        spheres.clear();capsules.clear();
        if(!value.valid)return;
        spheres.resize(size_t(value.sphereCount)*64);capsules.resize(size_t(value.capsuleCount)*80);prepared=true;
    }
    bool Complete() const {
        return prepared && rootBytes==8 && headerBytes==48 && observedRoot==layout.root && observedHeader==layout.header &&
            sphereBytes==spheres.size() && capsuleBytes==capsules.size();
    }
    bool RefreshBindings(Layout& target,size_t boneCount) const {
        if(!Complete() || !target.Matches(layout.modelBase,layout.revision) || target.root!=layout.root)return false;
        return CollectBindings(target.boneIndices,spheres,capsules,boneCount);
    }
    Sample Commit(uint64_t revision,uint32_t tick,const ModelWorld::Sample& world,
                  const PosePlan& plan, const ModelShapes::Model* model = nullptr) const {
        Sample out{};
        if(!Complete() || revision!=layout.revision || !world.valid || world.modelBase!=layout.modelBase)return out;
        out.modelBase=layout.modelBase;out.root=layout.root;out.skeletonRevision=revision;out.tick=tick;
        out.world=world;
        out.shapes.reserve(layout.sphereCount+layout.capsuleCount);
        out.poses.reserve(layout.sphereCount+layout.capsuleCount);
        size_t shapeOrdinal = 0;
        const auto append=[&](const std::vector<uint8_t>& data,size_t stride,size_t indexAt,size_t radiusAt,bool sphere) {
            for(size_t at=0;at<data.size();at+=stride) {
                const auto index=Field<uint16_t>(data.data()+at,indexAt);
                ModelShapes::Pose pose{};
                if(!plan.Read(index,pose) || !pose.Valid())return false;
                ModelShapes::Shape shape{};
                shape.boneId = model && shapeOrdinal < model->shapes.size()
                    ? model->shapes[shapeOrdinal].boneId : -1;
                shape.a=Field<Vector3>(data.data()+at,0);shape.b=sphere?shape.a:Field<Vector3>(data.data()+at,16);
                shape.radius=Field<float>(data.data()+at,radiusAt);
                if(!std::isfinite(shape.radius) || shape.radius<=0 || shape.radius>10)return false;
                for(auto p:{shape.a,shape.b})if(!std::isfinite(p.X)||!std::isfinite(p.Y)||!std::isfinite(p.Z)||p.Size()>20)return false;
                out.shapes.push_back(shape);out.poses.push_back(pose);
                ++shapeOrdinal;
            }
            return true;
        };
        // Native collections can contain several damage/activation classes; preserve
        // the decoded set for this display trial without inventing activation flags.
        if(!append(capsules,80,64,32,false) || !append(spheres,64,48,16,true))return {};
        return out;
    }
};
}
