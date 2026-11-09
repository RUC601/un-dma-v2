#pragma once
#include <filesystem>
#include <fstream>
#include <vector>
#include <string>
#include <cstring>
#include <cstdint>
namespace UI::OverlayFonts {
inline std::string error;
inline std::vector<unsigned char> Read(const std::string& path) {
    std::ifstream file(std::filesystem::u8path(path),std::ios::binary|std::ios::ate);
    if(!file)return {};
    const auto length=file.tellg();
    if(length<12||length>32*1024*1024)return {};
    std::vector<unsigned char> bytes(static_cast<size_t>(length));file.seekg(0);
    if(!file.read(reinterpret_cast<char*>(bytes.data()),bytes.size()))return {};
    auto u16=[&](size_t i){return (unsigned(bytes[i])<<8)|unsigned(bytes[i+1]);};
    auto u32=[&](size_t i){return (uint32_t(bytes[i])<<24)|(uint32_t(bytes[i+1])<<16)|(uint32_t(bytes[i+2])<<8)|bytes[i+3];};
    size_t face=0;
    if(std::memcmp(bytes.data(),"ttcf",4)==0){
        if(bytes.size()<16||u32(8)==0||u32(8)>1024||12ull+4ull*u32(8)>bytes.size())return {};
        face=u32(12);
    }
    if(face>bytes.size()-12)return {};
    const auto signature=u32(face);
    if(signature!=0x00010000&&signature!=0x4F54544F&&signature!=0x74727565)return {};
    const unsigned count=u16(face+4);
    if(!count||count>256||face+12ull+16ull*count>bytes.size())return {};
    bool cmap=false,head=false,hhea=false,hmtx=false,maxp=false,outlines=false;
    for(unsigned i=0;i<count;++i){const size_t at=face+12+i*16;
        const auto offset=u32(at+8),size=u32(at+12);
        if(offset>bytes.size()||size>bytes.size()-offset)return {};
        const auto tag=u32(at);
        cmap|=tag==0x636D6170&&size>=4;head|=tag==0x68656164&&size>=54;
        hhea|=tag==0x68686561&&size>=36;hmtx|=tag==0x686D7478&&size>=4;maxp|=tag==0x6D617870&&size>=6;
        outlines|=(tag==0x676C7966||tag==0x43464620)&&size>0;
    }
    if(!cmap||!head||!hhea||!hmtx||!maxp||!outlines)return {};
    return bytes;
}
}
