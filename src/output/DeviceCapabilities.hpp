#pragma once
#include "output/OutputRuntime.hpp"
namespace V2::Output {
struct Capabilities {
 const char* name;
 bool move=false, buttons=false, keyboard=false, physicalMonitor=false, release=false;
 int maxDelta=0, verifiedRateHz=0;
};
inline Capabilities Describe(const OutputRuntimeDescriptor& d) {
 switch(d.backend) {
 case OutputRuntimeBackend::Network:return {"KMBox Net",true,true,true,true,true,32767,0};
 case OutputRuntimeBackend::Serial: {
  const auto* s=std::get_if<OutputSerialEndpoint>(&d.endpoint);
  const char* name=!s || s->protocol==SerialProtocol::KmBox ? "KMBox Serial" :
    s->protocol==SerialProtocol::Makcu ? "MAKCU / 实机待验证" : "Ferrum Software API / 实机待验证";
  return {name,true,true,SupportsKeyboardOutput(d),s && s->protocol!=SerialProtocol::KmBox,true,32767,0};
 }
 case OutputRuntimeBackend::CatBox:return {"CatBox / 社区协议 / 实机待验证",true,true,true,true,true,32767,0};
 case OutputRuntimeBackend::Mock:return {"Mock / 离线",true,true,true,false,true,32767,0};
 default:return {"关闭"};
 }
}
}
