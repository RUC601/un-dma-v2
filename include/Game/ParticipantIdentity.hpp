#pragma once
#include <array>
#include <cstdint>
#include <cstring>
#include <string>
#include "Game/EntityRosterPolicy.hpp"

namespace OW {
// The engine string uses a 16-byte inline buffer or a heap pointer, not char* in both cases.
template<class Read>
bool ReadParticipantLabel(uint64_t component, Read&& read, std::string& label) {
    uint64_t object=0, checkObject=0;
    std::array<uint8_t,24> header{}, check{};
    if(!component || !read(component+0xE0,&object,sizeof(object)) || !object ||
       !read(object,header.data(),header.size()))return false;
    uint32_t length=0,capacity=0;
    std::memcpy(&length,header.data()+0x10,4);
    std::memcpy(&capacity,header.data()+0x14,4);
    if(length<2 || length>63 || length>capacity || capacity>65536)return false;
    std::string candidate(length,'\0');
    if(capacity<=16) {
        if(length>16)return false;
        std::memcpy(candidate.data(),header.data(),length);
    } else {
        uint64_t text=0;std::memcpy(&text,header.data(),8);
        if(!text || !read(text,candidate.data(),length))return false;
    }
    if(!candidate.empty() && candidate.back()=='\0')candidate.pop_back();
    if(!EntityRosterPolicy::ReliableParticipantId(candidate) ||
       !read(object,check.data(),check.size()) || check!=header ||
       !read(component+0xE0,&checkObject,sizeof(checkObject)) || checkObject!=object)return false;
    label=std::move(candidate);
    return true;
}
}
