#pragma once
#include "config/AimPlanJson.hpp"
#include "Game/AimBehaviorMode.hpp"
namespace OW::Config::ModeJson {
using namespace OW::AimPlan::Json;
inline Value TuningJson(const AimBehaviorTuning& t,Allocator& a) {
    Value v(rapidjson::kObjectType);
    Put(v,"smooth",t.smooth,a);
    Put(v,"autoshot",t.autoshot,a);
    Put(v,"keepFiring",t.keepFiring,a);
    Put(v,"firePolicy",t.firePolicy,a);
    Put(v,"pitchScale",t.pitchScale,a);
    Put(v,"maxAimTime",t.maxAimTime,a);
    Put(v,"minCharge",t.minCharge,a);
    Put(v,"maxCharge",t.maxCharge,a);
    Put(v,"trackingDeadzone",t.trackingDeadzone,a);
    Put(v,"flickShotClampMs",t.flickShotClampMs,a);
    Put(v,"flickPostFireDelayMs",t.flickPostFireDelayMs,a);
    Put(v,"magneticShotIntervalMs",t.magneticShotIntervalMs,a);
    Put(v,"magneticPostFirePauseMs",t.magneticPostFirePauseMs,a);
    Put(v,"magneticRecoveryMs",t.magneticRecoveryMs,a);
    Put(v,"magneticPostFireYawScale",t.magneticPostFireYawScale,a);
    Put(v,"magneticPostFirePitchScale",t.magneticPostFirePitchScale,a);
    Put(v,"magneticPreFireBoostWindowMs",t.magneticPreFireBoostWindowMs,a);
    Put(v,"magneticPreFireBoostScale",t.magneticPreFireBoostScale,a);
    Put(v,"flickTrajectoryWait",t.flickTrajectoryWait,a);
    Put(v,"flickTrajectoryWaitMs",t.flickTrajectoryWaitMs,a);
    Put(v,"flickTrajectoryApexWindowMs",t.flickTrajectoryApexWindowMs,a);
    Put(v,"flick2ndTriggerGate",t.flick2ndTriggerGate,a);
    Put(v,"flick2ndBoxPadding",t.flick2ndBoxPadding,a);
    Put(v,"flick2ndInnerRadius",t.flick2ndInnerRadius,a);
    Put(v,"flick2ndInnerSmoothScale",t.flick2ndInnerSmoothScale,a);
    Put(v,"flick2ndInnerMethod",t.flick2ndInnerMethod,a);
    Put(v,"baseSpeed",t.baseSpeed,a);
    Put(v,"moveSplitEnabled",t.moveSplitEnabled,a);
    Put(v,"moveSplitMaxPixels",t.moveSplitMaxPixels,a);
    Put(v,"moveSplitDelayUs",t.moveSplitDelayUs,a);
    Put(v,"releaseToFire",t.releaseToFire,a);
    Put(v,"autoCharge",t.autoCharge,a);
    Put(v,"chargeFallbackMs",t.chargeFallbackMs,a);
    Put(v,"releaseExecution",t.releaseExecution,a);
    Put(v,"releaseAimTimeoutMs",t.releaseAimTimeoutMs,a);
    Put(v,"startEnabled",t.startLimiter.enabled,a);
    Put(v,"startCap",t.startLimiter.initialCapDegPerSec,a);
    Put(v,"startRise",t.startLimiter.capRiseDegPerSec2,a);
    Put(v,"startRetarget",t.startLimiter.restartOnTargetChange,a);
    return v;
}
inline AimBehaviorTuning ReadTuning(const Value& v,AimBehaviorTuning t={}) {
    Read(v,"smooth",t.smooth);
    Read(v,"autoshot",t.autoshot);
    Read(v,"keepFiring",t.keepFiring);
    Read(v,"firePolicy",t.firePolicy);
    Read(v,"pitchScale",t.pitchScale);
    Read(v,"maxAimTime",t.maxAimTime);
    Read(v,"minCharge",t.minCharge);
    Read(v,"maxCharge",t.maxCharge);
    Read(v,"trackingDeadzone",t.trackingDeadzone);
    Read(v,"flickShotClampMs",t.flickShotClampMs);
    Read(v,"flickPostFireDelayMs",t.flickPostFireDelayMs);
    Read(v,"magneticShotIntervalMs",t.magneticShotIntervalMs);
    Read(v,"magneticPostFirePauseMs",t.magneticPostFirePauseMs);
    Read(v,"magneticRecoveryMs",t.magneticRecoveryMs);
    Read(v,"magneticPostFireYawScale",t.magneticPostFireYawScale);
    Read(v,"magneticPostFirePitchScale",t.magneticPostFirePitchScale);
    Read(v,"magneticPreFireBoostWindowMs",t.magneticPreFireBoostWindowMs);
    Read(v,"magneticPreFireBoostScale",t.magneticPreFireBoostScale);
    Read(v,"flickTrajectoryWait",t.flickTrajectoryWait);
    Read(v,"flickTrajectoryWaitMs",t.flickTrajectoryWaitMs);
    Read(v,"flickTrajectoryApexWindowMs",t.flickTrajectoryApexWindowMs);
    Read(v,"flick2ndTriggerGate",t.flick2ndTriggerGate);
    Read(v,"flick2ndBoxPadding",t.flick2ndBoxPadding);
    Read(v,"flick2ndInnerRadius",t.flick2ndInnerRadius);
    Read(v,"flick2ndInnerSmoothScale",t.flick2ndInnerSmoothScale);
    Read(v,"flick2ndInnerMethod",t.flick2ndInnerMethod);
    Read(v,"baseSpeed",t.baseSpeed);
    Read(v,"moveSplitEnabled",t.moveSplitEnabled);
    Read(v,"moveSplitMaxPixels",t.moveSplitMaxPixels);
    Read(v,"moveSplitDelayUs",t.moveSplitDelayUs);
    Read(v,"releaseToFire",t.releaseToFire);
    Read(v,"autoCharge",t.autoCharge);
    Read(v,"chargeFallbackMs",t.chargeFallbackMs);
    Read(v,"releaseExecution",t.releaseExecution);
    Read(v,"releaseAimTimeoutMs",t.releaseAimTimeoutMs);
    Read(v,"startEnabled",t.startLimiter.enabled);
    Read(v,"startCap",t.startLimiter.initialCapDegPerSec);
    Read(v,"startRise",t.startLimiter.capRiseDegPerSec2);
    Read(v,"startRetarget",t.startLimiter.restartOnTargetChange);
    NormalizeBehaviorTuning(t); return t;
}
inline std::string Encode(const Value& v) {
    rapidjson::StringBuffer b; rapidjson::Writer<rapidjson::StringBuffer> w(b); v.Accept(w); return {b.GetString(),b.GetSize()};
}
inline Value ConfigsJson(const std::vector<SlotBehaviorConfig>& configs,Allocator& a) {
    Value result(rapidjson::kArrayType);
    for(const auto& c:configs) {
        Value v(rapidjson::kObjectType), behaviors(rapidjson::kArrayType), motions(rapidjson::kArrayType);
        Put(v,"presetId",c.presetId,a);
        for(const auto& b:c.behaviors) {
            Value item(rapidjson::kObjectType); Put(item,"behavior",b.behavior,a);
            item.AddMember("tuning",TuningJson(b.tuning,a),a); behaviors.PushBack(item,a);
        }
        for(const auto& m:c.motions) motions.PushBack(MotionJson(m,a),a);
        v.AddMember("behaviors",behaviors,a); v.AddMember("motions",motions,a); result.PushBack(v,a);
    } return result;
}
inline std::vector<SlotBehaviorConfig> ReadConfigs(const Value& value) {
    std::vector<SlotBehaviorConfig> result;
    if(!value.IsArray()) return result;
    for(const auto& v:value.GetArray()) {
        // Protected defaults and retained legacy caches also occupy entries.
        // ResolveHeroBehaviorMode removes unknown mode IDs after loading.
        SlotBehaviorConfig c; Read(v,"presetId",c.presetId);
        if(c.presetId< -1 || std::any_of(result.begin(),result.end(),[&](const auto& old){return old.presetId==c.presetId;})) continue;
        if(v.IsObject()&&v.HasMember("behaviors")&&v["behaviors"].IsArray()) for(const auto& b:v["behaviors"].GetArray()) {
            int type=-1; Read(b,"behavior",type);
            if(type<0||type>4||c.behaviors.size()>=5||!b.IsObject()||!b.HasMember("tuning")||
               std::any_of(c.behaviors.begin(),c.behaviors.end(),[&](const auto& old){return old.behavior==type;})) continue;
            c.behaviors.push_back({type,ReadTuning(b["tuning"])});
        }
        if(v.IsObject()&&v.HasMember("motions")&&v["motions"].IsArray()) for(const auto& m:v["motions"].GetArray()) {
            const auto motion=ReadMotion(m);
            if(c.motions.size()<9&&!std::any_of(c.motions.begin(),c.motions.end(),[&](const auto& old){return old.method==motion.method;})) c.motions.push_back(motion);
        }
        result.push_back(std::move(c));
    } return result;
}
inline Value StateJson(const AimModeRuntime& state,Allocator& a) {
    Value v(rapidjson::kObjectType); Put(v,"enabled",state.enabled,a);
    v.AddMember("tuning",TuningJson(state.tuning,a),a);
    v.AddMember("motion",MotionJson(state.motion,a),a);
    v.AddMember("configs",ConfigsJson(state.configs,a),a);return v;
}
inline AimModeRuntime ReadState(const Value& v) {
    AimModeRuntime s; Read(v,"enabled",s.enabled);
    if(!v.IsObject()) return s;
    if(v.HasMember("tuning")) s.tuning=ReadTuning(v["tuning"]);
    if(v.HasMember("motion")) s.motion=ReadMotion(v["motion"]);
    if(v.HasMember("configs")) s.configs=ReadConfigs(v["configs"]);return s;
}

}
