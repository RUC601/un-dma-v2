#pragma once

#include <atomic>
#include <cstdint>
#include "Game/Cn154905.hpp"

// =============================================================================
// Overwatch Offsets - BZ 2.23.11.151722 / CN 2.25.1.0.154905N
//
// BZ evidence remains 03_DOWNP/2.23.11.151722/bz.
// CN dump: 03_DOWNP/2.25.1.0.154905N/Overwatch.exe_x64_memdump.exe
// CN IDA/Diaphora and read-only proof: same version's cn/offset_work_2026-11-04.
// CN entity/state/lock, view/admin, key-byte and input RVAs moved; effective
// component/parent/singleton/visibility transforms and payload layouts retained.
// Camera/primary projection processing is unchanged; physical/visual/endurance
// acceptance is separate from the bounded read-only evidence.
// =============================================================================

namespace OW {
    namespace offset {

        enum class RuntimeProfile {
            WorldBz,
            Cn,
            Bz154905,
            Kr152688,
        };

        // Initialization baseline for consumers outside an attached session.
        inline constexpr RuntimeProfile kProductRuntimeProfile = RuntimeProfile::Cn;

        enum class ComponentTransformMode {
            World0527,
            World150818,
            World151177,
            World151293,
            World151722,
            Identity,
            Cn154905,
            Bz154905,
        };

        enum class SingletonListMode {
            EncryptedWorldBz,
            EncryptedWorldBz151293,
            EncryptedWorldBz151722,
            LiveGameAdminWorldBz,
            Plain,
            Cn154905,
            Bz154905,
        };

        enum class ViewMatrixMode {
            EncryptedChain,
            EncryptedChainSubXorSub,
            EncryptedChainXorAdd,
            DirectChain,
            EncryptedDirectMatrix,
        };

        // The chain is selected by the version profile. Runtime must never
        // switch between these layouts based on an environment variable or a
        // failed read; a new layout is promoted only after static and live
        // evidence agree.
        enum class ViewMatrixChainMode {
            LegacyLong,
            CameraProjectionShort,
        };

        struct RuntimeOffsetProfile {
            const char* name = "world/bz";
            ComponentTransformMode componentTransform = ComponentTransformMode::World0527;
            uint32_t typeTeam = 0;
            uint32_t typePlayerController = 0;
            uint32_t typeRotation = 0;
            uint32_t typeLink = 0;
            uint32_t typeHealth = 0;
            uint32_t typeHeroId = 0;

            uint64_t Address_entity_base = 0;
            uint64_t InputMouseScaleX_RVA = 0;
            uint64_t InputMouseScaleY_RVA = 0;
            uint64_t ViewportWidth_RVA = 0;
            uint64_t ViewportHeight_RVA = 0;
            uint64_t VisibilityValueOffset = 0;

            uint64_t Address_viewmatrix_base = 0;
            uint64_t offset_viewmatrix_xor_key = 0;
            uint64_t offset_viewmatrix_xor_key2 = 0;
            uint64_t offset_viewmatrix_xor_key3 = 0;
            uint64_t VM_P1 = 0;
            uint64_t VM_P2 = 0;
            uint64_t VM_ViewProjectionParent = 0;
            uint64_t VM_ViewProjectionPtr = 0;
            uint64_t VM_ViewProjectionMatrix = 0;
            uint64_t VM_ViewMatrix = 0;
            uint64_t VM_ProjMatrix = 0;

            uint64_t GlobalAdmin_RVA = 0;
            SingletonListMode singletonListMode = SingletonListMode::EncryptedWorldBz;
            ViewMatrixMode viewMatrixMode = ViewMatrixMode::EncryptedChain;
            uint64_t VM_DirectMatrix = 0;
            ViewMatrixChainMode viewMatrixChain = ViewMatrixChainMode::LegacyLong;
            bool projectionFromRender = false;
            uint32_t typeSkill = 0x37;
            uint64_t componentKeyRootRva = 0;
            uint64_t componentKeyByteRva = 0;
            uint64_t componentKeyOffset = 0;
            uint64_t clientGameRva = 0;
            bool capacityBoundRegistry = false;
            bool pooledHealth = false;
            uint64_t visibilityClearMask = 0;
        };

        // =========================================================================
        // STATIC-VERIFIED (2026-05-27): IDA xrefs + forum p330/p331
        // =========================================================================

        // Key system
        static constexpr auto KeyGlobalStore_RVA = 0x40405F8; // diagnostic only
        static constexpr auto KeyGlobal_XOR    = 0xF5;        // diagnostic only
        static constexpr auto GetGlobalKey_RVA = 0x581D20;    // diagnostic legacy helper

        // BZ 151722 component transform owner is statically mapped, but its
        // historical builder-style diagnostic RVA has not been re-established.
        // OLD (0521): static constexpr auto DecryptComponent_RVA = 0x563700;
        static constexpr auto DecryptComponent_RVA = 0x0; // diagnostic only; unresolved for BZ 151722
        static constexpr auto DecryptTable_Mask    = 0x7FF;    // 2048 entries

        // Component pointer resolver (forum p331 NormPlayer, IDA tail at RVA 0x5637D2).
        // OLD (0521): static constexpr auto ComponentXorQword_RVA = 0x3A86E30;
        static constexpr auto ComponentXorQword_RVA = 0x3A8A578; // IDA + live reverified 2026-08-04: BZ 151722 key source root
        // OLD (0521): static constexpr auto ComponentXorQword_Off = 0x10C;
        static constexpr auto ComponentXorQword_Off = 0x186;     // IDA + live verified: [keySource+0x186]
        // OLD (0521): static constexpr auto ComponentXorByte_RVA = 0x3772769;
        static constexpr auto ComponentXorByte_RVA  = 0x3770195; // IDA + live reverified: BZ 151722 component tail byte
        static constexpr auto ComponentPreXorByte_RVA = 0x3800A7D; // helper/list byte in sub_7FF6444DCA40; not active component tail
        // OLD (0521): Component_Add1=0x4C8675CDE55BA1B2, Component_Add2=0x7BE57670994040F6
        // OLD (0521): Component_Xor1=0x3864150DB528414C, Component_Xor2=0xA4764E53CD34159B, Component_Ror=3
        static constexpr auto Component_Xor1        = 0xDC01B58B9BDFFB4B; // verified 0527: fixed xor
        static constexpr auto Component_Add1        = 0x024620C984E36588; // verified 0527: add after ROR32
        static constexpr auto Component_Sub1        = 0x7D957CD64821F39B; // verified 0527: subtract before final rotates
        static constexpr auto Component_Ror1        = 32;                 // verified 0527
        static constexpr auto Component_Ror2        = 60;                 // verified 0527
        static constexpr auto Component_Ror3        = 57;                 // verified 0527 helper call
        static constexpr auto Component150818_Add1  = 0x4C8675CDE55BA1B2;
        static constexpr auto Component150818_Add2  = 0x7BE57670994040F6;
        static constexpr auto Component150818_Xor1  = 0x3864150DB528414C;
        static constexpr auto Component150818_Xor2  = 0xA4764E53CD34159B;
        static constexpr auto Component150818_Ror   = 3;
        static constexpr auto Component151177_Add1  = 0x0BA96615BF373CE1;
        static constexpr auto Component151177_Xor1  = 0x71B2AC20DD7FA037;
        static constexpr auto Component151177_Xor2  = 0x9A2687D39D0D255A;
        static constexpr auto Component151177_Ror1  = 9;
        static constexpr auto Component151177_Ror2  = 47;
        // BZ 151293 returned to the add/xor/add/xor/xor/ROR3 transform shape.
        static constexpr auto Component151293_Add1  = Component150818_Add1;
        static constexpr auto Component151293_Add2  = Component150818_Add2;
        static constexpr auto Component151293_Xor1  = Component150818_Xor1;
        static constexpr auto Component151293_Xor2  = Component150818_Xor2;
        static constexpr auto Component151293_Ror   = Component150818_Ror;
        static constexpr auto Component151722_Sub1  = 0x55CDD22AED39B79C;
        static constexpr auto Component151722_Xor1  = 0xBDDFD6824A7577A0;
        static constexpr auto Component151722_Xor2  = 0x525D8E9BA11DDBFD;
        static constexpr auto Component151722_Add1  = 0xB41B65702C34F776;
        static constexpr auto Component151722_Ror1  = 11;
        static constexpr auto Component151722_Ror2  = 25;
        static constexpr auto Component151722_Ror3  = 3;
        static constexpr auto Component151722_Ror4  = 13;

        // Wrapper/helper path from plexies p332. This is not the main
        // DecryptComponent tail above: it uses a helper XOR/ROR stage plus a
        // different byte/key-field pair. IDA at RVA 0x58EF91 references the same
        // qword slot as ComponentXorQword_RVA; plexies' 0x4092E70 entry is not a
        // referenced slot in the 0527 IDA database.
        static constexpr auto ComponentWrapperHelper_XorKey = 0x3450DD4E165D1B80;
        static constexpr auto ComponentWrapperHelper_Ror    = 29;
        static constexpr auto ComponentWrapperByte_RVA      = 0x377E422;
        static constexpr auto ComponentWrapperQword_RVA     = ComponentXorQword_RVA;
        static constexpr auto ComponentWrapperQword_Off     = 0x45;
        static constexpr auto ComponentWrapper_SubK         = 0x5E1EE52880C2AB5C;
        static constexpr auto ComponentWrapper_XorK         = 0x0C42F747C128EF56;
        static constexpr auto ComponentWrapper_AddK         = 0x476DC63CD7A9AA1C;
        static constexpr auto ComponentWrapper_Ror1         = 12;
        static constexpr auto ComponentWrapper_Ror2         = 19;

        // Visibility/data flag resolver (forum p331 full disassembly, IDA RVA 0x58C880).
        // Legacy BZ 150818 encrypted-visibility formula. Active BZ/CN 151752
        // profiles both read +0x98, but only CN exposes a raw bool there.
        // OLD (0521): static constexpr auto VisibilityGlobalKeyPtr_RVA = 0x3B76970;
        static constexpr auto VisibilityGlobalKeyPtr_RVA = ComponentXorQword_RVA; // live BZ 150818 shared key source
        // OLD (0521): static constexpr auto VisibilityQwordOffset = 0x1B1;
        static constexpr auto VisibilityQwordOffset      = 0x6A;      // live BZ 150818 sujung formula
        // OLD (0521): static constexpr auto VisibilityValueOffset = 0x98;
        static constexpr auto VisibilityValueOffset      = 0x98;     // active BZ/CN visibility payload
        static constexpr auto VisibilityBzVisibleMask    = 0x80000000ull; // BZ 151722: set means visible
        static constexpr auto VisibilityMagicByte_RVA    = 0x3746659; // live BZ 150818 sujung formula
        static constexpr auto Visibility_Add1            = 0x5CE60F50EA1D337F;
        static constexpr auto Visibility_Ror1            = 3;
        static constexpr auto Visibility_Add2            = 0x78D75198F1D34D38;
        static constexpr auto Visibility_Ror2            = 12;
        static constexpr auto Visibility_Rol1            = 28;        // verified 0527
        static constexpr auto Visibility_Xor1            = 0x53DB07B6B873760C;
        static constexpr auto Visibility_Sub1            = 0x7A7DB4DE6CD03BBC;
        static constexpr auto Visibility_FinalRor        = 26;        // verified 0527

        // New decryption table base (24-byte entry format, not simple qword array).
        static constexpr auto DecryptTable_New = 0x3800000;

        // Entity system.
        // OLD (0521): static constexpr auto Address_entity_base = 0x39298C8;
        static constexpr auto Address_entity_base = 0x392BF58; // IDA, dumper and live reverified: BZ 151722 entity list root
        static constexpr auto EntityList_SlotCount_RVA = 0x392BF40; // BZ 151722 entity state/version qword
        static constexpr auto entity_entry_stride = 0x10;      // entity list slot: { entity, pad }
        static constexpr auto Entity_MatchId      = 0x138;     // u32 on component parent; retained from 0521
        static constexpr auto Entity_PoolPtr      = 0x30;      // masked with 0xFFFFFFFFFFFFFFC0
        static constexpr auto Pool_PoolId         = 0x10;
        static constexpr auto Link_TargetId       = 0xD0;      // u32 on CN link component for target-map lookup
        static constexpr auto Link_UniqueId       = 0xD4;      // u32 on decrypted link component
        static constexpr auto Link_UniqueIdAlt    = 0xD8;

        // p344 verified/carry-forward structural constants. Keep these named even
        // when a runtime path still uses older local helpers.
        static constexpr auto Address_LiveFov_RVA = 0x4037628; // unresolved for BZ 150818; not used by runtime path
        static constexpr auto Default_FOV_Horizontal_Deg = 103.0f;
        static constexpr auto kOpenProcessFlags = 0x438;

        static constexpr auto Transform_PositionOffset = 0x3D0;
        static constexpr auto ReplicatedTransform_PositionOffset = 0x180;
        static constexpr auto Velocity_PositionOffset = 0x200;
        static constexpr auto Velocity_LocationOffset = 0x200;
        static constexpr auto Velocity_VelocityOffset = 0x050;
        static constexpr auto Velocity_LocationYBias = 1.0f;
        static constexpr auto Velocity_BoneDataPtrOffset = 0x8B0;
        static constexpr auto BoneData_BonesArrayOffset = 0x20;
        static constexpr auto BoneEntryStride = 0x30;
        static constexpr auto Bone_PosOffset = 0x20;
        static constexpr auto Actor_TeamOffsetFromHeroId = 0x180;
        static constexpr auto Actor_PositionOffsetFromHeroId = 0x1C;
        static constexpr auto HeroId_HighWord = 0x02E0;
        static constexpr auto RotationBase_Sub1 = 0x888;
        static constexpr auto RotationBase_Sub2 = 0x8EC;
        static constexpr auto Vis_DataOffsetInComp = 0x98;
        static constexpr auto Vis_OccludedBitMask = 0x800;
        static constexpr auto Team_FlagsOffset = 0x58;
        static constexpr auto Team_LegacyMask = 0x0F800000;
        static constexpr auto Team_CnComparisonMask = 0x00FFFFFF;
        static constexpr auto Team_CnRelationMask = 0x000000FF;
        static constexpr auto Team1_Mask = 0x00800000;
        static constexpr auto Team2_Mask = 0x01000000;

        static constexpr auto Outline_ByteRva = 0x37528E4;
        static constexpr auto Outline_QwordBaseRva = ComponentXorQword_RVA;
        static constexpr auto Outline_QwordOffset = 0x97;
        static constexpr auto Outline_Rol1Amount = 23;
        static constexpr auto Outline_XorK1 = 0xEF781B6466FAB59B;
        static constexpr auto Outline_SubK2 = 0x2240EA534C11100D;
        static constexpr auto Outline_AddK3 = 0x605EC85DF1D6897D;
        static constexpr auto Outline_AddK4 = 0x1F21DE5151741226;

        static constexpr auto Healthpack_Small = 0x40000000000005F;
        static constexpr auto Healthpack_Large = 0x400000000000060;
        static constexpr auto Healthpack_Mega = 0x40000000000480A;

        // Current game sensitivity.
        // Static 150818: +0x2224/+0x2228 are per-frame input deltas scaled by
        // input.MouseScaleX/Y. The durable setting is singleton[0x6] + 0x2238,
        // stored as normalized UI sensitivity (4.5 -> 0.045).
        static constexpr auto Sensitivity_EncryptedPtr_RVA = 0x0; // direct pointer unresolved for BZ 151177
        static constexpr auto GlobalAdmin_WorldBz_RVA = 0x3A903F0; // IDA + live reverified BZ 151722 direct admin root
        static constexpr auto SensitivityAdmin_WorldBz_RVA = GlobalAdmin_WorldBz_RVA; // admin +0x160 live-decode -> slot[6]
        static constexpr auto GlobalAdmin_Ne_2_22_1_1_150434_RVA = 0x43F1BF0;
        static constexpr auto GlobalAdmin_Ne_2_23_0_0_150818_RVA = 0x43C97F0;
        static constexpr auto GlobalAdmin_Ne_2_23_0_3_151177_RVA = 0x43CB8B0;
        static constexpr auto GlobalAdmin_Ne_2_23_1_0_151293_RVA = 0x43C70C0;
        static constexpr auto GlobalAdmin_Ne_2_23_1_0_151752_RVA = 0x43C70C0;
        static constexpr auto GlobalAdmin_Ne_2_23_11_151722_RVA = 0x43CB300;
        static constexpr auto GlobalAdmin_Ne_2_24_0_0_152690_RVA = 0x4447970;
        static constexpr auto GlobalAdmin_Ne_2_24_1_0_153480_RVA = 0x446C6F0;
        static constexpr auto GlobalAdmin_RVA = GlobalAdmin_WorldBz_RVA;
        static constexpr auto Singleton_K1_xor = 0x5E3E2720BC53F290;
        static constexpr auto Singleton_K2_sub = 0x7A8EE1534F59D103;
        static constexpr auto Singleton_K3_add = 0xEFFE7E69F8213D2E;
        static constexpr auto Singleton_Ror = 3;
        static constexpr auto Singleton_InputOffset = 0x160;
        static constexpr auto LiveGameAdmin_InputOffset = 0x30;
        static constexpr auto LiveGameAdmin_Add1 = 0x78B568A5D3C8EF76;
        static constexpr auto LiveGameAdmin_Xor1 = 0x8B846BECDFD77B79;
        static constexpr auto LiveGameAdmin_Add2 = 0x73978469CB862683;
        static constexpr auto LiveGameAdmin_Ror = 48;
        static constexpr auto Singleton151722_Ror1 = 36;
        static constexpr auto Singleton151722_Sub1 = 0x6B044D449593AB9B;
        static constexpr auto Singleton151722_Xor1 = 0xD606A0DFAFD1DCFA;
        static constexpr auto Singleton151722_Ror2 = 47;
        static constexpr auto LiveGameAdmin_RejectedSensitivityHit1 = 0x1D09C; // transient float hit; not wired
        static constexpr auto LiveGameAdmin_RejectedSensitivityHit2 = 0x23F94; // input state +0x2224 in static ECS path
        static constexpr auto LiveGameAdmin_RejectedSensitivityHit3 = 0x23F98; // input state +0x2228 in static ECS path
        static constexpr auto LiveGameAdmin_RejectedSensitivityHit4 = 0xB5E4; // transient float hit; not wired
        static constexpr auto SensitivitySingletonIndex = 0x6;
        static constexpr auto Sensitivity = 0x2238;
        static constexpr auto SensX_Scale = 0x2224;
        static constexpr auto SensY_Scale = 0x2228;
        static constexpr auto Sensitivity_NormalizedToUserScale = 100.0f;
        static constexpr auto Invert_Y_Flag = 0x2156;

        // ViewMatrix - BZ 2.23.11.151722
        //   Chain root:
        //   0x38D03C0 -> (raw ^ 0x35614A18A98F5D88) +
        //                 0x5A62179C65E68926.
        //   Then +0x20 -> +0x48 -> +0x6D0 -> +0x8 -> render VP +0xC0.
        // OLD (0521): static constexpr auto Address_viewmatrix_base = 0x38D0220;
        static constexpr auto Address_viewmatrix_chain_base = 0x38D03C0; // IDA + live reverified BZ 151722 chain root
        static constexpr auto Address_viewmatrix_primary_base = 0x3960A58; // historical BZ 151177 diagnostic only
        static constexpr auto Address_viewmatrix_direct_base = 0x3960A70; // historical BZ 151177 diagnostic only
        static constexpr auto Address_viewmatrix_base = Address_viewmatrix_chain_base;
        // OLD (0521): key1=0x59D406B75C2A4377, key2=0xD54D81BA4EED36CE, key3=0x1C840F09D6923D76
        static constexpr auto offset_viewmatrix_xor_key  = 0x35614A18A98F5D88;
        static constexpr auto offset_viewmatrix_xor_key2 = 0x5A62179C65E68926;
        static constexpr auto offset_viewmatrix_xor_key3 = 0x0;
        static constexpr auto VM_P1          = 0x20;
        static constexpr auto VM_P2          = 0x48;
        static constexpr auto VM_ViewProjectionParent = 0x6D0;
        static constexpr auto VM_ViewProjectionPtr    = 0x8;
        static constexpr auto VM_ViewProjectionMatrix = 0xC0;
        static constexpr auto VM_ViewMatrix  = 0x140;
        static constexpr auto VM_ProjMatrix  = 0xB0;
        static constexpr auto Bz150818_DirectViewProjectionMatrix = 0x1C0;
        static constexpr auto Bz151177_DirectViewProjectionMatrix = 0xF70; // legacy dynamic-context seed; unused by BZ 151177 chain mode

        // Viewport dimensions (2026-05-27, dump_SCY.exe).
        // IDA: FullScreenWidth/FullScreenHeight option objects at
        // 0x7FF7C1137C00 / 0x7FF7C1137C70; current uint32 value is at +0x38.
        // sub_7FF7BEBA3330 returns these as the resolved output width/height.
        static constexpr auto ViewportWidth_RVA  = 0x0;
        static constexpr auto ViewportHeight_RVA = 0x0;

        // Input globals.
        static constexpr auto InputMouseScaleX_RVA      = 0x3776BDC; // IDA + live reverified BZ 151722 input.MouseScaleX
        static constexpr auto InputMouseScaleY_RVA      = 0x3776BF4; // IDA + live reverified BZ 151722 input.MouseScaleY

        static constexpr auto changefov = 0x402B658; // current candidate; not revalidated in 0527 pass

        inline constexpr RuntimeOffsetProfile kWorldBzRuntimeProfile{
            "world/bz 151722",
            ComponentTransformMode::World151722,
            0x20, // live semantic proof: Ana local 0x01000000, 6 enemies 0x00800000
            0x42, // live semantic proof: unique Ana +0x1260 unit vector
            0x2E, // rotation payload; 0x2F is now a compact wrapper
            0x33, // link payload; 0x34 is now a compact wrapper
            0x3A, // health payload; 0x3B is now a compact wrapper
            0x53, // hero payload; +0xD0 hero id retained
            Address_entity_base,
            InputMouseScaleX_RVA,
            InputMouseScaleY_RVA,
            ViewportWidth_RVA,
            ViewportHeight_RVA,
            VisibilityValueOffset,
            Address_viewmatrix_base,
            offset_viewmatrix_xor_key,
            offset_viewmatrix_xor_key2,
            offset_viewmatrix_xor_key3,
            VM_P1,
            VM_P2,
            VM_ViewProjectionParent,
            VM_ViewProjectionPtr,
            VM_ViewProjectionMatrix,
            VM_ViewMatrix,
            VM_ProjMatrix,
            GlobalAdmin_WorldBz_RVA,
            SingletonListMode::EncryptedWorldBz151722,
            ViewMatrixMode::EncryptedChainXorAdd,
            Bz151177_DirectViewProjectionMatrix,
            ViewMatrixChainMode::LegacyLong,
        };

        inline constexpr RuntimeOffsetProfile kCnRuntimeProfile{
            .name = "CN 154905N",
            .componentTransform = ComponentTransformMode::Cn154905,
            .typeTeam = 0x20,
            .typePlayerController = 0x42,
            .typeRotation = 0x2E,
            .typeLink = 0x33,
            .typeHealth = 0x3A,
            .typeHeroId = 0x53,
            .Address_entity_base = Cn154905::entityRoot,
            .InputMouseScaleX_RVA = Cn154905::mouseScaleX,
            .InputMouseScaleY_RVA = Cn154905::mouseScaleY,
            .VisibilityValueOffset = 0x98,
            .Address_viewmatrix_base = Cn154905::viewRoot,
            .offset_viewmatrix_xor_key = Cn154905::pointerXor,
            .offset_viewmatrix_xor_key2 = Cn154905::pointerAdd,
            .VM_P1 = 0x20,
            .VM_P2 = 0x48,
            .VM_ViewProjectionParent = 0x6D0,
            .VM_ViewProjectionPtr = 0x8,
            .VM_ViewProjectionMatrix = 0xC0,
            .VM_ViewMatrix = 0x140,
            .VM_ProjMatrix = 0xB0,
            .GlobalAdmin_RVA = Cn154905::globalAdmin,
            .singletonListMode = SingletonListMode::Cn154905,
            .viewMatrixMode = ViewMatrixMode::EncryptedChainXorAdd,
            .typeSkill = 0x36,
            .pooledHealth = true,
        };

        // 516727 p394#4850032. The camera, registry and component transform
        // have read-only evidence on 154905. Input scale is still unresolved.
        inline constexpr RuntimeOffsetProfile kBz154905RuntimeProfile{
            .name = "BZ 154905",
            .componentTransform = ComponentTransformMode::Bz154905,
            .typeTeam = 0x20,
            .typePlayerController = 0x42,
            .typeRotation = 0x2E,
            .typeLink = 0x33,
            .typeHealth = 0x3A,
            .typeHeroId = 0x53,
            .Address_entity_base = 0x3BB39A8,
            .VisibilityValueOffset = 0x98,
            .Address_viewmatrix_base = 0x3B4B0E8,
            .offset_viewmatrix_xor_key = 0x4FFB48E02CB5397CULL,
            .offset_viewmatrix_xor_key2 = 0x0F26BE43E44F8A31ULL,
            .VM_P1 = 0x20,
            .VM_P2 = 0x48,
            .VM_ViewMatrix = 0x140,
            .VM_ProjMatrix = 0xB0,
            .GlobalAdmin_RVA = 0x3D1CF10,
            .singletonListMode = SingletonListMode::Bz154905,
            .viewMatrixMode = ViewMatrixMode::EncryptedChainXorAdd,
            .viewMatrixChain = ViewMatrixChainMode::CameraProjectionShort,
            .typeSkill = 0x36,
            .componentKeyRootRva = 0x3D17098,
            .componentKeyByteRva = 0x39E3AE8,
            .componentKeyOffset = 0x3A,
            .clientGameRva = 0x3D171A0,
            .capacityBoundRegistry = true,
            .pooledHealth = true,
            .visibilityClearMask = 0x200000000000ULL,
        };

        // Pure Korea/Nexon 152688 and forum p370. Shared entity IDs are
        // provisional on KR. Admin, visibility and input scale are unknown.
        inline constexpr RuntimeOffsetProfile kKr152688RuntimeProfile{
            .name = "KR 152688",
            .componentTransform = ComponentTransformMode::Identity,
            .typeTeam = 0x20,
            .typePlayerController = 0x42,
            .typeRotation = 0x2E,
            .typeLink = 0x33,
            .typeHealth = 0x3A,
            .typeHeroId = 0x53,
            .Address_entity_base = 0x35E8568,
            .Address_viewmatrix_base = 0x3CD33B0,
            .VM_P1 = 0x20,
            .VM_P2 = 0x48,
            .VM_ViewMatrix = 0x140,
            .VM_ProjMatrix = 0xB0,
            .viewMatrixMode = ViewMatrixMode::DirectChain,
            .viewMatrixChain = ViewMatrixChainMode::CameraProjectionShort,
            .typeSkill = 0x36,
            .capacityBoundRegistry = true,
        };

        constexpr const RuntimeOffsetProfile& ForProfile(RuntimeProfile profile)
        {
            switch (profile) {
            case RuntimeProfile::Cn: return kCnRuntimeProfile;
            case RuntimeProfile::Bz154905: return kBz154905RuntimeProfile;
            case RuntimeProfile::Kr152688: return kKr152688RuntimeProfile;
            case RuntimeProfile::WorldBz: return kWorldBzRuntimeProfile;
            }
            return kCnRuntimeProfile;
        }

        constexpr uint64_t DecodeCameraRoot(const RuntimeOffsetProfile& offsets, uint64_t root)
        {
            switch (offsets.viewMatrixMode) {
            case ViewMatrixMode::DirectChain: return root;
            case ViewMatrixMode::EncryptedChainXorAdd:
                return (root ^ offsets.offset_viewmatrix_xor_key) + offsets.offset_viewmatrix_xor_key2;
            case ViewMatrixMode::EncryptedChainSubXorSub:
                return ((root - offsets.offset_viewmatrix_xor_key) ^ offsets.offset_viewmatrix_xor_key2) -
                    offsets.offset_viewmatrix_xor_key3;
            default:
                return (root + offsets.offset_viewmatrix_xor_key) ^ offsets.offset_viewmatrix_xor_key2;
            }
        }

        inline std::atomic<RuntimeProfile>& ActiveProfileStorage()
        {
            static std::atomic<RuntimeProfile> profile{ kProductRuntimeProfile };
            return profile;
        }

        inline void SetActiveProfile(RuntimeProfile profile)
        {
            ActiveProfileStorage().store(profile, std::memory_order_release);
        }

        inline RuntimeProfile ActiveProfile()
        {
            return ActiveProfileStorage().load(std::memory_order_acquire);
        }

        inline const RuntimeOffsetProfile& Active()
        {
            return ForProfile(ActiveProfile());
        }

        inline const char* ActiveProfileName()
        {
            return Active().name;
        }

        inline bool IsCnProfile()
        {
            return ActiveProfile() == RuntimeProfile::Cn;
        }

        inline uint32_t TeamRawComparisonKeyFromFlags(uint32_t flags)
        {
            return IsCnProfile()
                ? (flags & Team_CnComparisonMask)
                : (flags & Team_LegacyMask);
        }

        inline uint32_t TeamRelationCodeFromFlags(uint32_t flags)
        {
            return flags & Team_CnRelationMask;
        }

        inline uint32_t TeamComparisonKeyFromFlags(uint32_t flags)
        {
            // The low byte contains relation detail, not the stable team key.
            // Live data shows same-team actors can differ there, so compare
            // only the legacy team bits for active BZ 151722 / CN 153480.
            return flags & Team_LegacyMask;
        }

        // =========================================================================
        // Legacy / unresolved offsets
        // =========================================================================

        static constexpr auto VisFN        = 0x79E722;  // broken legacy table-walk path
        static constexpr auto Vis_Key      = 0x1AAC46FF0D473EBA; // broken legacy table-walk path
        static constexpr auto DecryptTable_2 = 0x389A700;      // legacy table-walk path; not current DecryptVis

    }
}
