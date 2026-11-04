#pragma once
#include "Game/AimPlan.hpp"
#include <type_traits>
#pragma push_macro("min")
#pragma push_macro("max")
#undef min
#undef max
#include <rapidjson/document.h>
#include <rapidjson/writer.h>
#include <rapidjson/stringbuffer.h>
#pragma pop_macro("max")
#pragma pop_macro("min")
namespace OW::AimPlan::Json {
using Value=rapidjson::Value;
using Allocator=rapidjson::Document::AllocatorType;
template<class T> void Read(const Value& v,const char* key,T& dst) {
    if(!v.IsObject()) return; auto i=v.FindMember(key); if(i==v.MemberEnd()) return;
    const auto& x=i->value;
    if constexpr(std::is_same_v<T,bool>) { if(x.IsBool()) dst=x.GetBool(); }
    else if constexpr(std::is_integral_v<T> && std::is_signed_v<T>) { if(x.IsInt64() && x.GetInt64()>=(std::numeric_limits<T>::min)() && x.GetInt64()<=(std::numeric_limits<T>::max)()) dst=T(x.GetInt64()); }
    else if constexpr(std::is_integral_v<T>) { if(x.IsUint64() && x.GetUint64()<=uint64_t((std::numeric_limits<T>::max)())) dst=T(x.GetUint64()); }
    else if(x.IsNumber() && std::isfinite(x.GetDouble())) dst=T(x.GetDouble());
}
template<class T> void Put(Value& v,const char* key,T x,Allocator& a) {
    Value k; k.SetString(key,a); Value b;
    if constexpr(std::is_same_v<T,bool>) b.SetBool(x);
    else if constexpr(std::is_integral_v<T> && std::is_signed_v<T>) b.SetInt64(int64_t(x));
    else if constexpr(std::is_integral_v<T>) b.SetUint64(uint64_t(x));
    else b.SetDouble(x);
    v.AddMember(k,b,a);
}
inline void Text(Value& v,const char* key,const std::string& text,Allocator& a) {
    Value k,b; k.SetString(key,a); b.SetString(text.c_str(),rapidjson::SizeType(text.size()),a); v.AddMember(k,b,a);
}
inline void ReadIds(const Value& v,const char* key,std::vector<uint64_t>& ids) {
    if(!v.IsObject()||!v.HasMember(key)||!v[key].IsArray()) return;
    ids.clear(); for(const auto& x:v[key].GetArray()) { if(ids.size()>=kMaxHeroes) break; if(x.IsUint64()&&x.GetUint64()) ids.push_back(x.GetUint64()); }
}
inline void PutIds(Value& v,const char* key,const std::vector<uint64_t>& ids,Allocator& a) {
    Value list(rapidjson::kArrayType); for(auto id:ids) list.PushBack(id,a);
    Value k; k.SetString(key,a); v.AddMember(k,list,a);
}
inline Motion ReadMotion(const Value& v,Motion m={}) {
    Read(v,"responseX1",m.responseCurve.x1); Read(v,"responseY1",m.responseCurve.y1);
    Read(v,"responseX2",m.responseCurve.x2); Read(v,"responseY2",m.responseCurve.y2);
    Read(v,"method",m.method);
    Read(v,"angularSpeedScale",m.angularSpeedScale);
    Read(v,"pidP",m.pidP);
    Read(v,"pidI",m.pidI);
    Read(v,"pidD",m.pidD);
    Read(v,"pidMaxIntegral",m.pidMaxIntegral);
    Read(v,"pidDeadzone",m.pidDeadzone);
    Read(v,"bezierControlPoints",m.bezierControlPoints);
    Read(v,"bezierCurvature",m.bezierCurvature);
    Read(v,"bezierSpeed",m.bezierSpeed);
    Read(v,"piecewiseNearDegrees",m.piecewiseNearDegrees);
    Read(v,"piecewiseMidDegrees",m.piecewiseMidDegrees);
    Read(v,"piecewiseFarDegrees",m.piecewiseFarDegrees);
    Read(v,"piecewiseNearScale",m.piecewiseNearScale);
    Read(v,"piecewiseMidScale",m.piecewiseMidScale);
    Read(v,"piecewiseFarScale",m.piecewiseFarScale);
    Read(v,"accelLimitedAcceleration",m.accelLimitedAcceleration);
    Read(v,"constantAngularSpeedDeg",m.constantAngularSpeedDeg);
    Read(v,"hybridConstantSpeedDeg",m.hybridConstantSpeedDeg);
    Read(v,"hybridMaxSpeedDeg",m.hybridMaxSpeedDeg);
    Read(v,"hybridAccelerationDeg",m.hybridAccelerationDeg);
    Read(v,"hybridDecelerationDeg",m.hybridDecelerationDeg);
    Read(v,"hybridNearRadiusDeg",m.hybridNearRadiusDeg);
    Read(v,"hybridTargetMotionGain",m.hybridTargetMotionGain);
    Read(v,"hybridSuddenMotionBoost",m.hybridSuddenMotionBoost);
    Read(v,"hybridDeadzoneDeg",m.hybridDeadzoneDeg);
    if(v.IsObject()&&v.HasMember("shuwu")&&v["shuwu"].IsObject()) { const auto& s=v["shuwu"];
        Read(s,"kp",m.shuwu.kp);
        Read(s,"kd",m.shuwu.kd);
        Read(s,"predict_x",m.shuwu.predict_x);
        Read(s,"predict_y",m.shuwu.predict_y);
        Read(s,"rate",m.shuwu.rate);
        Read(s,"smooth",m.shuwu.smooth);
        Read(s,"deadzone",m.shuwu.deadzone);
        Read(s,"reset_jump",m.shuwu.reset_jump);
        Read(s,"integral_threshold",m.shuwu.integral_threshold);
        Read(s,"integral_rate",m.shuwu.integral_rate);
        Read(s,"kp_threshold",m.shuwu.kp_threshold);
        Read(s,"gain_decay",m.shuwu.gain_decay);
        Read(s,"velocity_q",m.shuwu.velocity_q);
        Read(s,"velocity_r",m.shuwu.velocity_r);
        Read(s,"integral_q",m.shuwu.integral_q);
        Read(s,"integral_r",m.shuwu.integral_r);
        Read(s,"micro_error",m.shuwu.micro_error);
        Read(s,"micro_diff",m.shuwu.micro_diff);
        Read(s,"ki_deadband",m.shuwu.ki_deadband);
        Read(s,"soft_a",m.shuwu.soft_a);
        Read(s,"soft_b",m.shuwu.soft_b);
        Read(s,"ki_scale",m.shuwu.ki_scale);
        Read(s,"max_move_per_frame",m.shuwu.max_move_per_frame);
        Read(s,"suppression_enabled",m.shuwu.suppression_enabled);
        Read(s,"suppression_x",m.shuwu.suppression_x);
        Read(s,"suppression_y",m.shuwu.suppression_y);
        Read(s,"soft_lock_enabled",m.shuwu.soft_lock_enabled);
        Read(s,"soft_lock_x",m.shuwu.soft_lock_x);
        Read(s,"soft_lock_y",m.shuwu.soft_lock_y);
        Read(s,"soft_lock_delay_ms",m.shuwu.soft_lock_delay_ms);
    } NormalizeMotion(m); return m;
}
inline Value MotionJson(const Motion& m,Allocator& a) {
    Value v(rapidjson::kObjectType);
    Put(v,"responseX1",m.responseCurve.x1,a); Put(v,"responseY1",m.responseCurve.y1,a);
    Put(v,"responseX2",m.responseCurve.x2,a); Put(v,"responseY2",m.responseCurve.y2,a);
    Put(v,"method",m.method,a);
    Put(v,"angularSpeedScale",m.angularSpeedScale,a);
    Put(v,"pidP",m.pidP,a);
    Put(v,"pidI",m.pidI,a);
    Put(v,"pidD",m.pidD,a);
    Put(v,"pidMaxIntegral",m.pidMaxIntegral,a);
    Put(v,"pidDeadzone",m.pidDeadzone,a);
    Put(v,"bezierControlPoints",m.bezierControlPoints,a);
    Put(v,"bezierCurvature",m.bezierCurvature,a);
    Put(v,"bezierSpeed",m.bezierSpeed,a);
    Put(v,"piecewiseNearDegrees",m.piecewiseNearDegrees,a);
    Put(v,"piecewiseMidDegrees",m.piecewiseMidDegrees,a);
    Put(v,"piecewiseFarDegrees",m.piecewiseFarDegrees,a);
    Put(v,"piecewiseNearScale",m.piecewiseNearScale,a);
    Put(v,"piecewiseMidScale",m.piecewiseMidScale,a);
    Put(v,"piecewiseFarScale",m.piecewiseFarScale,a);
    Put(v,"accelLimitedAcceleration",m.accelLimitedAcceleration,a);
    Put(v,"constantAngularSpeedDeg",m.constantAngularSpeedDeg,a);
    Put(v,"hybridConstantSpeedDeg",m.hybridConstantSpeedDeg,a);
    Put(v,"hybridMaxSpeedDeg",m.hybridMaxSpeedDeg,a);
    Put(v,"hybridAccelerationDeg",m.hybridAccelerationDeg,a);
    Put(v,"hybridDecelerationDeg",m.hybridDecelerationDeg,a);
    Put(v,"hybridNearRadiusDeg",m.hybridNearRadiusDeg,a);
    Put(v,"hybridTargetMotionGain",m.hybridTargetMotionGain,a);
    Put(v,"hybridSuddenMotionBoost",m.hybridSuddenMotionBoost,a);
    Put(v,"hybridDeadzoneDeg",m.hybridDeadzoneDeg,a);
    Value s(rapidjson::kObjectType);
    Put(s,"kp",m.shuwu.kp,a);
    Put(s,"kd",m.shuwu.kd,a);
    Put(s,"predict_x",m.shuwu.predict_x,a);
    Put(s,"predict_y",m.shuwu.predict_y,a);
    Put(s,"rate",m.shuwu.rate,a);
    Put(s,"smooth",m.shuwu.smooth,a);
    Put(s,"deadzone",m.shuwu.deadzone,a);
    Put(s,"reset_jump",m.shuwu.reset_jump,a);
    Put(s,"integral_threshold",m.shuwu.integral_threshold,a);
    Put(s,"integral_rate",m.shuwu.integral_rate,a);
    Put(s,"kp_threshold",m.shuwu.kp_threshold,a);
    Put(s,"gain_decay",m.shuwu.gain_decay,a);
    Put(s,"velocity_q",m.shuwu.velocity_q,a);
    Put(s,"velocity_r",m.shuwu.velocity_r,a);
    Put(s,"integral_q",m.shuwu.integral_q,a);
    Put(s,"integral_r",m.shuwu.integral_r,a);
    Put(s,"micro_error",m.shuwu.micro_error,a);
    Put(s,"micro_diff",m.shuwu.micro_diff,a);
    Put(s,"ki_deadband",m.shuwu.ki_deadband,a);
    Put(s,"soft_a",m.shuwu.soft_a,a);
    Put(s,"soft_b",m.shuwu.soft_b,a);
    Put(s,"ki_scale",m.shuwu.ki_scale,a);
    Put(s,"max_move_per_frame",m.shuwu.max_move_per_frame,a);
    Put(s,"suppression_enabled",m.shuwu.suppression_enabled,a);
    Put(s,"suppression_x",m.shuwu.suppression_x,a);
    Put(s,"suppression_y",m.shuwu.suppression_y,a);
    Put(s,"soft_lock_enabled",m.shuwu.soft_lock_enabled,a);
    Put(s,"soft_lock_x",m.shuwu.soft_lock_x,a);
    Put(s,"soft_lock_y",m.shuwu.soft_lock_y,a);
    Put(s,"soft_lock_delay_ms",m.shuwu.soft_lock_delay_ms,a);
    v.AddMember("shuwu",s,a); return v;
}
inline Plan ReadPlan(const Value& v) {
    Plan p; if(!v.IsObject()) return p;
    auto& s=p.selection;
    Read(v,"mode",s.mode); Read(v,"predicate",s.predicate); ReadIds(v,"heroes",s.heroes);
    Read(v,"angleWeight",s.angleWeight); Read(v,"distanceWeight",s.distanceWeight); Read(v,"healthWeight",s.healthWeight);
    Read(v,"minHealth",s.minHealth); Read(v,"maxHealth",s.maxHealth);
    Read(v,"overrideMotion",p.overrideMotion); if(v.HasMember("motion")) p.motion=ReadMotion(v["motion"]);
    if(v.HasMember("groups")&&v["groups"].IsArray()) for(const auto& x:v["groups"].GetArray()) {
        if(p.groups.size()>=kMaxGroups) break; if(!x.IsObject()) continue;
        Group g; Read(x,"id",g.id); if(x.HasMember("name")&&x["name"].IsString()) g.name=x["name"].GetString();
        ReadIds(x,"heroes",g.heroes);
        Read(x,"overrideLimits",g.overrideLimits); Read(x,"overrideAimPoint",g.overrideAimPoint); Read(x,"overrideMotion",g.overrideMotion);
        Read(x,"minDistance",g.limits.minDistance); Read(x,"maxDistance",g.limits.maxDistance); Read(x,"fov",g.limits.fov);
        Read(x,"minHealth",g.limits.minHealth); Read(x,"maxHealth",g.limits.maxHealth);
        Read(x,"boneMask",g.point.boneMask); Read(x,"predictionMode",g.point.predictionMode);
        if(x.HasMember("motion")) g.motion=ReadMotion(x["motion"]);
        p.groups.push_back(std::move(g));
    }
    Normalize(p); return p;
}
inline Value PlanJson(const Plan& input,Allocator& a) {
    Plan p=input; Normalize(p); Value v(rapidjson::kObjectType); const auto& s=p.selection;
    Put(v,"version",1,a); Put(v,"mode",s.mode,a); Put(v,"predicate",s.predicate,a); PutIds(v,"heroes",s.heroes,a);
    Put(v,"angleWeight",s.angleWeight,a); Put(v,"distanceWeight",s.distanceWeight,a); Put(v,"healthWeight",s.healthWeight,a);
    Put(v,"minHealth",s.minHealth,a); Put(v,"maxHealth",s.maxHealth,a); Put(v,"overrideMotion",p.overrideMotion,a);
    v.AddMember("motion",MotionJson(p.motion,a),a);
    Value groups(rapidjson::kArrayType); for(const auto& g:p.groups) {
        Value x(rapidjson::kObjectType); Put(x,"id",g.id,a); Text(x,"name",g.name,a); PutIds(x,"heroes",g.heroes,a);
        Put(x,"overrideLimits",g.overrideLimits,a); Put(x,"overrideAimPoint",g.overrideAimPoint,a); Put(x,"overrideMotion",g.overrideMotion,a);
        Put(x,"minDistance",g.limits.minDistance,a); Put(x,"maxDistance",g.limits.maxDistance,a); Put(x,"fov",g.limits.fov,a);
        Put(x,"minHealth",g.limits.minHealth,a); Put(x,"maxHealth",g.limits.maxHealth,a);
        Put(x,"boneMask",g.point.boneMask,a); Put(x,"predictionMode",g.point.predictionMode,a);
        x.AddMember("motion",MotionJson(g.motion,a),a); groups.PushBack(x,a);
    }
    v.AddMember("groups",groups,a); return v;
}
inline std::string Encode(const Plan& p) {
    rapidjson::Document d; auto v=PlanJson(p,d.GetAllocator()); rapidjson::StringBuffer b;
    rapidjson::Writer<rapidjson::StringBuffer> w(b); v.Accept(w); return {b.GetString(),b.GetSize()};
}
inline Plan Decode(const std::string& s) {
    if(s.size()>262144) return {}; rapidjson::Document d; d.Parse(s.c_str()); return d.HasParseError()?Plan{}:ReadPlan(d);
}
} // namespace OW::AimPlan::Json
