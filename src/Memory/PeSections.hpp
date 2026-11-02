#pragma once
#include <array>
#include <cstdint>
#include <cstring>
#include <vector>

namespace V2::Memory {
struct SectionRange { uint64_t begin, end; };
// 只解析实际映像中已声明的可写非执行节；短读/越界不产生扫描范围。
template<class Read>
std::vector<SectionRange> WritableSections(uint64_t module,uint32_t imageSize,Read&& read) {
    std::array<uint8_t,4096> bytes{};
    if(!read(module,bytes.data(),bytes.size()))return {};
    auto u16=[&](size_t at){uint16_t v;std::memcpy(&v,bytes.data()+at,2);return v;};
    auto u32=[&](size_t at){uint32_t v;std::memcpy(&v,bytes.data()+at,4);return v;};
    const size_t nt=u32(0x3c);
    if(u16(0)!=0x5a4d || nt>bytes.size()-24 || u32(nt)!=0x4550)return {};
    const auto count=u16(nt+6), optional=u16(nt+20);
    const size_t first=nt+24+optional;
    if(count>64 || first>bytes.size() || size_t(count)*40>bytes.size()-first)return {};
    std::vector<SectionRange> result;
    for(size_t i=0;i<count;++i) {
        const auto at=first+40*i;const uint64_t size=u32(at+8),rva=u32(at+12);
        const auto flags=u32(at+36);
        if((flags&0x80000000) && !(flags&0x20000000) && size>=32 && rva<imageSize && size<=imageSize-rva)
            result.push_back({rva,rva+size});
    }
    return result;
}
}
