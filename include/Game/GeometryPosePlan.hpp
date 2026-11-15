#pragma once
#include <algorithm>
#include <array>
#include <cstring>
#include <span>
#include <vector>
#include "Game/ModelShapes.hpp"
#include "Game/SkeletonRead.hpp"

namespace OW::NativeHitboxes {
// One current scatter payload serves semantic translations and native poses.
// Plans retain capacity, never read completion or a pose from the previous batch.
struct PosePlan {
    std::vector<Skeleton::PoseRange> ranges;
    std::vector<uint8_t> data;
    void Clear() { ranges.clear(); data.clear(); }
    bool Build(std::span<const int> semantic, std::span<const uint16_t> native, size_t boneCount, uint64_t bonesBase) {
        Clear();
        if (!boneCount || boneCount > Skeleton::MaxBones) return false;
        std::array<bool, Skeleton::MaxBones> selected{};
        for (int index : semantic) {
            if (index < 0 || size_t(index) >= boneCount) return false;
            selected[index] = true;
        }
        for (auto index : native) {
            if (index >= boneCount) return false;
            selected[index] = true;
        }
        size_t bytes = 0;
        for (size_t first = 0; first < boneCount;) {
            if (!selected[first]) { ++first; continue; }
            size_t last = first;
            // Scatter operates on pages. Merge holes only within a page already
            // needed by a selected pose, avoiding both redundant requests and
            // new pages. A 48-byte pose crossing a page remains one exact read.
            while (last + 1 < boneCount) {
                size_t next=last+1;
                while(next<boneCount && !selected[next])++next;
                if(next==boneCount || (bonesBase+next*48)/4096 > (bonesBase+(last+1)*48-1)/4096)break;
                last=next;
            }
            const size_t size = (last - first + 1) * sizeof(ModelShapes::Pose);
            ranges.push_back({int(first), int(last), bytes, size, 0});
            bytes += size;
            first = last + 1;
        }
        data.resize(bytes);
        return !ranges.empty();
    }
    const Skeleton::PoseRange* CompleteRange(int index) const {
        for (const auto& range : ranges)
            if (range.prepared && index >= range.first && index <= range.last && range.bytes == range.size &&
                range.offset <= data.size() && range.size <= data.size() - range.offset)
                return &range;
        return nullptr;
    }
    bool Read(int index, ModelShapes::Pose& pose) const {
        const auto* range = CompleteRange(index);
        if (!range) return false;
        std::memcpy(&pose, data.data() + range->offset + size_t(index-range->first)*48, sizeof(pose));
        return true;
    }
    Skeleton::PoseRange TranslationView(int index) const {
        const auto* range = CompleteRange(index);
        if (!range) return {index,index,0,12,0,false};
        return {index,index,range->offset + size_t(index-range->first)*48 + 32,12,12,true};
    }
};
}
