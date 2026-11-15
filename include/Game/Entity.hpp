#pragma once
#include <cstdint>
#include <array>
#include <string>
#include <cmath>
#include <cstring>
#include <span>
#include <mutex>
#include <DirectXMath.h>
using namespace DirectX;

#include "Game/ModelShapes.hpp"
#include "Game/ModelWorldTransform.hpp"
#include "Game/NativeHitboxes.hpp"
#include "Game/BoneSlots.hpp"
#include "Game/SkeletonRead.hpp"
#include "Game/SkeletonBatch.hpp"
#include "Game/SkeletonSemantics.hpp"
#include "Game/Structs.hpp"
#include "Game/PresentationTiming.hpp"
#include "Game/EntityRosterPolicy.hpp"
#include "Game/OverlayStatus.hpp"
#include "Game/HeroSkillSignals.hpp"
#include "Game/RuntimeState.hpp"
#include "Game/Offsets.hpp"
#include "Game/SDK.hpp"
#include "Utils/Diagnostics.hpp"

namespace OW {

    enum class PresentSnapshotSource : uint8_t {
        Raw = 0,
        Interp,
        Extrap,
        Hold
    };

    inline const char* PresentSnapshotSourceName(PresentSnapshotSource source) {
        switch (source) {
        case PresentSnapshotSource::Raw: return "raw";
        case PresentSnapshotSource::Interp: return "interp";
        case PresentSnapshotSource::Extrap: return "extrap";
        case PresentSnapshotSource::Hold: return "hold";
        default: return "unknown";
        }
    }

    enum class EntityRosterState : uint8_t {
        Fresh = 0,
        Missing,
        Dead
    };

    class c_entity {
    public:
        // --- Base addresses ---
        uint64_t address       = 0;
        uint64_t LinkParent    = 0;
        uint64_t LinkBase      = 0;
        uint64_t HealthBase    = 0;
        uint64_t TeamBase      = 0;
        uint64_t VelocityBase  = 0;
        uint64_t HeroBase      = 0;
        uint64_t BoneBase      = 0;
        uint64_t OutlineBase   = 0;
        uint64_t SkillBase     = 0;
        uint64_t RotationBase  = 0;
        uint64_t VisBase       = 0;
        uint64_t AngleBase     = 0;
        uint64_t EnemyAngleBase = 0;
        uint64_t ObjectBase    = 0;
        uint64_t HeroID        = 0;
        uint64_t statcombase   = 0;

        int      head_index    = 0;
        uint32_t PlayerID      = 0;
        uint16_t Dva           = 0;

        std::string battletag;

        // --- Health ---
        float PlayerHealth      = 0.f;
        float PlayerHealthMax   = 0.f;
        uint32_t health_sample_tick_ms = 0;
        float MinHealth         = 0.f;
        float MaxHealth         = 0.f;
        float MinArmorHealth    = 0.f;
        float MaxArmorHealth    = 0.f;
        float MinBarrierHealth  = 0.f;
        float MaxBarrierHealth  = 0.f;

        bool barrprot = false;
        bool sombraStealthKnown=false, sombraStealth=false;
        bool imort    = false;

        // --- State ---
        float ULT    = 0.f;
        bool  Alive  = true;
        bool death_known = false;
        bool is_dead = false;
        uint32_t death_sample_tick_ms = 0;
        bool  Vis    = false;
        bool  Team   = false;
        bool  team_known = false;
        uint32_t team_key = 0;
        bool  Trg    = false;
        EntityRosterState roster_state = EntityRosterState::Fresh;
        uint64_t roster_key = 0;
        uint64_t participant_key = 0;
        uint64_t actor_generation = 0;
        uint64_t connection_epoch = 0;
        uint64_t scene_generation = 0;
        uint64_t body_absence_revision = 0;
        uint64_t body_observation_fence = 0;
        uint64_t body_authority_revision = 0;
        uint32_t last_discovered_tick_ms = 0;
        bool body_valid = false;
        uint64_t publish_sequence = 0;
        uint32_t skeleton_sample_tick_ms = 0;
        uint64_t actual_scan_generation = 0;
        uint8_t actual_scan_source = 0;
        uint32_t match_id = 0;
        uint32_t last_seen_tick_ms = 0;
        uint32_t missing_since_tick_ms = 0;
        uint32_t last_scan_seen_tick_ms = 0;
        uint32_t last_hot_read_tick_ms = 0;
        uint32_t scan_seen_gap_ms = 0;
        uint32_t hot_read_gap_ms = 0;
        uint32_t consecutive_scan_miss_count = 0;

        bool   skill1act = false;
        bool   skill2act = false;
        std::array<uint32_t, 2> skill_active_tick_ms{};
        float  ultimate  = 0.f;
        float  skillcd1  = 0.f;
        float  skillcd2  = 0.f;
        float  skillready = 0.f;
        std::array<OverlayStatus::CooldownSample, 2> display_cooldowns{};
        uint32_t display_ultimate_tick = 0;
        HeroSkillSignals::Sample hero_skill_signals{};

        // --- Positions ---
        Vector3 head_pos{};
        Vector3 view_direction{};
        Vector3 velocity{};
        Vector3 Rot{};
        bool rotation_valid = false;
        Vector3 pos{};
        Vector3 neck_pos{};
        Vector3 chest_pos{};
        ModelShapes::Sample model_shapes{};
        NativeHitboxes::Sample native_hitboxes{};
        ModelWorld::Sample model_world{};
        Skeleton::BodyIdentity model_world_identity{};
        std::array<Vector3, 18> skeleton_bones{};
        std::array<bool, 18> skeleton_bone_valid{};
        SkeletonSemantics::Form skeleton_form = SkeletonSemantics::Form::Unknown;
        SkeletonSemantics::Map skeleton_requested_ids = SkeletonSemantics::kUnavailableMap;
        uint64_t skeleton_revision = 0;
        Skeleton::ReadStatus skeleton_status = Skeleton::ReadStatus::Empty;
        std::array<SkeletonSemantics::CoreAnchorSource, 3> core_anchor_source{};
        Vector3 cached_bot_chest_bone{};
        bool cached_bot_chest_bone_valid = false;

        // Render interpolation samples. The processing thread writes the current
        // positions and carries the previous published positions for the renderer.
        uint64_t sample_time_us = 0, previous_sample_time_us = 0;
        uint32_t render_sample_tick_ms = 0;
        uint32_t previous_render_sample_tick_ms = 0;
        uint32_t position_sample_tick_ms = 0;
        // Observation clocks belong to the coordinates, not their publication.
        uint32_t anchor_sample_tick_ms = 0;
        uint32_t read_started_tick_ms = 0;
        uint32_t read_completed_tick_ms = 0;
        uint32_t published_tick_ms = 0;
        bool has_previous_render_sample = false;
        Vector3 previous_head_pos{};
        Vector3 previous_velocity{};
        Vector3 previous_pos{};
        Vector3 previous_neck_pos{};
        Vector3 previous_chest_pos{};
        std::array<Vector3, 18> previous_skeleton_bones{};
        std::array<bool, 18> previous_skeleton_bone_valid{};
        Vector3 previous_cached_bot_chest_bone{};
        bool previous_cached_bot_chest_bone_valid = false;
        PresentSnapshotSource present_source = PresentSnapshotSource::Raw;
        uint32_t present_raw_sample_tick_ms = 0;
        uint32_t present_tick_ms = 0;
        uint64_t present_tick_us = 0;
        uint32_t present_age_ms = 0;
        float present_prediction_ms = 0.0f;
        float present_confidence = 1.0f;

        uint64_t bone_fallback_logged_offset = UINT64_MAX;
        PresentationTiming::ActorIdentity PresentationIdentity() const {
            return {address,LinkParent,actor_generation,HeroID,match_id,Alive,roster_state==EntityRosterState::Fresh};
        }
        bool HasFreshLifeState(uint32_t now) const {
            return death_known && !is_dead && death_sample_tick_ms!=0 && uint32_t(now-death_sample_tick_ms)<=100;
        }
        bool HasUsableBody(uint32_t now, uint64_t scene) const {
            return HasFreshLifeState(now) &&
                EntityRosterPolicy::BodyUsable(body_valid, Alive,
                roster_state == EntityRosterState::Fresh, anchor_sample_tick_ms, now, scene_generation, scene) &&
                EntityBodyCurrent({address,LinkParent,HeroID,match_id,last_discovered_tick_ms},actor_generation,
                    body_authority_revision,connection_epoch,scene_generation,body_absence_revision);
        }
        uint64_t HeroIdForStatus(uint64_t epoch, uint64_t scene) const {
            // Configuration labels need identity, not a live spatial sample.
            return epoch != 0 && scene != 0 && connection_epoch == epoch &&
                scene_generation == scene && GameData::IsKnownHeroId(HeroID) ? HeroID : 0;
        }
        bool debugHeadLookupAttempted = false;
        bool debugHeadLookupResolved = false;
        bool debugHeadIdFound = false;
        bool debugHeadLocalFinite = false;
        bool debugHeadLocalNonZero = false;
        bool debugHeadWorldNonZero = false;
        bool debugHeadLookupException = false;
        uint64_t debugHeadBoneData = 0;
        uint64_t debugHeadBonesBase = 0;
        uint64_t debugHeadBonePtr = 0;
        uint64_t debugHeadBoneIdTable = 0;
        uint64_t debugHeadLocalAddress = 0;
        uint16_t debugHeadBoneCount = 0;
        int debugHeadMappedIndex = -1;
        XMFLOAT3 debugHeadLocal{};
        Vector3 debugHeadWorld{};

        // =====================================================================
        // Constructors / operators
        // =====================================================================
        c_entity() : address(0) {}
        explicit c_entity(uint64_t _UniqueID) : address(_UniqueID) {}

        bool operator==(const c_entity& entity) const {
            return (this->address == entity.address);
        }
        bool operator!=(const c_entity& entity) const {
            return (this->address != entity.address);
        }

        // =====================================================================
        // Team detection
        // =====================================================================
        uint32_t GetTeamFlags() const {
            uint32_t flags = 0;
            return TeamBase && SDK->read_range(TeamBase + OW::offset::Team_FlagsOffset,
                &flags, sizeof(flags)) ? flags : 0;
        }

        uint32_t GetTeamComparisonKey() const {
            return OW::offset::TeamComparisonKeyFromFlags(GetTeamFlags());
        }

        bool SameTeamAs(const c_entity& other) const {
            return team_known && other.team_known && team_key == other.team_key;
        }

        eTeam GetTeam() const {
            uint32_t teamBits = GetTeamFlags() & OW::offset::Team_LegacyMask;
            if (!EntityRosterPolicy::ValidTeamKey(teamBits)) return eTeam::TEAM_UNCONFIRMED;
            std::bitset<sizeof(int) * CHAR_BIT> bitTeam(teamBits);
            if (bitTeam[0x17]) return eTeam::TEAM_RED;
            if (bitTeam[0x18]) return eTeam::TEAM_BLUE;
            if (bitTeam[0x19]) return eTeam::TEAM_UNKNOWN1;
            if (bitTeam[0x1A]) return eTeam::TEAM_UNKNOWN2;
            if (bitTeam[0x1B]) return eTeam::TEAM_DEATHMATCH;
            return eTeam::TEAM_UNCONFIRMED;
        }

        // =====================================================================
        // Bone helpers
        // =====================================================================
        static constexpr uint16_t kMaxBoneIdCount = 1024;

        struct BoneDebugProbe {
            int requestedBoneId = 0;
            bool preconditions = false;
            bool resolved = false;
            bool idFound = false;
            bool localFinite = false;
            bool localNonZero = false;
            bool worldNonZero = false;
            bool exception = false;
            uint64_t boneData = 0;
            uint64_t bonesBase = 0;
            uint64_t bonePtr = 0;
            uint64_t boneIdTable = 0;
            uint64_t localAddress = 0;
            uint16_t boneCount = 0;
            int mappedIndex = -1;
            XMFLOAT3 local{};
            Vector3 world{};
        };

        struct BoneResolveInfo {
            uint64_t boneData = 0;
            uint64_t bonesBase = 0;
            uint64_t bonePtr = 0;
            uint64_t boneIdTable = 0;
            uint16_t boneCount = 0;
        };

        struct SkeletonBoneCache {
            bool valid = false;
            Skeleton::BodyIdentity identity{};
            Skeleton::Layout layout{};
            SkeletonSemantics::Form form = SkeletonSemantics::Form::Unknown;
            uint64_t revision = 0, hits = 0, misses = 0;
            Skeleton::ReadMetrics metrics{};
            Skeleton::GuardFailureSeries guardFailures{};
            Skeleton::ReadStatus status = Skeleton::ReadStatus::Empty;
            uint64_t heroId = 0;
            uint64_t boneData = 0;
            uint64_t velocityBoneData = 0;
            uint64_t bonesBase = 0;
            uint64_t bonePtr = 0;
            uint64_t boneIdTable = 0;
            uint16_t boneCount = 0;
            std::array<int, 18> requestedBoneIds{};
            std::array<int, 18> mappedIndices{};
            int maxMappedIndex = -1;
            int botChestMappedIndex = -1;
            bool hasBotChest = false;
            std::vector<int> shapeMapped;
            int shapeModelIndex=-1;
            int shapeMaxIndex = -1;
        };

        static bool IsFiniteBoneValue(const XMFLOAT3& value) {
            return std::isfinite(value.x) && std::isfinite(value.y) && std::isfinite(value.z);
        }

        static bool IsNonZeroBoneValue(const XMFLOAT3& value) {
            return value.x != 0.0f || value.y != 0.0f || value.z != 0.0f;
        }

        static bool IsFiniteVectorValue(const Vector3& value) {
            return std::isfinite(value.X) && std::isfinite(value.Y) && std::isfinite(value.Z);
        }

        static bool IsPlausibleSpatialAnchor(const Vector3& value) {
            return IsFiniteVectorValue(value) &&
                   std::fabs(value.X) < 5000.0f &&
                   std::fabs(value.Y) < 5000.0f &&
                   std::fabs(value.Z) < 5000.0f &&
                   value.DistTo(Vector3(0, 0, 0)) > 1.0f;
        }

        // A valid actor position is still useful when a partial skeleton read
        // misses one or more core bones. Keep real bone-derived anchors and
        // synthesize only the missing aliases used by ESP and target selection.
        bool FillMissingCoreAnchorsFromPosition() {
            if (!Alive || !IsPlausibleSpatialAnchor(pos))
                return false;

            bool filled = false;
            auto fillMissing = [&](Vector3& anchor, float height, size_t slot) {
                if (IsPlausibleSpatialAnchor(anchor)) {
                    if (core_anchor_source[slot] == SkeletonSemantics::CoreAnchorSource::Unavailable)
                        core_anchor_source[slot] = SkeletonSemantics::CoreAnchorSource::PositionEstimate;
                    return;
                }
                anchor = pos + Vector3(0.0f, height, 0.0f);
                core_anchor_source[slot] = SkeletonSemantics::CoreAnchorSource::PositionEstimate;
                filled = true;
            };

            std::array<float, 3> heights{1.65f, 1.35f, 1.05f};
            // The established synthetic bot type owns its geometry in both
            // cold and hot samples; changing health must not change its anchors.
            if (offset::UsesProtectedEntityLayout() && !HeroBase) {
                if (HeroID == eHero::HERO_TRAININGBOT4)
                    heights = {1.45f, 1.10f, 0.75f};
                else if (HeroID == eHero::HERO_TRAININGBOT5)
                    heights = {2.00f, 1.55f, 1.05f};
            }
            fillMissing(head_pos, heights[0], 0);
            fillMissing(neck_pos, heights[1], 1);
            fillMissing(chest_pos, heights[2], 2);
            return filled;
        }

        int get_bone_id(uint64_t bonedata, int bone_id) {
            const bool debugHead = bone_id == OW::BONE_HEAD;
            if (debugHead) {
                debugHeadLookupAttempted = true;
                debugHeadLookupResolved = false;
                debugHeadIdFound = false;
                debugHeadLookupException = false;
                debugHeadBoneData = bonedata;
                debugHeadBonesBase = 0;
                debugHeadBonePtr = 0;
                debugHeadBoneIdTable = 0;
                debugHeadBoneCount = 0;
                debugHeadMappedIndex = -1;
                debugHeadLocalAddress = 0;
                debugHeadLocal = {};
                debugHeadWorld = {};
                debugHeadLocalFinite = false;
                debugHeadLocalNonZero = false;
                debugHeadWorldNonZero = false;
            }
            __try {
                uint64_t bonePtr = SDK->RPM<uint64_t>(bonedata);
                if (debugHead)
                    debugHeadBonePtr = bonePtr;
                if (!bonePtr)
                    return -1;

                uint64_t boneIdTable = SDK->RPM<uint64_t>(bonePtr + 0x38);
                if (debugHead)
                    debugHeadBoneIdTable = boneIdTable;
                if (!boneIdTable)
                    return -1;

                uint16_t count = SDK->RPM<uint16_t>(bonePtr + Skeleton::BoneCountOffset());
                if (debugHead)
                    debugHeadBoneCount = count;
                if (count == 0 || count > kMaxBoneIdCount)
                    return -1;

                if (debugHead)
                    debugHeadLookupResolved = true;

                std::array<uint32_t, kMaxBoneIdCount> boneIds{};
                if (!SDK->read_range(boneIdTable, boneIds.data(), sizeof(uint32_t) * count))
                    return -1;

                for (uint16_t i = 0; i < count; i++) {
                    if (static_cast<uint16_t>(boneIds[i]) == bone_id) {
                        if (debugHead) {
                            debugHeadIdFound = true;
                            debugHeadMappedIndex = static_cast<int>(i);
                        }
                        return i;
                    }
                }
            } __except (EXCEPTION_EXECUTE_HANDLER) {
                if (debugHead)
                    debugHeadLookupException = true;
            }
            return -1;
        }

        bool ResolveBoneDataCandidate(uint64_t pBoneData, BoneResolveInfo& info) {
            info = {};
            if (!pBoneData)
                return false;

            info.boneData = pBoneData;
            info.bonesBase = SDK->RPM<uint64_t>(pBoneData + 0x20);
            if (!info.bonesBase)
                return false;

            info.bonePtr = SDK->RPM<uint64_t>(pBoneData);
            if (!info.bonePtr)
                return false;

            info.boneIdTable = SDK->RPM<uint64_t>(info.bonePtr + 0x38);
            if (!info.boneIdTable)
                return false;

            info.boneCount = SDK->RPM<uint16_t>(info.bonePtr + Skeleton::BoneCountOffset());
            return info.boneCount > 0 && info.boneCount <= kMaxBoneIdCount;
        }

        bool ResolveBoneDataCandidate(uint64_t pBoneData, uint64_t& bonesBase) {
            BoneResolveInfo info{};
            const bool resolved = ResolveBoneDataCandidate(pBoneData, info);
            bonesBase = info.bonesBase;
            return resolved;
        }

        BoneResolveInfo ResolveBoneDataInfo(uint64_t knownVelocityBoneData = 0) {
            BoneResolveInfo info{};
            if (this->VelocityBase) {
                const uint64_t pBoneData = knownVelocityBoneData
                    ? knownVelocityBoneData
                    : SDK->RPM<uint64_t>(this->VelocityBase + 0x8B0);
                if (ResolveBoneDataCandidate(pBoneData, info))
                    return info;
            }

            if (!this->BoneBase)
                return {};

            constexpr std::array<uint64_t, 3> entryOffsets = { 0x20, 0x0, 0x10 };
            for (const uint64_t entryOffset : entryOffsets) {
                const uint64_t pBoneData = SDK->RPM<uint64_t>(this->BoneBase + entryOffset);
                if (!ResolveBoneDataCandidate(pBoneData, info))
                    continue;

                if (this->bone_fallback_logged_offset != entryOffset) {
                    Diagnostics::Info(
                        "[BONE] BoneBase fallback resolved entry=0x%llX bone_base=0x%llX bonedata=0x%llX bones_base=0x%llX.",
                        static_cast<unsigned long long>(entryOffset),
                        static_cast<unsigned long long>(this->BoneBase),
                        static_cast<unsigned long long>(info.boneData),
                        static_cast<unsigned long long>(info.bonesBase));
                    this->bone_fallback_logged_offset = entryOffset;
                }
                return info;
            }

            return {};
        }

        uint64_t ResolveBoneData(uint64_t& bonesBase) {
            const BoneResolveInfo info = ResolveBoneDataInfo();
            bonesBase = info.bonesBase;
            return info.boneData;
        }

        BoneDebugProbe ProbeBone(int boneId) {
            BoneDebugProbe probe{};
            probe.requestedBoneId = boneId;
            __try {
                probe.preconditions = this->pos != Vector3(0, 0, 0) && this->PlayerHealth > 0;
                if (!probe.preconditions)
                    return probe;

                probe.boneData = ResolveBoneData(probe.bonesBase);
                if (!probe.boneData || !probe.bonesBase)
                    return probe;

                probe.bonePtr = SDK->RPM<uint64_t>(probe.boneData);
                if (!probe.bonePtr)
                    return probe;

                probe.boneIdTable = SDK->RPM<uint64_t>(probe.bonePtr + 0x38);
                if (!probe.boneIdTable)
                    return probe;

                probe.boneCount = SDK->RPM<uint16_t>(probe.bonePtr + Skeleton::BoneCountOffset());
                if (probe.boneCount == 0 || probe.boneCount > kMaxBoneIdCount)
                    return probe;

                probe.resolved = true;
                std::array<uint32_t, kMaxBoneIdCount> boneIds{};
                if (!SDK->read_range(probe.boneIdTable, boneIds.data(), sizeof(uint32_t) * probe.boneCount))
                    return probe;

                for (uint16_t i = 0; i < probe.boneCount; ++i) {
                    if (static_cast<uint16_t>(boneIds[i]) == boneId) {
                        probe.idFound = true;
                        probe.mappedIndex = static_cast<int>(i);
                        break;
                    }
                }

                if (!probe.idFound)
                    return probe;

                probe.localAddress = probe.bonesBase + (0x30 * static_cast<uint64_t>(probe.mappedIndex)) + 0x20;
                probe.local = SDK->RPM<XMFLOAT3>(probe.localAddress);
                probe.localFinite = IsFiniteBoneValue(probe.local);
                probe.localNonZero = probe.localFinite && IsNonZeroBoneValue(probe.local);
                if (!probe.localFinite)
                    return probe;

                if (!TransformBoneWorld(probe.local, probe.world)) return probe;
                probe.worldNonZero = IsFiniteVectorValue(probe.world) && probe.world != Vector3(0, 0, 0);
                return probe;
            } __except (EXCEPTION_EXECUTE_HANDLER) {
                probe.exception = true;
                return probe;
            }
        }

        // Consumers read the published snapshot. Raw address diagnostics use ProbeBone.
        Vector3 GetBonePos(int boneId) const {
            if (offset::IsCnProfile() && !HasCurrentModelWorldTransform()) return {};
            if (boneId == 83 && cached_bot_chest_bone_valid)
                return cached_bot_chest_bone;
            for (size_t slot = 0; slot < skeleton_requested_ids.size(); ++slot) {
                if (skeleton_requested_ids[slot] == boneId && skeleton_bone_valid[slot])
                    return skeleton_bones[slot];
            }
            return {};
        }

        std::array<int, 18> GetSkel() const { return GetRenderSkel(); }
        std::array<int, 18> GetRenderSkel() const { return skeleton_requested_ids; }

        bool TrySkeletonPoint(size_t slot, Vector3& point) const {
            point = {};
            if (!(offset::IsCnProfile() ? HasCurrentModelWorldTransform() : rotation_valid) ||
                skeleton_form == SkeletonSemantics::Form::Unknown || !skeleton_revision ||
                slot >= skeleton_bones.size() || !skeleton_bone_valid[slot] ||
                skeleton_requested_ids[slot] < 0 || !IsFiniteVectorValue(skeleton_bones[slot]) ||
                skeleton_bones[slot] == Vector3{}) return false;
            point = skeleton_bones[slot];
            return true;
        }

        Skeleton::BodyIdentity SkeletonIdentity() const {
            return {address, LinkParent, HeroID, actor_generation, connection_epoch,
                scene_generation, body_absence_revision, VelocityBase, BoneBase, LinkBase,
                match_id, Alive && std::isfinite(PlayerHealth) && PlayerHealth > 0.f};
        }

        void ClearModelWorldTransform() {
            model_world = {};
            model_world_identity = {};
        }

        bool SetModelWorldTransform(uint64_t modelBase, const ModelWorld::QstRecord& record) {
            const ModelWorld::QstRecord captured = record;
            ClearModelWorldTransform();
            const auto identity = SkeletonIdentity();
            if (!offset::IsCnProfile() || !identity.alive || modelBase != VelocityBase ||
                !model_world.Set(modelBase, captured)) return false;
            model_world.sampleAnchor = pos;
            model_world_identity = identity;
            return true;
        }

        bool HasCurrentModelWorldTransform() const {
            return model_world.valid && model_world.modelBase == VelocityBase &&
                model_world_identity == SkeletonIdentity();
        }

        bool HasBoneWorldTransform() const {
            return offset::IsCnProfile() ? HasCurrentModelWorldTransform() :
                rotation_valid && IsPlausibleSpatialAnchor(pos) && IsFiniteVectorValue(Rot);
        }

        bool TransformBoneWorld(const XMFLOAT3& local, Vector3& point, bool legacyBallBias = true) const {
            point = {};
            if (!IsFiniteBoneValue(local) || !HasBoneWorldTransform()) return false;
            if (offset::IsCnProfile()) {
                point = model_world.TransformPoint({local.x, local.y, local.z}) + (pos - model_world.sampleAnchor);
            } else {
                XMFLOAT3 rotated{};
                XMStoreFloat3(&rotated, XMVector3Transform(XMLoadFloat3(&local), XMMatrixRotationY(Rot.X)));
                const float bias = legacyBallBias && HeroID == eHero::HERO_WRECKINGBALL ? -.7f : 0.f;
                point = Vector3(rotated.x, rotated.y + bias, rotated.z) + pos;
            }
            return IsFiniteVectorValue(point) && point != Vector3{};
        }

        static Skeleton::ReadSource ProductionSkeletonSource(Skeleton::ReadMetrics* metrics = nullptr) {
            return {nullptr, [](void*, uint64_t address, void* buffer, size_t size) {
                return mem.ReadFreshExact(address, buffer, size);
            }, metrics};
        }

        bool ReadSkeletonRotation(uint64_t& leaf, const Skeleton::ReadSource* supplied = nullptr) {
            const auto source = supplied ? *supplied : ProductionSkeletonSource();
            rotation_valid = false;
            leaf = 0;
            uint64_t after = 0;
            Vector3 observed{};
            const uint64_t pointerAddress = Skeleton::AddressOffset(RotationBase, offset::RotationBase_Sub1);
            if (!source.Value(pointerAddress, leaf) || !leaf ||
                !source.Value(Skeleton::AddressOffset(leaf, offset::RotationBase_Sub2), observed) ||
                !source.Value(pointerAddress, after) || after != leaf || !IsFiniteVectorValue(observed)) {
                leaf = 0;
                return false;
            }
            Rot = observed;
            rotation_valid = true;
            return true;
        }

        void ClearSkeletonObservation(bool preserveModelWorld = false) {
            // Only an in-progress same-body commit may retain the current read.
            if (!preserveModelWorld || !HasCurrentModelWorldTransform()) ClearModelWorldTransform();
            skeleton_bone_valid.fill(false);
            skeleton_bones.fill(Vector3{});
            cached_bot_chest_bone = {};
            cached_bot_chest_bone_valid = false;
            model_shapes = {};
            native_hitboxes = {};
            skeleton_sample_tick_ms = 0;
            skeleton_form = SkeletonSemantics::Form::Unknown;
            skeleton_requested_ids = SkeletonSemantics::kUnavailableMap;
            head_pos = {}; neck_pos = {}; chest_pos = {};
            core_anchor_source.fill(SkeletonSemantics::CoreAnchorSource::Unavailable);
            debugHeadLookupAttempted = debugHeadLookupResolved = debugHeadIdFound = false;
            debugHeadLocalFinite = debugHeadLocalNonZero = debugHeadWorldNonZero = false;
            debugHeadLookupException = false;
            debugHeadBoneData = debugHeadBonesBase = debugHeadBonePtr = debugHeadBoneIdTable = 0;
            debugHeadLocalAddress = 0; debugHeadBoneCount = 0; debugHeadMappedIndex = -1;
            debugHeadLocal = {}; debugHeadWorld = {};
        }

        void ApplySkeletonMapping(const SkeletonBoneCache& cache) {
            skeleton_form = cache.form;
            skeleton_requested_ids = cache.requestedBoneIds;
            skeleton_revision = cache.revision;
            skeleton_status = cache.status;
        }

        void ApplySkeletonCoreAnchors() {
            Vector3* anchors[] = {&head_pos, &neck_pos, &chest_pos};
            const bool observedTransformValid = !offset::IsCnProfile() || HasCurrentModelWorldTransform();
            if (!observedTransformValid) {
                for (size_t slot = 0; slot < 3; ++slot) {
                    if (core_anchor_source[slot] == SkeletonSemantics::CoreAnchorSource::ObservedBone) {
                        *anchors[slot] = {};
                        core_anchor_source[slot] = SkeletonSemantics::CoreAnchorSource::Unavailable;
                    }
                }
            }
            for (size_t slot = 0; slot < 3; ++slot) {
                if (observedTransformValid && skeleton_bone_valid[slot]) {
                    *anchors[slot] = skeleton_bones[slot];
                    core_anchor_source[slot] = SkeletonSemantics::CoreAnchorSource::ObservedBone;
                }
            }
            if (observedTransformValid && GameData::IsTrainingBotHeroId(HeroID) && cached_bot_chest_bone_valid) {
                chest_pos = cached_bot_chest_bone;
                core_anchor_source[2] = SkeletonSemantics::CoreAnchorSource::ObservedBone;
            }
            FillMissingCoreAnchorsFromPosition();
        }

        bool ConfirmSkeletonLayout(SkeletonBoneCache& cache, uint64_t expectedBoneData = 0,
                                   const Skeleton::ReadSource* supplied = nullptr) {
            auto source = supplied ? *supplied : ProductionSkeletonSource();
            source.metrics = &cache.metrics;
            Skeleton::Layout observed{};
            const auto identity = SkeletonIdentity();
            if (!Skeleton::ObserveLayout(source, identity, HeroID == eHero::HERO_DVA, observed, cache.status) ||
                (expectedBoneData && observed.sourceAddress == Skeleton::AddressOffset(VelocityBase, 0x8B0) &&
                 expectedBoneData != observed.boneData)) {
                if (cache.status == Skeleton::ReadStatus::Ready) cache.status = Skeleton::ReadStatus::LayoutChanged;
                cache.valid = false;
                return false;
            }
            const auto form = SkeletonSemantics::ResolveForm(HeroID, observed.formKnown, observed.linkD4, observed.linkD8);
            cache.guardFailures.Reset();
            const auto ids = SkeletonSemantics::RequestedIds(HeroID, form);
            if (cache.valid && cache.identity == identity && cache.layout == observed && cache.requestedBoneIds == ids) {
                ++cache.hits;
                return true;
            }
            ++cache.misses;
            cache.revision = Skeleton::RevisionSequence.fetch_add(1, std::memory_order_relaxed) + 1;
            cache.valid = false;
            cache.identity = identity;
            cache.layout = observed;
            cache.form = form;
            cache.heroId = HeroID;
            cache.boneData = observed.boneData;
            cache.velocityBoneData = observed.boneData;
            cache.bonesBase = observed.bonesBase;
            cache.bonePtr = observed.bonePtr;
            cache.boneIdTable = observed.idTable;
            cache.boneCount = observed.count;
            cache.requestedBoneIds = ids;
            cache.mappedIndices.fill(-1);
            cache.maxMappedIndex = -1;
            cache.botChestMappedIndex = -1;
            cache.hasBotChest = GameData::IsTrainingBotHeroId(HeroID);
            const auto find = [&](int id) {
                if (id < 0) return -1;
                for (uint16_t index = 0; index < observed.count; ++index)
                    if (uint16_t(observed.ids[index]) == uint16_t(id)) return int(index);
                return -1;
            };
            for (size_t slot = 0; slot < ids.size(); ++slot) {
                cache.mappedIndices[slot] = find(ids[slot]);
                cache.maxMappedIndex = (std::max)(cache.maxMappedIndex, cache.mappedIndices[slot]);
            }
            if (cache.hasBotChest) {
                cache.botChestMappedIndex = find(83);
                cache.maxMappedIndex = (std::max)(cache.maxMappedIndex, cache.botChestMappedIndex);
            }
            cache.shapeMapped.clear();
            cache.shapeMaxIndex = -1;
            cache.shapeModelIndex = ModelShapes::ResolveModel(HeroID, observed.ids.data(), observed.count);
            if (cache.shapeModelIndex >= 0) {
                for (const auto& shape : ModelShapes::kModels[cache.shapeModelIndex].shapes) {
                    const int mapped = ModelShapes::FindBone(observed.ids.data(), observed.count, shape.boneId);
                    cache.shapeMapped.push_back(mapped);
                    cache.shapeMaxIndex = (std::max)(cache.shapeMaxIndex, mapped);
                }
            }
            cache.valid = cache.maxMappedIndex >= 0 && form != SkeletonSemantics::Form::Unknown;
            if (form == SkeletonSemantics::Form::Unknown) cache.status = Skeleton::ReadStatus::UnknownForm;
            else if (cache.maxMappedIndex < 0) cache.status = Skeleton::ReadStatus::NoMappedBones;
            return cache.valid;
        }

        bool ReconfirmSkeletonLayout(SkeletonBoneCache& cache, const Skeleton::ReadSource* supplied = nullptr) {
            auto source = supplied ? *supplied : ProductionSkeletonSource();
            source.metrics = &cache.metrics;
            Skeleton::Layout observed{};
            const bool same = cache.valid && cache.identity == SkeletonIdentity() &&
                Skeleton::ObserveLayout(source, cache.identity, HeroID == eHero::HERO_DVA, observed, cache.status) &&
                observed == cache.layout;
            if (!same) {
                if (cache.status == Skeleton::ReadStatus::Ready) cache.status = Skeleton::ReadStatus::LayoutChanged;
                cache.valid = false;
            }
            return same;
        }

        void CacheSkeletonBones(uint64_t knownVelocityBoneData = 0) {
            CacheSkeletonBonesCached(nullptr, knownVelocityBoneData);
        }

        void CacheSkeletonBones(SkeletonBoneCache& cache, uint64_t knownVelocityBoneData = 0, bool allowSlowFallback = true) {
            CacheSkeletonBonesCached(&cache, knownVelocityBoneData, allowSlowFallback);
        }

        bool CommitSkeletonPoses(SkeletonBoneCache& mapping,
                                 std::span<const Skeleton::PoseRange> ranges,
                                 std::span<const uint8_t> poses,
                                 const std::array<bool, 18>& requested, bool botRequested,
                                 const Skeleton::ReadSource* supplied = nullptr) {
            ClearSkeletonObservation(true);
            if (!HasBoneWorldTransform()) {
                mapping.status = skeleton_status = Skeleton::ReadStatus::InvalidTransform;
                return false;
            }
            if (!ReconfirmSkeletonLayout(mapping, supplied)) {
                skeleton_status = mapping.status;
                return false;
            }
            return ApplyVerifiedSkeletonPoses(mapping, ranges, poses, requested, botRequested);
        }

        bool CommitSkeletonPosesGuarded(SkeletonBoneCache& mapping,
                                       const Skeleton::LayoutGuard& before, const Skeleton::LayoutGuard& after,
                                       uint64_t revision, std::span<const Skeleton::PoseRange> ranges,
                                       std::span<const uint8_t> poses,
                                       const std::array<bool, 18>& requested, bool botRequested,
                                       uint32_t sampleTick = 0) {
            ClearSkeletonObservation(true);
            const bool current = mapping.valid && revision == mapping.revision && mapping.identity == SkeletonIdentity();
            const auto first=before.Compare(mapping.layout),last=after.Compare(mapping.layout);
            if (!current || first==Skeleton::GuardResult::Changed || last==Skeleton::GuardResult::Changed) {
                mapping.valid = false;
                mapping.status = skeleton_status = Skeleton::ReadStatus::LayoutChanged;
                return false;
            }
            if(first!=Skeleton::GuardResult::Same || last!=Skeleton::GuardResult::Same) {
                if(mapping.guardFailures.Expired(sampleTick,PresentationTiming::kAnchorFreshMs))mapping.valid=false;
                mapping.status=skeleton_status=Skeleton::ReadStatus::DescriptorReadFailed;
                return false;
            }
            mapping.guardFailures.Reset();
            mapping.status=Skeleton::ReadStatus::Ready;
            for (size_t slot = 0; slot < requested.size(); ++slot) {
                const int index = mapping.mappedIndices[slot];
                if (requested[slot] && index >= 0 && (!before.Covers(index) || !after.Covers(index))) {
                    mapping.status = skeleton_status = Skeleton::ReadStatus::PoseReadFailed;
                    return false;
                }
            }
            if (botRequested && mapping.hasBotChest && mapping.botChestMappedIndex >= 0 &&
                (!before.Covers(mapping.botChestMappedIndex) || !after.Covers(mapping.botChestMappedIndex))) {
                mapping.status = skeleton_status = Skeleton::ReadStatus::PoseReadFailed;
                return false;
            }
            if (!HasBoneWorldTransform()) {
                mapping.status = skeleton_status = Skeleton::ReadStatus::InvalidTransform;
                return false;
            }
            ++mapping.hits;
            return ApplyVerifiedSkeletonPoses(mapping, ranges, poses, requested, botRequested);
        }

        bool ApplyVerifiedSkeletonPoses(SkeletonBoneCache& mapping,
                                       std::span<const Skeleton::PoseRange> ranges,
                                       std::span<const uint8_t> poses,
                                       const std::array<bool, 18>& requested, bool botRequested) {
            ApplySkeletonMapping(mapping);
            // CN uses the model transform sampled beside these poses; no
            // velocity-origin or Wrecking Ball height correction is added.
            const bool useModelWorld = offset::IsCnProfile();
            const auto rotation = useModelWorld ? XMMatrixIdentity() : XMMatrixRotationY(Rot.X);
            const auto world = [&](int index, Vector3& point) {
                if (index < 0 || index >= mapping.boneCount) return false;
                for (const auto& range : ranges) {
                    if (!range.prepared || index < range.first || index > range.last || range.bytes != range.size ||
                        range.offset > poses.size() || range.size > poses.size() - range.offset) continue;
                    const size_t localOffset = 0x30 * size_t(index - range.first);
                    if (localOffset > range.size || sizeof(XMFLOAT3) > range.size - localOffset) continue;
                    XMFLOAT3 local{};
                    std::memcpy(&local, poses.data() + range.offset + localOffset, sizeof(local));
                    if (index == mapping.mappedIndices[0]) {
                        debugHeadLocal = local;
                        debugHeadLocalAddress = mapping.bonesBase + 0x20 + 0x30 * uint64_t(index);
                        debugHeadLocalFinite = IsFiniteBoneValue(local);
                        debugHeadLocalNonZero = debugHeadLocalFinite && IsNonZeroBoneValue(local);
                    }
                    if (!IsFiniteBoneValue(local)) return false;
                    if (useModelWorld) {
                        point = model_world.TransformPoint({local.x, local.y, local.z});
                    } else {
                        XMFLOAT3 rotated{};
                        XMStoreFloat3(&rotated, XMVector3Transform(XMLoadFloat3(&local), rotation));
                        const float bias = HeroID == eHero::HERO_WRECKINGBALL ? -.7f : 0.f;
                        point = Vector3(rotated.x, rotated.y + bias, rotated.z) + pos;
                    }
                    return IsFiniteVectorValue(point) && point != Vector3{};
                }
                return false;
            };
            for (size_t slot = 0; slot < skeleton_bones.size(); ++slot)
                if (requested[slot]) skeleton_bone_valid[slot] = world(mapping.mappedIndices[slot], skeleton_bones[slot]);
            if (botRequested && mapping.hasBotChest)
                cached_bot_chest_bone_valid = world(mapping.botChestMappedIndex, cached_bot_chest_bone);
            debugHeadLookupAttempted = true;
            debugHeadLookupResolved = mapping.valid;
            debugHeadBoneData = mapping.boneData;
            debugHeadBonesBase = mapping.bonesBase;
            debugHeadBonePtr = mapping.bonePtr;
            debugHeadBoneIdTable = mapping.boneIdTable;
            debugHeadBoneCount = mapping.boneCount;
            debugHeadMappedIndex = mapping.mappedIndices[0];
            debugHeadIdFound = debugHeadMappedIndex >= 0;
            debugHeadWorld = skeleton_bones[0];
            debugHeadWorldNonZero = skeleton_bone_valid[0];
            for (size_t slot = 0; slot < requested.size(); ++slot)
                if (requested[slot] && mapping.requestedBoneIds[slot] >= 0 && !skeleton_bone_valid[slot])
                    skeleton_status = Skeleton::ReadStatus::PartialPose;
            ApplySkeletonCoreAnchors();
            return true;
        }

        void CacheSkeletonBonesCached(SkeletonBoneCache* cache, uint64_t knownVelocityBoneData,
                                      bool allowSlowFallback = true, const Skeleton::ReadSource* supplied = nullptr) {
            // Keep the legacy argument for callers. Failed observations cannot expand
            // into unconfirmed per-bone reads with a different identity.
            (void)allowSlowFallback;
            ClearSkeletonObservation(true);
            SkeletonBoneCache temporary{};
            auto& mapping = cache ? *cache : temporary;
            if (!HasBoneWorldTransform()) {
                mapping.valid = false;
                mapping.status = skeleton_status = Skeleton::ReadStatus::InvalidTransform;
                return;
            }
            if (!ConfirmSkeletonLayout(mapping, knownVelocityBoneData, supplied)) {
                mapping.valid = false;
                skeleton_status = mapping.status;
                return;
            }
            auto source = supplied ? *supplied : ProductionSkeletonSource();
            source.metrics = &mapping.metrics;
            constexpr size_t stride = 0x30;
            const size_t bytes = stride * size_t(mapping.maxMappedIndex) + sizeof(XMFLOAT3);
            std::array<uint8_t, stride * kMaxBoneIdCount> poses{};
            if (!source.Exact(mapping.bonesBase + 0x20, poses.data(), bytes)) {
                // The complete mapping was just observed. A dynamic-pose
                // failure does not require another full ID-table discovery;
                // the next hot frame must still pass both layout guards.
                mapping.status = skeleton_status = Skeleton::ReadStatus::PoseReadFailed;
                return;
            }
            const Skeleton::PoseRange range{0, mapping.maxMappedIndex, 0, bytes, uint32_t(bytes)};
            std::array<bool, 18> requested{};
            requested.fill(true);
            CommitSkeletonPoses(mapping, std::span(&range, 1), poses, requested, mapping.hasBotChest, supplied);
        }

        // =====================================================================
        // 3D box corners
        // =====================================================================
        void Get3DBoxPos(Vector3& veca, Vector3& vecb, Vector3& vecc, Vector3& vecd,
                         Vector3& vece, Vector3& vecf, Vector3& vecg, Vector3& vech) {
            __try {
                if (this->pos != Vector3(0, 0, 0) && this->PlayerHealth > 0) {
                    uint64_t bonesBase = 0;
                    uint64_t pBoneData = ResolveBoneData(bonesBase);
                    if (pBoneData) {
                        if (bonesBase) {
                            const int boneIdx17 = get_bone_id(pBoneData, 17);
                            if (boneIdx17 < 0)
                                return;
                            XMFLOAT3 currentBone = SDK->RPM<XMFLOAT3>(
                                bonesBase + (0x30 * boneIdx17) + 0x20
                            );
                            currentBone.y += 0.3f;
                            XMFLOAT3 a = {currentBone.x - 0.5f, currentBone.y, currentBone.z - 0.5f};
                            XMFLOAT3 b = {currentBone.x - 0.5f, currentBone.y, currentBone.z + 0.5f};
                            XMFLOAT3 c = {currentBone.x + 0.5f, currentBone.y, currentBone.z - 0.5f};
                            XMFLOAT3 d = {currentBone.x + 0.5f, currentBone.y, currentBone.z + 0.5f};
                            currentBone.y -= 1.5f;
                            XMFLOAT3 e = {currentBone.x - 0.5f, currentBone.y, currentBone.z - 0.5f};
                            XMFLOAT3 f = {currentBone.x - 0.5f, currentBone.y, currentBone.z + 0.5f};
                            XMFLOAT3 g = {currentBone.x + 0.5f, currentBone.y, currentBone.z - 0.5f};
                            XMFLOAT3 h = {currentBone.x + 0.5f, currentBone.y, currentBone.z + 0.5f};

                            TransformBoneWorld(a, veca); TransformBoneWorld(b, vecb);
                            TransformBoneWorld(c, vecc); TransformBoneWorld(d, vecd);
                            TransformBoneWorld(e, vece); TransformBoneWorld(f, vecf);
                            TransformBoneWorld(g, vecg); TransformBoneWorld(h, vech);
                        }
                    }
                }
            } __except (EXCEPTION_EXECUTE_HANDLER) {}
        }

        // =====================================================================
        // Eye ray (head-sight direction)
        // =====================================================================
        void GetEyeRayPoint(Vector3& veceye, Vector3& arrow1, Vector3& arrow2,
                            Vector3& arrow3, Vector3& arrow4) {
            __try {
                if (this->pos != Vector3(0, 0, 0) && this->PlayerHealth > 0) {
                    uint64_t bonesBase = 0;
                    uint64_t pBoneData = ResolveBoneData(bonesBase);
                    if (pBoneData) {
                        if (bonesBase) {
                            const int boneIdx17 = get_bone_id(pBoneData, 17);
                            if (boneIdx17 < 0)
                                return;
                            XMFLOAT3 b = SDK->RPM<XMFLOAT3>(
                                bonesBase + (0x30 * boneIdx17) + 0x20
                            );
                            b = XMFLOAT3(b.x, b.y, b.z + 0.8f);
                            XMFLOAT3 b1 = {b.x + 0.2f, b.y,     b.z - 0.3f};
                            XMFLOAT3 b2 = {b.x - 0.2f, b.y,     b.z - 0.3f};
                            XMFLOAT3 b3 = {b.x,       b.y + 0.2f, b.z - 0.3f};
                            XMFLOAT3 b4 = {b.x,       b.y - 0.2f, b.z - 0.3f};

                            // Legacy eye rays never used the skeleton Ball bias.
                            TransformBoneWorld(b, veceye, false);
                            TransformBoneWorld(b1, arrow1, false);
                            TransformBoneWorld(b2, arrow2, false);
                            TransformBoneWorld(b3, arrow3, false);
                            TransformBoneWorld(b4, arrow4, false);
                        }
                    }
                }
            } __except (EXCEPTION_EXECUTE_HANDLER) {}
        }

        // =====================================================================
        // ESP bone list (projects to screen)
        // =====================================================================
        espBone getBoneList(std::array<int, 18> index) {
            __try {
                espBone a{};
                Vector2 pmin{}, pmax{}, w2s{};
                Vector3 root = this->pos;

                bool isBot = GameData::IsTrainingBotHeroId(HeroID);

                // For W2S we need the global viewMatrix, WX, WY.
                // These are accessed via the global variables declared in Overwatch.hpp.
                // We rely on the caller to have set up the extern viewMatrix and WX/WY.
                extern Matrix viewMatrix;
                extern float WX, WY;
                extern std::mutex g_viewMatrixMutex;
                Matrix view{};
                g_viewMatrixMutex.lock();
                view = viewMatrix;
                g_viewMatrixMutex.unlock();

                if (isBot) {
                    if (!view.WorldToScreen(root, &w2s, Vector2(WX, WY))) {
                        a.boneerror = true;
                        return a;
                    }
                    pmin = pmax = w2s;
                    int botIndices[] = { 17, 16, 3, 13, 54 };
                    for (int i = 0; i < 5; i++) {
                        Vector3 bone = GetBonePos(botIndices[i]);
                        Vector2 ws{};
                        if (view.WorldToScreen(bone, &ws, Vector2(WX, WY))) {
                            if (i == 0) { a.head = ws; a.head.Y += 4.f; }
                            else if (i == 1) a.neck = ws;
                            else if (i == 2) a.body_1 = ws;
                            else if (i == 3) a.l_1 = ws;
                            else if (i == 4) a.r_1 = ws;
                            if (ws.X < pmin.X) pmin.X = ws.X;
                            if (ws.Y < pmin.Y) pmin.Y = ws.Y;
                            if (ws.X > pmax.X) pmax.X = ws.X;
                            if (ws.Y > pmax.Y) pmax.Y = ws.Y;
                        }
                    }
                } else {
                    if (!view.WorldToScreen(root, &w2s, Vector2(WX, WY))) {
                        a.boneerror = true;
                        return a;
                    }
                    pmin = pmax = w2s;
                    for (int i = 0; i < 18; i++) {
                        Vector3 bone = GetBonePos(index[i]);
                        Vector2 ws{};
                        if (view.WorldToScreen(bone, &ws, Vector2(WX, WY))) {
                            if      (i == 0)  a.head = ws;
                            else if (i == 1)  a.neck = ws;
                            else if (i == 2)  a.body_1 = ws;
                            else if (i == 3)  a.body_2 = ws;
                            else if (i == 4)  a.l_1 = ws;
                            else if (i == 5)  a.r_1 = ws;
                            else if (i == 6)  a.l_d_1 = ws;
                            else if (i == 7)  a.r_d_1 = ws;
                            else if (i == 8)  a.l_a_1 = ws;
                            else if (i == 9)  a.r_a_1 = ws;
                            else if (i == 10) a.l_a_2 = ws;
                            else if (i == 11) a.r_a_2 = ws;
                            else if (i == 12) a.l_d_2 = ws;
                            else if (i == 13) a.r_d_2 = ws;
                            else if (i == 14) a.sex = ws;
                            else if (i == 15) a.sex1 = ws;
                            else if (i == 16) a.sex2 = ws;
                            else if (i == 17) a.sex3 = ws;
                            if (ws.X < pmin.X) pmin.X = ws.X;
                            if (ws.Y < pmin.Y) pmin.Y = ws.Y;
                            if (ws.X > pmax.X) pmax.X = ws.X;
                            if (ws.Y > pmax.Y) pmax.Y = ws.Y;
                        }
                    }
                }

                a.upL   = pmin;
                a.upR   = Vector2(pmax.X, pmin.Y);
                a.downL = Vector2(pmin.X, pmax.Y);
                a.downR = pmax;
                return a;
            } __except (EXCEPTION_EXECUTE_HANDLER) {
                return espBone{};
            }
        }
    };

} // namespace OW
