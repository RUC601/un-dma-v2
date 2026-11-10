#pragma once
#include "ui/AimPlanEditor.hpp"
#include "output/OutputRuntime.hpp"
#include "output/DeviceCapabilities.hpp"

namespace UI {
inline bool BehaviorTuningEditor(OW::Config::AimBehaviorTuning& t,int behavior) {
    using namespace AimPlanEditor;
    bool changed=false;
    changed|=Float("Speed Scale","##smooth",t.smooth,0,100," %");
    changed|=Float("Base Speed Scale","##base",t.baseSpeed,0,100," %");
    changed|=Float("Pitch Scale","##pitch",t.pitchScale,.1f,3);
    SettingRow("Fire Policy");PushControlWidth();
    if(UISelect("##policy",&t.firePolicy,kFirePolicy,IM_ARRAYSIZE(kFirePolicy))) {
        t.keepFiring=t.firePolicy==1;t.autoshot=t.firePolicy>=2;changed=true;
    }
    ImGui::PopItemWidth();
    if(behavior==1||behavior==2||behavior==4) {
        SettingRow("Release to fire");changed|=UICheckbox("##releaseToFire",&t.releaseToFire);
        if(t.releaseToFire) {
            const char* sources[]={"Hold manually","Charge automatically"};int source=t.autoCharge?1:0;
            SettingRow("Charge control");PushControlWidth();
            if(UISelect("##chargeControl",&source,sources,2)) {t.autoCharge=source==1;changed=true;}
            ImGui::PopItemWidth();
            if(t.autoCharge) {
                const char* timings[]={"While holding the auxiliary key","After releasing the auxiliary key"};
                SettingRow("Aim and fire timing");PushControlWidth();
                changed|=UISelect("##releaseExecution",&t.releaseExecution,timings,2);
                ImGui::PopItemWidth();
                if(t.releaseExecution==1) {
                    changed|=Float("Release aim limit","##releaseAimLimit",t.releaseAimTimeoutMs,50,5000," ms");
                    ImGui::TextWrapped("%s",L("Hold to charge anywhere. Release to aim and fire once. With no target, or when the aim limit expires, fire at the current crosshair."));
                }
            }
            changed|=Float("Minimum fire charge","##chargeMin",t.minCharge,0,100," %");
            changed|=Float("Charge fallback wait","##chargeFallback",t.chargeFallbackMs,0,10000," ms");
            ImGui::TextWrapped("%s",L(t.autoCharge
                ?"Automatic charging needs a separate aim activation key. Stopping releases the owned button."
                :"Hold the fire button. Release and press it again after each assisted shot."));
            if(!t.autoCharge && (!V2::Output::SupportsMouseMask(V2::Output::ActiveRuntimeSnapshot().descriptor)||!V2::Output::ActivePhysicalInput().available))
                ImGui::TextWrapped("%s",L("Manual release is unavailable: this output device needs physical-button monitoring and masking."));
        }
        changed|=Float("Shot Clamp","##shotClamp",t.flickShotClampMs,0,1000," ms");
        changed|=Float("Post-fire Delay","##postFire",t.flickPostFireDelayMs,0,500," ms");
        SettingRow("Trajectory Wait");changed|=UICheckbox("##trajectory",&t.flickTrajectoryWait);
        if(t.flickTrajectoryWait) {
            changed|=Float("Wait Limit","##wait",t.flickTrajectoryWaitMs,0,3000," ms");
            changed|=Float("Apex Window","##apex",t.flickTrajectoryApexWindowMs,0,1000," ms");
        }
    }
    if(behavior==0||behavior==3) {
        changed|=Float("Deadzone","##deadzone",t.trackingDeadzone,0,250," px");
        SettingRow("Start Speed Limit");changed|=UICheckbox("##startLimit",&t.startLimiter.enabled);
        if(t.startLimiter.enabled) {
            changed|=Float("Initial Speed Cap","##startCap",t.startLimiter.initialCapDegPerSec,0,3600," deg/s");
            changed|=Float("Cap Rise","##capRise",t.startLimiter.capRiseDegPerSec2,0,100000," deg/s2");
            SettingRow("Restart On Target Change");changed|=UICheckbox("##restart",&t.startLimiter.restartOnTargetChange);
        }
    }
    if(behavior==2) {
        const char* methods[]={"Inherit","Linear","PID","Bezier","Piecewise","Accel Limited","Constant","Hybrid PID","Shuwu","Error response curve"};
        int index=t.flick2ndInnerMethod+1;SettingRow("Second Method");PushControlWidth();
        if(UISelect("##secondMethod",&index,methods,IM_ARRAYSIZE(methods))) {t.flick2ndInnerMethod=index-1;changed=true;}
        ImGui::PopItemWidth();
        SettingRow("Trigger Gate");changed|=UICheckbox("##gate",&t.flick2ndTriggerGate);
        changed|=Float("Box Padding","##box",t.flick2ndBoxPadding,0,80," px");
        changed|=Float("Inner Radius","##inner",t.flick2ndInnerRadius,0,250," px");
        changed|=Float("Inner Smooth","##innerSmooth",t.flick2ndInnerSmoothScale,.1f,1,"x");
    }
    if(behavior==4) {
        changed|=Float("Repeat Interval","##interval",t.magneticShotIntervalMs,0,5000," ms");
        changed|=Float("Aim Pause","##pause",t.magneticPostFirePauseMs,0,500," ms");
        changed|=Float("Aim Recovery","##recovery",t.magneticRecoveryMs,0,1000," ms");
        changed|=Float("Pause X Scale","##pauseX",t.magneticPostFireYawScale,0,1);
        changed|=Float("Pause Y Scale","##pauseY",t.magneticPostFirePitchScale,0,1);
        changed|=Float("Pre-fire Window","##boostWindow",t.magneticPreFireBoostWindowMs,0,250," ms");
        changed|=Float("Pre-fire Boost","##boost",t.magneticPreFireBoostScale,1,3,"x");
    }
    SettingRow("Max Aim Time");PushControlWidth();
    changed|=UISlider("##maxAimTime",&t.maxAimTime,0,100,AtOrAbove("Endless",100,AtOrBelow("Endless",0,Number(0," ms",20))));
    ImGui::PopItemWidth();
    SettingRow("Move Split");changed|=UICheckbox("##split",&t.moveSplitEnabled);
    if(t.moveSplitEnabled) {
        SettingRow("Split Chunk");PushControlWidth();changed|=UISlider("##chunk",&t.moveSplitMaxPixels,1,50,Number(0," counts"));ImGui::PopItemWidth();
        SettingRow("Split Delay");PushControlWidth();changed|=UISlider("##delay",&t.moveSplitDelayUs,0,10000,Number(0," us"));ImGui::PopItemWidth();
    }
    OW::Config::NormalizeBehaviorTuning(t);return changed;
}
}
