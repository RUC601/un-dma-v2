#include "CameraWorker.hpp"
#include "Game/Overwatch.hpp"
#include "runtime/Acquisition.hpp"

DWORD ViewMatrixPollSleepMs() { return 0; }
DWORD ViewMatrixScanBackoffMs() { return 0; }
DWORD ViewMatrixScanDueGuardMs() { return 0; }
uint64_t CriticalMatrixPollPeriodUs() { return 1000000 / V2::Acquisition::kTargetHz; }
uint64_t CriticalMatrixSpinTailUs() { return 0; }
bool CriticalMatrixAboveNormalRequested() { return false; }

namespace {
struct CameraState {
    VMMDLL_SCATTER_HANDLE scatter = mem.CreateScatterHandle();
    uint64_t camera=0, projection=0, epoch=0, scene=0;
    DWORD resolvedAt=0, lastLog=0;
    bool hasStatus=false, lastValid=false;
    OW::ViewMatrixPublishState publication{};
    ~CameraState() { if(scatter) mem.CloseScatterHandle(scatter); }
};
}

bool SampleAcquisitionCamera() {
    thread_local CameraState state;
    Diagnostics::ScopedDmaCallsite tag(Diagnostics::DmaCallsite::ViewMatrix);
    const auto epoch=OW::ProcessConnection::ConnectionEpoch();
    const auto scene=OW::EntitySceneGeneration();
    const DWORD now=GetTickCount();
    const auto& offsets=OW::offset::Active();
    if(!state.camera || epoch!=state.epoch || scene!=state.scene || now-state.resolvedAt>=500) {
        const uint64_t root=OW::SDK->RPM<uint64_t>(OW::SDK->dwGameBase+offsets.Address_viewmatrix_base);
        const uint64_t decoded=OW::offset::DecodeCameraRoot(offsets, root);
        const uint64_t p1=root ? OW::SDK->RPM<uint64_t>(decoded+offsets.VM_P1) : 0;
        const uint64_t p2=p1 ? OW::SDK->RPM<uint64_t>(p1+offsets.VM_P2) : 0;
        uint64_t projection=p2 ? p2+offsets.VM_ProjMatrix : 0;
        if(p2 && offsets.projectionFromRender) {
            const uint64_t parent=OW::SDK->RPM<uint64_t>(p2+offsets.VM_ViewProjectionParent);
            const uint64_t render=parent ? OW::SDK->RPM<uint64_t>(parent+offsets.VM_ViewProjectionPtr) : 0;
            projection=render ? render+offsets.VM_ProjMatrix : 0;
        }
        state.camera=p2 ? p2+offsets.VM_ViewMatrix : 0;
        state.projection=projection; state.resolvedAt=now;
        if(epoch!=state.epoch || scene!=state.scene) state.publication={};
        state.epoch=epoch; state.scene=scene;
    }
    if(!state.camera || !state.projection || !state.scatter) return false;
    OW::Matrix camera{}, projection{};
    DWORD cameraBytes=0, projectionBytes=0;
    const uint64_t started=GetTickCount64();
    const bool cameraPrepared=mem.AddScatterReadRequest(state.scatter,state.camera,&camera,sizeof(camera),&cameraBytes);
    const bool projectionPrepared=mem.AddScatterReadRequest(state.scatter,state.projection,&projection,sizeof(projection),&projectionBytes);
    const bool executed=mem.ExecuteReadScatter(state.scatter);
    const uint64_t completed=GetTickCount64();
    if(!cameraPrepared || !projectionPrepared || !executed || cameraBytes!=sizeof(camera) || projectionBytes!=sizeof(projection)) {
        state.camera=0; return false;
    }
    const auto combined=OW::ComposeCameraProjection(camera,projection);
    OW::Matrix publishedCamera{};
    const char* reason=nullptr;
    const bool published=OW::TryPublishViewMatrices(combined,camera,true,true,epoch,
        state.publication,publishedCamera,reason,started,completed);
    if(!published) {
        state.camera=0;
        OW::RecordViewMatrixRejected(reason,state.camera,state.projection,state.hasStatus,state.lastValid,state.lastLog);
        return false;
    }
    RuntimeTrace::Emit(RuntimeTrace::CameraRead,started,completed,1,epoch,&camera.m11,16);
    OW::RecordViewMatrixResolved(state.projection,state.camera,true,state.hasStatus,state.lastValid,state.lastLog);
    return true;
}
