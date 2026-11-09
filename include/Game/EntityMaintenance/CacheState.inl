// Shared lexical step of entity_thread; requires its surrounding cycle state.
    struct ComponentBaseCache {
        uint64_t linkParent = 0;
        std::array<uint64_t,6> componentLayout{},linkLayout{};
        bool layoutsKnown = false;
        DWORD layoutCheckTick = 0;
        OW::EntityRosterPolicy::TeamConfirmation confirmedTeam{};
        DWORD teamUpdateTick = 0;
        uint64_t health = 0;
        uint64_t link = 0;
        uint64_t team = 0;
        uint64_t transform = 0;
        uint64_t velocity = 0;
        uint64_t hero = 0;
        uint64_t bone = 0;
        uint64_t rotation = 0;
        uint64_t rotationLeaf = 0;
        uint64_t skill = 0;
        uint64_t visibility = 0;
        uint64_t angle = 0;
        uint64_t enemyAngle = 0;
        uint64_t stat = 0;
        uint32_t matchId = 0;
        DWORD baseUpdateTick = 0;
        bool matchIdValid = false;
        DWORD healthUpdateTick = 0;
        bool healthValid = false;
        float playerHealth = 0.0f;
        float playerHealthMax = 0.0f;
        float minHealth = 0.0f;
        float maxHealth = 0.0f;
        float minArmorHealth = 0.0f;
        float maxArmorHealth = 0.0f;
        float minBarrierHealth = 0.0f;
        float maxBarrierHealth = 0.0f;
        bool alive = false;
        bool deathKnown = false;
        bool dead = false;
        DWORD deathUpdateTick = 0;
        bool imort = false;
        bool barrprot = false;
        bool heroValid = false;
        uint64_t heroId = 0;
        DWORD heroUpdateTick = 0;
        DWORD nameTeamUpdateTick = 0;
        bool nameTeamValid = false;
        bool isEnemy = false;
        bool vis = false;
        OW::VisibilitySample visibilitySample{};
        std::string heroName = "Unknown";
        OW::c_entity::SkeletonBoneCache skeletonCache{};
        OW::ModelShapes::Sample cachedModelShapes{};
        std::array<OW::Vector3, 18> cachedSkeletonBones{};
        std::array<bool, 18> cachedSkeletonBoneValid{};
        OW::Vector3 cachedSkeletonPos{};
        OW::Vector3 cachedBotChestBone{};
        bool cachedBotChestBoneValid = false;
        bool cachedSkeletonValid = false;
        DWORD skeletonUpdateTick = 0;
        bool retainedAcrossLinkChange = false;
        DWORD linkRetainTick = 0;
        size_t linkRetainCount = 0;
        void ApplyBases(OW::c_entity& entity) const {
            entity.HealthBase=health;entity.LinkBase=link;entity.TeamBase=team;
            entity.VelocityBase=velocity;entity.HeroBase=hero;entity.BoneBase=bone;
            entity.RotationBase=rotation;entity.SkillBase=skill;entity.VisBase=visibility;
            entity.AngleBase=angle;entity.EnemyAngleBase=enemyAngle;entity.statcombase=stat;
        }
    };
    struct BaseDecryptCycleKey {
        uint64_t parent = 0;
        uint32_t type = 0;

        bool operator==(const BaseDecryptCycleKey& other) const {
            return parent == other.parent && type == other.type;
        }
    };
    struct BaseDecryptCycleKeyHash {
        size_t operator()(const BaseDecryptCycleKey& key) const {
            uint64_t mixed = key.parent;
            mixed ^= static_cast<uint64_t>(key.type) +
                0x9E3779B97F4A7C15ull +
                (mixed << 6) +
                (mixed >> 2);
            return static_cast<size_t>(mixed ^ (mixed >> 32));
        }
    };
    struct RosterEntry : ComponentBaseCache {
        OW::EdOutline::State outlineState{};
        std::array<uint64_t, 6> outlineIdentity{};
        // Actor state must survive replacing the component-address cache in CacheRebuild.inl.
        DWORD skillUpdateTick = 0;
        bool skillValid = false;
        uint8_t skillRefreshCursor = 0;
        std::array<OW::FieldDeadline, 3> skillDeadlines{};
        DWORD localSkillUpdateTick = 0;
        bool localSkillValid = false;
        uint8_t localSkillRefreshCursor = 0;
        bool skill1act = false;
        bool skill2act = false;
        std::array<uint32_t, 2> skillActiveTick{};
        float ultimate = 0.0f;
        bool ultimateValid = false;
        DWORD displayUltimateTick = 0;
        std::array<uint64_t, 3> ultimateIdentity{};
        float skillcd1 = 0.0f;
        float skillcd2 = 0.0f;
        std::array<OW::OverlayStatus::CooldownSample, 2> displayCooldowns{};
        std::array<OW::CooldownTimers::State, 2> cooldownTimers{};
        std::array<uint64_t, 4> cooldownIdentity{};
        bool reloading = false;
        uint64_t participantKey = 0;
        DWORD participantIdentityUpdateTick = 0;
        bool participantIdentityValid = false;
        std::string participantLabel{};
        uint64_t participantController = 0;
        uint32_t participantControllerUid = 0;
        std::string_view admissionRejectReason{};
        DWORD admissionRejectTick = 0;
        OW::c_entity entity{};
        OW::c_entity lastRawObservation{};
        bool seenThisCycle = false;
        bool scanSeenThisCycle = false;
        bool hotReadRetainedThisCycle = false;
        int skippedJumpObservations = 0;
        DWORD lastScanSeenTick = 0;
        DWORD lastHotReadTick = 0;
        DWORD consecutiveScanMissCount = 0;
        DWORD respawnWatchStartedTick = 0;
        DWORD respawnLastScanRequestTick = 0;
        bool respawnFirstScanRequested = false;
    };
    // Two indexes reference the same owner record; no mirrored record store.
    std::unordered_map<uint64_t, std::shared_ptr<RosterEntry>> componentBaseCache{};
    componentBaseCache.reserve(128);

    struct DynamicEntityCache {
        uint64_t linkParent = 0;
        bool valid = false;
        uint64_t entityId = 0;
        uintptr_t meshBase = 0;
        XMFLOAT3 pos{};
        DWORD updateTick = 0, lastAttemptTick = 0;
    };
    std::unordered_map<uint64_t, DynamicEntityCache> dynamicEntityCache{};
    dynamicEntityCache.reserve(64);

