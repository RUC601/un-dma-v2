#pragma once
#include "Game/Entity.hpp"
#include <memory>
#include "Game/RuntimeState.hpp"
#include "Utils/ProcessConnection.hpp"
extern std::mutex g_mutex;
namespace OW {
// 发布端持有互斥锁写入；消费端取值，不返回可变引用。
// 这里集中身体 100 ms / scene 与矩阵 connection-epoch 新鲜度门槛。
    extern Matrix viewMatrix;
    extern Matrix viewMatrix_xor;
    extern std::mutex g_viewMatrixMutex;
    extern ViewMatrixSampleState viewMatrixSampleState;
    extern float WX;
    extern float WY;
    extern std::vector<c_entity> entities;
    extern c_entity local_entity;
    extern std::vector<hpanddy> hp_dy_entities;
    extern std::vector<c_entity> present_entities;
    extern std::vector<hpanddy> present_hp_dy_entities;
    extern std::mutex g_presentEntityMutex;
    extern std::vector<c_entity> present_render_entities;
    extern std::vector<hpanddy> present_render_hp_dy_entities;
    extern std::mutex g_presentRenderEntityMutex;

    struct ConsumptionFrame {
        uint64_t sequence=0;
        Matrix view{}, camera{};
        ViewMatrixSampleState cameraSample{};
        c_entity local{};
        std::vector<c_entity> entities;
        std::vector<hpanddy> dynamic;
        std::chrono::steady_clock::time_point sampledAt{};
    };
    inline std::mutex consumptionFrameMutex;
    inline std::shared_ptr<const ConsumptionFrame> consumptionFrame=std::make_shared<ConsumptionFrame>();
    inline thread_local std::shared_ptr<const ConsumptionFrame> consumerFrame;
    inline std::shared_ptr<const ConsumptionFrame> SnapshotFrame() {
        std::lock_guard<std::mutex> lock(consumptionFrameMutex);
        return consumptionFrame;
    }
    inline void PublishFrame(std::shared_ptr<const ConsumptionFrame> frame) {
        std::lock_guard<std::mutex> lock(consumptionFrameMutex);
        consumptionFrame=std::move(frame);
    }
    struct ScopedConsumptionFrame {
        std::shared_ptr<const ConsumptionFrame> previous=consumerFrame;
        ScopedConsumptionFrame() { consumerFrame=SnapshotFrame(); }
        ~ScopedConsumptionFrame() { consumerFrame=std::move(previous); }
        void Refresh() { consumerFrame=SnapshotFrame(); }
        const ConsumptionFrame& Get() const { return *consumerFrame; }
    };

    inline bool TrySnapshotViewMatrices(Matrix& view, Matrix& viewXor, uint64_t* sequence=nullptr) {
        if(consumerFrame) {
            const auto& frame=*consumerFrame;
            const bool fresh=RuntimeWorkersRunning() && IsViewMatrixSampleFresh(frame.cameraSample,GetTickCount64(),
                ProcessConnection::ConnectionEpoch(),ProcessConnection::IsConnected());
            view=fresh?frame.view:Matrix{};viewXor=fresh?frame.camera:Matrix{};
            if(sequence)*sequence=frame.cameraSample.publishSequence;
            return fresh;
        }
        std::lock_guard<std::mutex> lock(g_viewMatrixMutex);
        if (!RuntimeWorkersRunning() ||
            !IsViewMatrixSampleFresh(viewMatrixSampleState, GetTickCount64(),
                                     ProcessConnection::ConnectionEpoch(),
                                     ProcessConnection::IsConnected())) {
            view = {};
            viewXor = {};
            return false;
        }
        if(sequence)*sequence=viewMatrixSampleState.publishSequence;
        view = viewMatrix;
        viewXor = viewMatrix_xor;
        return true;
    }

    inline void SnapshotViewMatrices(Matrix& view, Matrix& viewXor) {
        TrySnapshotViewMatrices(view, viewXor);
    }

    inline bool HasFreshViewMatrices() {
        if(consumerFrame)return RuntimeWorkersRunning() && IsViewMatrixSampleFresh(consumerFrame->cameraSample,
            GetTickCount64(),ProcessConnection::ConnectionEpoch(),ProcessConnection::IsConnected());
        std::lock_guard<std::mutex> lock(g_viewMatrixMutex);
        return RuntimeWorkersRunning() &&
            IsViewMatrixSampleFresh(viewMatrixSampleState, GetTickCount64(),
                                      ProcessConnection::ConnectionEpoch(),
                                      ProcessConnection::IsConnected());
    }

    inline Matrix SnapshotViewMatrix() {
        Matrix view{}, viewXor{};
        SnapshotViewMatrices(view, viewXor);
        return view;
    }

    inline Vector3 SnapshotCameraPosition() {
        Matrix view{}, viewXor{};
        SnapshotViewMatrices(view, viewXor);
        const auto camera = viewXor.get_location();
        return Vector3(camera.x, camera.y, camera.z);
    }

    inline c_entity SnapshotLocalEntity() {
        const auto frame=consumerFrame?consumerFrame:SnapshotFrame();
        return frame->local.HasUsableBody(GetTickCount(),EntitySceneGeneration())?frame->local:c_entity{};
    }

    inline bool TryCommitLocalEntity(const c_entity& entity,uint32_t now) {
        if(!entity.address || !entity.AngleBase || !entity.actor_generation || !entity.body_authority_revision || !entity.HasFreshLifeState(now) ||
            !GameData::IsKnownHeroId(entity.HeroID) || !c_entity::IsPlausibleSpatialAnchor(entity.pos) ||
            !EntityRosterPolicy::BodyUsable(entity.body_valid,entity.Alive,
                entity.roster_state==EntityRosterState::Fresh,entity.anchor_sample_tick_ms,now,
                entity.scene_generation,EntitySceneGeneration()))return false;
        std::lock_guard<std::mutex> lock(::g_mutex);
        std::lock_guard<std::mutex> registryLock(entityRegistryMonitor.mutex);
        std::lock_guard<std::mutex> bodyLock(entityBodyValidityMutex);
        const auto authority=entityBodyAuthorities.find(entity.address);
        if(!RuntimeWorkersRunning() || !ProcessConnection::IsConnected() ||
            ProcessConnection::ConnectionEpoch()!=entity.connection_epoch ||
            entity.scene_generation!=EntitySceneGeneration() || !entityRegistryMonitor.state.available ||
            authority==entityBodyAuthorities.end() || !authority->second.alive ||
            authority->second.registry!=entityRegistryMonitor.state.snapshot.generation ||
            !EntityBodyEvidenceCurrentLocked({entity.address,entity.LinkParent,entity.HeroID,entity.match_id,entity.last_discovered_tick_ms},
                entity.scene_generation,now,entity.body_absence_revision) ||
            !EntityBodyAuthorityMatchesLocked({entity.address,entity.LinkParent,entity.HeroID,entity.match_id,entity.last_discovered_tick_ms},
                entity.actor_generation,entity.body_authority_revision,entity.connection_epoch,
                entity.scene_generation,entity.body_absence_revision))return false;
        local_entity=entity;
        SDK->g_player_controller=entity.AngleBase;
        return true;
    }

    inline uint64_t SnapshotLocalHeroIdForStatus() {
        std::lock_guard<std::mutex> lock(::g_mutex);
        if (!RuntimeWorkersRunning() || !ProcessConnection::IsConnected())
            return 0;
        return local_entity.HeroIdForStatus(
            ProcessConnection::ConnectionEpoch(), EntitySceneGeneration());
    }

    inline uint64_t SnapshotLocalSkillBase() {
        return SnapshotLocalEntity().SkillBase;
    }

}
