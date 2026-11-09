#pragma once
#ifndef NOMINMAX
#define NOMINMAX
#endif
#define _SILENCE_CXX17_ITERATOR_BASE_CLASS_DEPRECATION_WARNING
#include <Windows.h>
#ifdef min
#undef min
#endif
#ifdef max
#undef max
#endif
#include <cstdio>
#include <string>
#include <rapidjson/document.h>
#include "config/UiPreferences.hpp"
#include "Utils/Config.hpp"
#include "Renderer/Overlay.hpp"
namespace V2::Launcher {
inline bool enabled=false;
inline std::string updatePhase,updateVersion,updateError;
inline unsigned long long received=0,total=0;
inline void Send(const char* message){if(enabled){std::printf("UNIPC %s\n",message);std::fflush(stdout);}}
inline void RequestDownload(){Send("{\"event\":\"download\"}");}
inline void RequestCheck(){Send("{\"event\":\"check\"}");}
inline void RequestInstall(){Send("{\"event\":\"install\"}");}
inline void Poll(){
 if(!enabled)return;
 static std::string buffer;
 HANDLE input=GetStdHandle(STD_INPUT_HANDLE);DWORD available=0;
 if(PeekNamedPipe(input,nullptr,0,nullptr,&available,nullptr)&&available){
  char bytes[4096];DWORD count=0;
  if(ReadFile(input,bytes,(std::min)(DWORD(sizeof(bytes)),available),&count,nullptr))buffer.append(bytes,count);
 }
 if(buffer.size()>65536){buffer.clear();return;}
 size_t end;
 while((end=buffer.find('\n'))!=std::string::npos){
  const auto line=buffer.substr(0,end);buffer.erase(0,end+1);
  rapidjson::Document d;d.Parse(line.c_str());if(!d.IsObject()||!d.HasMember("command")||!d["command"].IsString())continue;
  const std::string command=d["command"].GetString();
  if(command=="save_exit"){
   const bool saved=OW::Config::SaveAllConfig(OW::Config::ConfigPath())&&UI::Preferences::Save();
   Send(saved?"{\"event\":\"saved\"}":"{\"event\":\"save_failed\"}");
   if(saved)g_Overlay.RequestExit();
  }else if(command=="update"&&d.HasMember("state")&&d["state"].IsObject()){
   const auto& s=d["state"];
   auto text=[&](const char* key){return s.HasMember(key)&&s[key].IsString()?std::string(s[key].GetString()):std::string();};
   updatePhase=text("phase");updateVersion=text("version");updateError=text("error");
   received=s.HasMember("received")&&s["received"].IsUint64()?s["received"].GetUint64():0;
   total=s.HasMember("total")&&s["total"].IsUint64()?s["total"].GetUint64():0;
  }
 }
 static int language=-1;static bool glass=true;
 if(language!=UI::Preferences::values.language||glass!=UI::Preferences::values.loginGlass){
  language=UI::Preferences::values.language;glass=UI::Preferences::values.loginGlass;
  char message[128];std::snprintf(message,sizeof(message),"{\"event\":\"preferences\",\"language\":%d,\"loginGlass\":%s}",language,glass?"true":"false");Send(message);
 }
}
}
