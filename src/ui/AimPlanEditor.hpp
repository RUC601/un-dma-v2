#pragma once
#include "ui/EditorSupport.hpp"
#include "ui/ResponseCurveEditor.hpp"
#include <imgui.h>
#include <cstdio>

namespace UI::AimPlanEditor {
using namespace OW;
inline bool Float(const char* label,const char* id,float& v,float lo,float hi,const char* unit="") {
    SettingRow(label); PushControlWidth();
    const bool changed=UISlider(id,&v,lo,hi,Number(2,unit));
    ImGui::PopItemWidth(); return changed;
}
inline bool Double(const char* label,double& v,float lo,float hi) {
    float f=float(v); ImGui::PushID(label);
    const bool changed=Float(label,"##value",f,lo,hi);
    if(changed) v=f; ImGui::PopID(); return changed;
}
inline bool Heroes(const char* id,std::vector<uint64_t>& heroes,const AimPlan::Plan* plan=nullptr,uint64_t ownGroup=0) {
    return UIHeroMultiSelect(id, heroes, plan, ownGroup);
}
inline bool Shuwu(koi::ShuwuConfig& s) {
    bool changed=false;
    ImGui::TextWrapped("%s",L("Shuwu uses 16 virtual units per degree. These are not screen pixels or device counts."));
    changed|=Double("Shuwu KP",s.kp,0,2); changed|=Double("Shuwu KD",s.kd,0,1);
    changed|=Double("Yaw prediction",s.predict_x,-2,3); changed|=Double("Pitch prediction",s.predict_y,-2,3);
    changed|=Double("Gain response",s.rate,.01f,1); changed|=Double("Nonlinear smoothing",s.smooth,0,1);
    changed|=Double("Virtual deadzone",s.deadzone,0,10);
    changed|=Double("Virtual jump reset",s.reset_jump,5,160);
    changed|=Double("Reference step limit",s.max_move_per_frame,1,100);
    changed|=UICheckbox(L("Axis suppression"),&s.suppression_enabled);
    if(s.suppression_enabled) {
        changed|=Double("Yaw scale",s.suppression_x,0,3); changed|=Double("Pitch scale",s.suppression_y,0,3);
    }
    if(ImGui::TreeNode(L("Shuwu filter and compensation"))) {
        changed|=Double("Compensation threshold",s.integral_threshold,1,320);
        changed|=Double("Compensation response",s.integral_rate,.001f,1);
        changed|=Double("KP threshold",s.kp_threshold,1,10000); changed|=Double("Gain decay",s.gain_decay,.001f,1);
        changed|=Double("Velocity Q",s.velocity_q,.0001f,10); changed|=Double("Velocity R",s.velocity_r,.0001f,10);
        changed|=Double("Compensation Q",s.integral_q,.0001f,10); changed|=Double("Compensation R",s.integral_r,.0001f,10);
        changed|=Double("Micro error",s.micro_error,0,10); changed|=Double("Micro difference",s.micro_diff,0,5);
        changed|=Double("Compensation deadband",s.ki_deadband,0,10);
        changed|=Double("Nonlinear A",s.soft_a,0,2); changed|=Double("Nonlinear B",s.soft_b,0,2);
        changed|=Double("Compensation scale",s.ki_scale,10,100000);
        ImGui::TreePop();
    }
    return changed;
}
inline bool Motion(AimPlan::Motion& m,bool chooseMethod=true) {
    bool changed=false;
    if(chooseMethod) { SettingRow("Motion method"); PushControlWidth();
    changed|=UISelect("##motionMethod",&m.method,kAimMethod,IM_ARRAYSIZE(kAimMethod)); ImGui::PopItemWidth(); }
    changed|=Float("Method speed scale","##methodScale",m.angularSpeedScale,0,200," %");
    if(m.method==1||m.method==6) {
        changed|=Float("PID P","##p",m.pidP,0,2);
        changed|=Float("PID I","##i",m.pidI,0,.5f);
        changed|=Float("PID D","##d",m.pidD,0,1);
        changed|=Float("Integral limit","##integral",m.pidMaxIntegral,1,50);
        if(m.method==1) changed|=Float("PID deadzone","##pidDeadzone",m.pidDeadzone,0,10," deg");
    }
    if(m.method==2) {
        SettingRow("Bezier points"); PushControlWidth();
        changed|=UISlider("##points",&m.bezierControlPoints,2,6,Number(0)); ImGui::PopItemWidth();
        changed|=Float("Bezier curvature","##curve",m.bezierCurvature,0,1);
        changed|=Float("Bezier speed","##bezierSpeed",m.bezierSpeed,0,200);
    }
    if(m.method==3) {
        changed|=Float("Near angle","##nearDeg",m.piecewiseNearDegrees,0,180," deg");
        changed|=Float("Mid angle","##midDeg",m.piecewiseMidDegrees,m.piecewiseNearDegrees,180," deg");
        changed|=Float("Far angle","##farDeg",m.piecewiseFarDegrees,m.piecewiseMidDegrees,180," deg");
        changed|=Float("Near scale","##nearScale",m.piecewiseNearScale,0,1);
        changed|=Float("Mid scale","##midScale",m.piecewiseMidScale,0,1);
        changed|=Float("Far scale","##farScale",m.piecewiseFarScale,0,1);
    }
    if(m.method==4) changed|=Float("Acceleration","##accel",m.accelLimitedAcceleration,0,20);
    if(m.method==5) changed|=Float("Angular speed","##angularSpeed",m.constantAngularSpeedDeg,0,7200," deg/s");
    if(m.method==6) {
        changed|=Float("Cruise speed","##cruise",m.hybridConstantSpeedDeg,0,7200," deg/s");
        changed|=Float("Max angular speed","##maxSpeed",m.hybridMaxSpeedDeg,0,7200," deg/s");
        changed|=Float("Acceleration","##hybridAccel",m.hybridAccelerationDeg,0,100000," deg/s2");
        changed|=Float("Deceleration","##decel",m.hybridDecelerationDeg,0,100000," deg/s2");
        changed|=Float("Near angle","##hybridNear",m.hybridNearRadiusDeg,.001f,180," deg");
        changed|=Float("Motion gain","##motionGain",m.hybridTargetMotionGain,0,10);
        changed|=Float("Sudden motion boost","##boost",m.hybridSuddenMotionBoost,0,10);
        changed|=Float("Deadzone","##hybridDeadzone",m.hybridDeadzoneDeg,0,180," deg");
    }
    if(m.method==7) changed|=Shuwu(m.shuwu);
    if(m.method==8) changed|=UI::ResponseCurveEditor(m.responseCurve);
    return changed;
}
inline bool Policy(OW::Config::HeroPreset& preset, bool weightedRanking = true) {
    auto& p=preset.aimPlan; auto& s=p.selection; bool changed=false;
    static const char* modes[]={"Any","Prefer","Only"};
    static const char* predicates[]={"Selected heroes","Full health","Visible"};
    SettingRow("Target preference"); PushControlWidth(); changed|=UISelect("##preference",&s.mode,modes,3); ImGui::PopItemWidth();
    if(s.mode) {
        SettingRow("Preference condition"); PushControlWidth(); changed|=UISelect("##predicate",&s.predicate,predicates,3); ImGui::PopItemWidth();
        if(s.predicate==0) changed|=Heroes("##policyHeroes",s.heroes);
        ImGui::TextWrapped("%s",L(s.mode==2?"Only: no match means no target. An empty hero set selects nobody.":
            "Prefer: matching valid targets rank first, otherwise use the rest. An active minimum lock expires first."));
    }
    changed|=Float("Minimum HP","##minHP",s.minHealth,0,2000," HP");
    changed|=Float("Maximum HP (0 = any)","##maxHP",s.maxHealth,0,2000," HP");
    if(weightedRanking && preset.priority==3) {
        changed|=Float("Angle weight","##angleWeight",s.angleWeight,0,100);
        changed|=Float("Distance weight","##distanceWeight",s.distanceWeight,0,100);
        changed|=Float("Health weight","##healthWeight",s.healthWeight,0,100);
        ImGui::TextWrapped("%s",L("Smart compares normalized angle, distance and remaining health fraction. Zero weights use angle."));
    }
    return changed;
}
inline bool Editor(OW::Config::HeroPreset& preset) {
    bool changed=false; auto& p=preset.aimPlan;
    UIGroupBox("Default motion and target hero exceptions");
    ImGui::TextWrapped("%s",L("Slot behavior stays fixed. Hero exceptions change settings, not which heroes are allowed."));
    if(ImGui::TreeNodeEx(L("Default for all target heroes"),ImGuiTreeNodeFlags_DefaultOpen)) {
        p.overrideMotion=true;
        const auto previous=p.motion;
        if(Motion(p.motion,preset.aimBehaviorPresetId<0)) {
            const int chosen=p.motion.method;
            if(chosen!=previous.method) {
                p.motion=previous;preset.aimMethod=previous.method;Config::StoreHeroBehaviorMode(preset);
                auto& cache=Config::EnsureSlotBehaviorConfig(preset,preset.aimBehaviorPresetId);
                auto stored=std::find_if(cache.motions.begin(),cache.motions.end(),[&](const auto& m){return m.method==chosen;});
                if(stored==cache.motions.end()) {
                    auto defaults=Config::CaptureBehaviorMotion(preset.aimBehavior);defaults.method=chosen;
                    cache.motions.push_back(defaults);stored=std::prev(cache.motions.end());
                }
                p.motion=*stored;
            }
            preset.aimMethod=chosen;changed=true;
        }
        ImGui::TreePop();
    }
    int remove=-1;
    for(size_t i=0;i<p.groups.size();++i) {
        auto& g=p.groups[i]; ImGui::PushID(reinterpret_cast<void*>(uintptr_t(g.id)));
        std::string summary=g.name+" ("+std::to_string(g.heroes.size())+")";
        summary+=" / "; summary+=L(g.overrideMotion?"Motion override":"Inherited motion");
        if(g.overrideLimits) {summary+=" / ";summary+=L("Range override");}
        if(g.overrideAimPoint) {summary+=" / ";summary+=L("Aim point override");}
        const std::string groupId="##targetRule-"+std::to_string(g.id);
        if(ImGui::TreeNodeEx(groupId.c_str(),0,"%s",summary.c_str())) {
            char name[128]; std::snprintf(name,sizeof(name),"%s",g.name.c_str());
            ImGui::SetNextItemWidth(-1); if(ImGui::InputText("##name",name,sizeof(name))) {g.name=name;changed=true;}
            changed|=Heroes("##exceptionHeroes",g.heroes,&p,g.id);
            ImGui::TextWrapped("%s",L("Unchecked sections inherit the current default. A hero can belong to one exception."));
            const bool wasLimits=g.overrideLimits;
            changed|=UICheckbox(L("Override range and health"),&g.overrideLimits);
            if(!wasLimits && g.overrideLimits) g.limits={preset.minDistance,preset.maxDistance,preset.fov,p.selection.minHealth,p.selection.maxHealth};
            if(g.overrideLimits) {
                changed|=Float("FOV (deg)","##fov",g.limits.fov,0,180," deg");
                changed|=Float("Min Distance","##minDist",g.limits.minDistance,0,500," m");
                changed|=Float("Max Distance (0 = any)","##maxDist",g.limits.maxDistance,0,500," m");
                changed|=Float("Minimum HP","##minHP",g.limits.minHealth,0,2000," HP");
                changed|=Float("Maximum HP (0 = any)","##maxHP",g.limits.maxHealth,0,2000," HP");
            }
            const bool wasPoint=g.overrideAimPoint;
            changed|=UICheckbox(L("Override aim point and prediction"),&g.overrideAimPoint);
            if(!wasPoint && g.overrideAimPoint) g.point={uint16_t(preset.aimBoneMask),preset.predictionMode};
            if(g.overrideAimPoint) {
                int bone=g.point.boneMask==1?1:g.point.boneMask==2?2:g.point.boneMask==4?0:3;
                static const char* bones[]={"Chest","Head","Neck","Closest"};
                SettingRow("Aim Bone"); PushControlWidth();
                if(UISelect("##bone",&bone,bones,4)) {g.point.boneMask=bone==0?4:bone==1?1:bone==2?2:7;changed=true;} ImGui::PopItemWidth();
                SettingRow("Prediction"); PushControlWidth(); changed|=UISelect("##prediction",&g.point.predictionMode,kPredictionMode,3); ImGui::PopItemWidth();
            }
            const bool wasMotion=g.overrideMotion;
            changed|=UICheckbox(L("Override motion"),&g.overrideMotion);
            if(!wasMotion && g.overrideMotion) {
                g.motion=p.motion;
                if(!p.overrideMotion) {
                    if(const auto* shared=Config::AimMethodPresetForBehavior(preset.aimBehavior)) g.motion=*shared;
                    else g.motion=CaptureMethodPresetFromCurrent(Config::AimBehaviorMethod(preset.aimBehavior), "Hero motion");
                }
            }
            if(g.overrideMotion) changed|=Motion(g.motion);
            if(ImGui::SmallButton(L("Remove hero exception"))) remove=int(i);
            ImGui::TreePop();
        }
        ImGui::PopID();
    }
    if(remove>=0) {p.groups.erase(p.groups.begin()+remove);changed=true;}
    ImGui::BeginDisabled(p.groups.size()>=AimPlan::kMaxGroups);
    if(ImGui::Button(L("Add hero exception"))) {
        AimPlan::Group g; g.id=1;
        while(std::any_of(p.groups.begin(),p.groups.end(),[&](const auto& old){return old.id==g.id;})) ++g.id;
        g.name=L("Hero exception"); g.motion=p.motion;
        g.limits={preset.minDistance,preset.maxDistance,preset.fov,p.selection.minHealth,p.selection.maxHealth};
        g.point={uint16_t(preset.aimBoneMask),preset.predictionMode};
        p.groups.push_back(std::move(g));changed=true;
    }
    ImGui::EndDisabled();
    CloseGroupBox(); if(changed) AimPlan::Normalize(p); return changed;
}
} // namespace UI::AimPlanEditor
