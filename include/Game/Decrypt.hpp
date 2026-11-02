#pragma once
#include "game/entities/RegistryResolver.hpp"
#include "memory/PeSections.hpp"
#include "Utils/RegionEnvironment.hpp"
#include <Windows.h>
#include <coroutine>
#include <exception>
#include <intrin.h>
#include <intsafe.h>
#include <cstdint>
#include <string>
#include <vector>
#include <utility>
#include <type_traits>
#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstring>
#include <cstdio>
#include <unordered_map>
#include <unordered_set>
#include <emmintrin.h>

#ifndef HIDWORD
#define HIDWORD(_ui64) ((DWORD)(((DWORDLONG)(_ui64) >> 32) & 0xFFFFFFFF))
#endif
#ifndef LODWORD
#define LODWORD(_ui64) ((DWORD)((_ui64) & 0xFFFFFFFF))
#endif
#ifndef __ROL4__
#define __ROL4__(x, n) _rotl(x, n)
#endif

#include "Game/Structs.hpp"
#include "Game/Offsets.hpp"
#include "Game/SDK.hpp"
#include "Game/RuntimeState.hpp"
#include "Game/EntityRosterPolicy.hpp"
#include "Utils/ProcessConnection.hpp"
#include "Utils/Diagnostics.hpp"
#include "Utils/RuntimeTrace.hpp"

namespace OW {

    static inline uint64_t ROR64(uint64_t x, int bits) {
        bits &= 63;
        if (bits == 0) return x;
        return (x >> bits) | (x << (64 - bits));
    }

    static inline uint64_t ROL64(uint64_t x, int bits) {
        bits &= 63;
        if (bits == 0) return x;
        return (x << bits) | (x >> (64 - bits));
    }

    inline bool GetGlobalKey() {
        Diagnostics::SetKeyStatus(Diagnostics::KeyStatus::Resolving);
        Diagnostics::Info("GetGlobalKey resolution started.");

        auto markResolved = [](const char* method) {
            Diagnostics::SetKeyStatus(
                Diagnostics::KeyStatus::Resolved,
                SDK->GlobalKey1,
                SDK->GlobalKey2);
            Diagnostics::Info("GetGlobalKey succeeded via %s: key1=0x%llX key2=0x%llX",
                method,
                static_cast<unsigned long long>(SDK->GlobalKey1),
                static_cast<unsigned long long>(SDK->GlobalKey2));
            return true;
        };

        static const uint8_t key_sig[] =
            "\x00\x00\x00\x00\x21\x00\x00\x00\x00\x00\x00\x00\x24\x00\x00\x00"
            "\x01\x00\x00\x00\x29\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00"
            "\x00\x00\x00\x00\x00\x00";
        static const char* key_mask = "xxxxxxxxxxxxx?xxxxxxxxxxxxxxxxxxxxxxxx";

        int pattern_attempts = 0;
        while (RuntimeWorkersRunning() && ProcessConnection::IsConnected()) {
            if (pattern_attempts < 3) {
                uint64_t Key = SDK->FindPatternExReg(
                    reinterpret_cast<const uint8_t*>(key_sig),
                    key_mask,
                    0x100000
                );
                if (Key && Key < 0xF000000000000000 &&
                    SDK->RPM<uint64_t>(Key + 0x38) > 0x1000000000000000 &&
                    SDK->RPM<uint64_t>(Key + 0xB8) > 0x1000000000000000) {
                    SDK->GlobalKey1 = SDK->RPM<uint64_t>(Key + 0x38);
                    SDK->GlobalKey2 = SDK->RPM<uint64_t>(Key + 0xB8);
                    printf("[Decrypt] GlobalKey1: 0x%llX\n", SDK->GlobalKey1.load());
                    printf("[Decrypt] GlobalKey2: 0x%llX\n", SDK->GlobalKey2.load());
                    return markResolved("pattern scan");
                }
                pattern_attempts++;
            }

            {
                uint64_t gk_addr = SDK->dwGameBase + offset::GetGlobalKey_RVA;
                uint8_t code[128] = {};
                SDK->read_buf(gk_addr, (char*)code, sizeof(code));

                uint64_t lea_target = 0;
                for (int i = 0; i < 120; i++) {
                    if (code[i] == 0x48 && code[i+1] == 0x8D &&
                        (code[i+2] & 0xC7) == 0x05) {
                        int32_t disp = *(int32_t*)&code[i+3];
                        lea_target = gk_addr + i + 7 + disp;
                        break;
                    }
                }

                uint64_t const1 = 0, const2 = 0, const3 = 0;
                for (int i = 0; i < 120; i++) {
                    if (code[i] == 0x48 && code[i+1] == 0xB9 && !const1) {
                        memcpy(&const1, code + i + 2, 8);
                        i += 9;
                    } else if (code[i] == 0x48 && code[i+1] == 0xB8) {
                        uint64_t val;
                        memcpy(&val, code + i + 2, 8);
                        if (!const2) { const2 = val; }
                        else if (!const3 && val != const2) { const3 = val; }
                        i += 9;
                    }
                }

                if (lea_target && const1 && const2 && const3) {
                    uint64_t decoded = const1 + ROR64(lea_target, 0x0A);
                    decoded ^= const2;
                    decoded -= const3;

                    printf("[Decrypt] IDA method: decoded struct ptr = 0x%llX (RVA 0x%llX)\n",
                           decoded, decoded - SDK->dwGameBase);

                    bool decoded_in_range = (decoded >= SDK->dwGameBase &&
                                             decoded < SDK->dwGameBase + 0x4000000);

                    if (decoded_in_range || decoded > 0x10000) {
                        uint64_t k1 = SDK->RPM<uint64_t>(decoded + 0x38);
                        uint64_t k2 = SDK->RPM<uint64_t>(decoded + 0xB8);

                        if (k1 > 0x1000000000000000 && k2 > 0x1000000000000000) {
                            SDK->GlobalKey1 = k1;
                            SDK->GlobalKey2 = k2;
                            printf("[Decrypt] GlobalKey1: 0x%llX (from decoded struct)\n", k1);
                            printf("[Decrypt] GlobalKey2: 0x%llX (from decoded struct)\n", k2);
                            return markResolved("decoded struct");
                        }

                        int found = 0;
                        for (int64_t off = -0x200; off <= 0x200 && found < 2; off += 8) {
                            uint64_t v = SDK->RPM<uint64_t>(decoded + off);
                            if (v > 0x1000000000000000) {
                                if (!SDK->GlobalKey1) { SDK->GlobalKey1 = v; found++; }
                                else if (!SDK->GlobalKey2 && v != SDK->GlobalKey1) { SDK->GlobalKey2 = v; found++; }
                            }
                        }
                        if (found >= 2) {
                            printf("[Decrypt] GlobalKey1: 0x%llX (scanned struct)\n", SDK->GlobalKey1.load());
                            printf("[Decrypt] GlobalKey2: 0x%llX (scanned struct)\n", SDK->GlobalKey2.load());
                            return markResolved("struct scan");
                        }
                        SDK->GlobalKey1 = SDK->GlobalKey2 = 0;
                    }

                    for (int i = 0; i < 120; i++) {
                        if (code[i] == 0x48 && code[i+1] == 0x89 && code[i+2] == 0x05) {
                            int32_t disp = *(int32_t*)&code[i+3];
                            uint64_t global_rva = (gk_addr + i + 7 + disp) - SDK->dwGameBase;
                            uint64_t global_val = SDK->RPM<uint64_t>(SDK->dwGameBase + global_rva);
                            printf("[Decrypt] Global store at RVA 0x%llX = 0x%llX\n",
                                   global_rva, global_val);

                            if (global_val > 0x1000000000000000) {
                                SDK->GlobalKey1 = global_val;
                                for (int64_t d = -0x1000; d <= 0x1000; d += 8) {
                                    if (d == 0) continue;
                                    uint64_t v = SDK->RPM<uint64_t>(SDK->dwGameBase + global_rva + d);
                                    if (v > 0x1000000000000000 && v != global_val) {
                                        SDK->GlobalKey2 = v;
                                        printf("[Decrypt] GlobalKey1: 0x%llX (global store)\n", SDK->GlobalKey1.load());
                                        printf("[Decrypt] GlobalKey2: 0x%llX (near global)\n", SDK->GlobalKey2.load());
                                        return markResolved("global store");
                                    }
                                }
                            }
                        }
                    }
                }
                SDK->GlobalKey1 = SDK->GlobalKey2 = 0;
            }

            Diagnostics::SetKeyStatus(Diagnostics::KeyStatus::Failed);
            Diagnostics::Warn("GetGlobalKey resolution attempt failed; retrying in 2s.");
            printf("[Decrypt] Key resolution failed, retrying in 2s...\n");
            RuntimeWorkerSleep(2000);
        }
        return false;
    }

    inline uint64_t GetParent(uint64_t encrypted) {
        if (offset::Active().componentTransform == offset::ComponentTransformMode::Cn154504)
            return Cn154504::Parent(encrypted);
        __try {
            auto result = encrypted;
            result = (result >> 0x20) | (result << 0x20);
            result ^= 0x4B920A7072A077C5;
            result -= 0x107816B001CA79C8;
            result = (result >> 0x23) | (result << 0x1D);
            result += 0xFD2150D0AEF24514;
            return result;
        } __except (EXCEPTION_EXECUTE_HANDLER) {
            return 0;
        }
    }

    inline void sub_E8D1A0(uint64_t* bit_mask, uint64_t* lower_mask,
                           uint32_t* shift, uint32_t* bucket,
                           uint32_t componentid) {
        *shift = componentid & 0x3F;
        *bit_mask = 1ull << *shift;
        *lower_mask = *bit_mask - 1;
        *bucket = componentid >> 6;
    }

    static constexpr uint64_t kEntityHeaderSnapshotOffset = 0x30;
    static constexpr size_t kEntityHeaderSnapshotSize = 0x120;

    struct EntityHeaderSnapshot {
        uintptr_t parent = 0;
        MemorySDK::ReadRange range{};
        bool valid = false;

        bool Read(uintptr_t value) {
            parent = value;
            valid = parent != 0 &&
                range.Read(parent + kEntityHeaderSnapshotOffset,
                           kEntityHeaderSnapshotSize);
            return valid;
        }

        bool ReadComponentLayout(std::array<uint64_t,6>& layout) const {
            uint32_t indices=0;
            if(!ReadParentOffset(0x80,layout[0]) || !ReadParentOffset(0x130,indices))return false;
            for(size_t bucket=0;bucket<4;++bucket)
                if(!ReadParentOffset(0x110+bucket*8,layout[1+bucket]))return false;
            layout[5]=indices;
            return layout[0]!=0;
        }

        template <typename T>
        bool ReadParentOffset(uint64_t offset, T& value) const {
            if (!valid) {
                value = {};
                return false;
            }
            return range.ReadAddress(parent + offset, value);
        }
    };

#ifdef UNLEASHED_HEALTH_CACHE_TEST
    inline thread_local uint64_t componentKeyTestTick = 0;
    inline thread_local void (*componentDecryptTestObserver)(uintptr_t,uint32_t,bool) = nullptr;
#endif
    inline uintptr_t DecryptComponent(uintptr_t parent, uint32_t idx,
                                      const EntityHeaderSnapshot* parent_snapshot,
                                      bool requireExactReads = false) {
#ifdef UNLEASHED_HEALTH_CACHE_TEST
        if(componentDecryptTestObserver)componentDecryptTestObserver(parent,idx,requireExactReads);
#endif
        if (!parent)
            return 0;

        uint64_t bit_mask = 0;
        uint64_t lower_mask = 0;
        uint32_t shift = 0;
        uint32_t bucket = 0;
        sub_E8D1A0(&bit_mask, &lower_mask, &shift, &bucket, idx);

        auto readParent = [&](uint64_t offset, auto& value) {
            if (parent_snapshot &&
                parent_snapshot->parent == parent &&
                parent_snapshot->ReadParentOffset(offset, value)) {
                return true;
            }
            if (requireExactReads)
                return SDK->read_range(parent + offset, &value, sizeof(value));
            value = SDK->RPM<std::remove_reference_t<decltype(value)>>(parent + offset);
            return true;
        };

        uint64_t component_bits = 0;
        if (!readParent(8ull * bucket + 0x110, component_bits))
            return 0;
        const uint64_t present = (component_bits & bit_mask) >> shift;
        if (!present)
            return 0;

        uint64_t below = component_bits & lower_mask;
        below -= (below >> 1) & 0x5555555555555555ull;
        below = (below & 0x3333333333333333ull) +
                ((below >> 2) & 0x3333333333333333ull);
        below = (below + (below >> 4)) & 0x0F0F0F0F0F0F0F0Full;

        uint8_t component_index_base = 0;
        if (!readParent(bucket + 0x130, component_index_base))
            return 0;

        const uint64_t component_index =
            component_index_base +
            ((below * 0x0101010101010101ull) >> 0x38);

        uint64_t component_table = 0;
        if (!readParent(0x80, component_table))
            return 0;
        if (!component_table) {
            Diagnostics::RecordDecryptFailure();
            return 0;
        }

        uint64_t component = 0;
        if (requireExactReads) {
            if (!SDK->read_range(component_table + 8ull * component_index, &component, sizeof(component)))
                return 0;
        } else {
            component = SDK->RPM<uint64_t>(component_table + 8ull * component_index);
        }
        if (!component) {
            Diagnostics::RecordDecryptFailure();
            return 0;
        }

        const uint64_t present_mask =
            static_cast<uint64_t>(static_cast<int64_t>(
                -static_cast<int32_t>(present)));
        const auto& activeOffsets = offset::Active();

        if (activeOffsets.componentTransform == offset::ComponentTransformMode::Cn154504) {
            struct Keys {
                uint64_t base = 0, epoch = 0, sampled = 0;
                uint8_t a = 0, b = 0;
                bool valid = false;
            };
            static thread_local Keys keys;
#ifdef UNLEASHED_HEALTH_CACHE_TEST
            const uint64_t now = componentKeyTestTick;
#else
            const uint64_t now = GetTickCount64();
#endif
            const uint64_t epoch = ProcessConnection::ConnectionEpoch();
            if (!keys.valid || keys.base != SDK->dwGameBase || keys.epoch != epoch || now - keys.sampled >= 16) {
                keys.valid = false;
                uint8_t a = 0, b = 0;
                if (!SDK->read_range(SDK->dwGameBase + Cn154504::keyByteA, &a, sizeof(a)) ||
                    !SDK->read_range(SDK->dwGameBase + Cn154504::keyByteB, &b, sizeof(b))) {
                    Diagnostics::RecordDecryptFailure();
                    return 0;
                }
                keys = {SDK->dwGameBase, epoch, now, a, b, true};
            }
            const uintptr_t decoded = static_cast<uintptr_t>(present_mask & Cn154504::Component(component, keys.a, keys.b));
            uint8_t type = 0;
            if (decoded < 0x10000 || decoded >= 0x0000800000000000ULL ||
                !SDK->read_range(decoded + 0x10, &type, sizeof(type)) || type != idx) {
                Diagnostics::RecordDecryptFailure();
                return 0;
            }
            return decoded;
        }

        if (activeOffsets.componentTransform == offset::ComponentTransformMode::Identity) {
            const uintptr_t decoded = static_cast<uintptr_t>(present_mask & component);
            if (!decoded)
                Diagnostics::RecordDecryptFailure();
            return decoded;
        }

        uint64_t component_key_source = 0;
        uint64_t component_key_material_1 = 0;
        uint8_t component_key_byte = 0;
        if (!SDK->GetCachedComponentKeyMaterial(
                SDK->dwGameBase + offset::ComponentXorQword_RVA,
                offset::ComponentXorQword_Off,
                SDK->dwGameBase + offset::ComponentXorByte_RVA,
                component_key_source,
                component_key_material_1,
                component_key_byte)) {
            Diagnostics::RecordDecryptFailure();
            return 0;
        }

        if (activeOffsets.componentTransform == offset::ComponentTransformMode::World151177) {
            component = ROR64(component ^ component_key_material_1,
                offset::Component151177_Ror1);
            component = ROR64(component + offset::Component151177_Add1,
                offset::Component151177_Ror2);
            component ^= static_cast<uint64_t>(component_key_byte);
            component ^= offset::Component151177_Xor1;
            component ^= offset::Component151177_Xor2;

            const uintptr_t decoded = static_cast<uintptr_t>(present_mask & component);
            if (!decoded)
                Diagnostics::RecordDecryptFailure();
            return decoded;
        }

        if (activeOffsets.componentTransform == offset::ComponentTransformMode::World151293) {
            component = static_cast<uint64_t>(component_key_byte) ^
                ((component_key_material_1 ^
                    (component + offset::Component151293_Add1)) +
                    offset::Component151293_Add2) ^
                offset::Component151293_Xor1;
            component ^= offset::Component151293_Xor2;
            component = ROR64(component, offset::Component151293_Ror);

            const uintptr_t decoded = static_cast<uintptr_t>(present_mask & component);
            if (!decoded)
                Diagnostics::RecordDecryptFailure();
            return decoded;
        }

        if (activeOffsets.componentTransform == offset::ComponentTransformMode::World151722) {
            component -= offset::Component151722_Sub1;
            component ^= offset::Component151722_Xor1;
            component = ROR64(component, offset::Component151722_Ror1);
            component ^= offset::Component151722_Xor2;
            component = ROR64(component, offset::Component151722_Ror2);
            component = ROR64(component, offset::Component151722_Ror3);
            component = ROR64(component, offset::Component151722_Ror4);
            component ^= component_key_material_1;
            component ^= static_cast<uint64_t>(component_key_byte);
            component += offset::Component151722_Add1;

            const uintptr_t decoded = static_cast<uintptr_t>(present_mask & component);
            if (!decoded)
                Diagnostics::RecordDecryptFailure();
            return decoded;
        }

        if (activeOffsets.componentTransform == offset::ComponentTransformMode::World150818) {
            component = static_cast<uint64_t>(component_key_byte) ^
                ((component_key_material_1 ^
                    (component + offset::Component150818_Add1)) +
                    offset::Component150818_Add2) ^
                offset::Component150818_Xor1;
            component ^= offset::Component150818_Xor2;
            component = ROR64(component, offset::Component150818_Ror);

            const uintptr_t decoded = static_cast<uintptr_t>(present_mask & component);
            if (!decoded)
                Diagnostics::RecordDecryptFailure();
            return decoded;
        }

        component ^= component_key_material_1;
        component ^= offset::Component_Xor1;
        component = ROR64(component, offset::Component_Ror1);
        component += offset::Component_Add1;
        component ^= static_cast<uint64_t>(component_key_byte);
        component -= offset::Component_Sub1;
        component = ROR64(component, offset::Component_Ror2);
        component = ROR64(component, offset::Component_Ror3);

        const uintptr_t decoded = static_cast<uintptr_t>(present_mask & component);
        if (!decoded)
            Diagnostics::RecordDecryptFailure();
        return decoded;
    }

    inline uintptr_t DecryptComponent(uintptr_t parent, uint32_t idx) {
        EntityHeaderSnapshot snapshot{};
        const EntityHeaderSnapshot* active_snapshot =
            snapshot.Read(parent) ? &snapshot : nullptr;
        return DecryptComponent(parent, idx, active_snapshot);
    }

    inline uint64_t DecodeVisibilityRaw(uint64_t raw) {
        if (offset::Active().componentTransform == offset::ComponentTransformMode::Cn154504)
            return Cn154504::Visibility(raw);
        return offset::IsCnProfile() ? raw : ((raw & offset::VisibilityBzVisibleMask) != 0 ? 1 : 0);
    }

    inline uint64_t DecryptVis(uint64_t visBase) {
        const auto& activeOffsets = offset::Active();
        if (!activeOffsets.VisibilityValueOffset)
            return 0;

        const uint64_t raw = SDK->RPM<uint64_t>(visBase + activeOffsets.VisibilityValueOffset);
        const bool worldBz = offset::ActiveProfile() == offset::RuntimeProfile::WorldBz;
        const bool visible = DecodeVisibilityRaw(raw) == 1;

        static bool loggedVisibility = false;
        if (!loggedVisibility) {
            if (worldBz) {
                Diagnostics::Info("[VISIBILITY] BZ 151722 component 0x34 +0x%llX uses visibility mask 0x%llX; set means visible.",
                    static_cast<unsigned long long>(activeOffsets.VisibilityValueOffset),
                    static_cast<unsigned long long>(offset::VisibilityBzVisibleMask));
            } else {
                Diagnostics::Info("[VISIBILITY] CN 154504N component 0x34 +0x%llX uses encrypted state; visible when decoded == 1.",
                    static_cast<unsigned long long>(activeOffsets.VisibilityValueOffset));
            }
            loggedVisibility = true;
        }

        const uint64_t result = visible ? 1 : 0;
        {
            static uint32_t sampleCount = 0;
            if (sampleCount < 50) {
                Diagnostics::Aim("visibility.raw sample=%u raw=0x%llX visible=%d visbase=0x%llX",
                    sampleCount,
                    static_cast<unsigned long long>(raw),
                    visible ? 1 : 0,
                    static_cast<unsigned long long>(visBase));
                ++sampleCount;
            }
        }
        return result;
    }

    // =========================================================================
    // Input sensitivity helpers
    // =========================================================================

    inline bool IsPlausibleSensitivity(float value) {
        return value > 0.0f && value < 100.0f;
    }

    inline bool IsPlausibleUserPointer(uint64_t value) {
        return value >= 0x10000ull &&
            value < 0x0000800000000000ull &&
            (value % alignof(void*) == 0);
    }

    inline bool IsAddressInRange(uint64_t value, uint64_t start, uint64_t end) {
        return start != 0 && end > start && value >= start && value < end;
    }

    inline bool IsLikelyEntityMatchId(uint32_t value) {
        return (value & 0x80000000u) != 0;
    }

    inline bool IsLikelyCnEntityParent(
        uint64_t value,
        uint64_t entityList,
        size_t entityListReadSize,
        uint64_t moduleBase,
        uint64_t moduleSize) {
        if (!IsPlausibleUserPointer(value))
            return false;

        const uint64_t moduleEnd =
            moduleSize != 0 ? moduleBase + moduleSize : moduleBase + 0x8000000ull;
        if (IsAddressInRange(value, moduleBase, moduleEnd))
            return false;
        if (IsAddressInRange(value, entityList, entityList + entityListReadSize))
            return false;
        return true;
    }

    inline uint64_t ResolveCnWrapperMapBase(uint64_t parent) {
        if (!IsPlausibleUserPointer(parent))
            return 0;

        const uint32_t id = SDK->RPM<uint32_t>(parent + 0x2F0);
        if (id == 0)
            return 0;

        const uint64_t bucketArray = SDK->RPM<uint64_t>(parent + 0x18);
        if (!IsPlausibleUserPointer(bucketArray))
            return 0;

        const uint64_t bucket = id & 0xFFFu;
        const uint64_t entries = SDK->RPM<uint64_t>(bucketArray + 16ull * bucket);
        const uint32_t count = SDK->RPM<uint32_t>(bucketArray + 16ull * bucket + 8);
        if (!IsPlausibleUserPointer(entries) || count == 0 || count > 4096)
            return 0;

        const uint32_t cappedCount = (std::min)(count, 512u);
        struct MapEntry { uint32_t id, padding; uint64_t pointer; };
        std::array<MapEntry,16> values{};
        for(uint32_t first=0;first<cappedCount;) {
            const auto address=entries+uint64_t(first)*sizeof(MapEntry);
            const auto pageEntries=uint32_t((0x1000-(address&0xFFF))/sizeof(MapEntry));
            const auto length=(std::min)({uint32_t(values.size()),cappedCount-first,pageEntries});
            const bool read=length && SDK->read_range(address,values.data(),size_t(length)*sizeof(MapEntry));
            if(read)for (uint32_t index = 0; index < length; ++index) {
            if(values[index].id!=id)continue;
            const uint64_t mapBase=values[index].pointer;
            if (IsPlausibleUserPointer(mapBase))
                return mapBase;
            }
            first+=length?length:1;
        }
        return 0;
    }

    inline uint64_t ResolveCnLinkTargetMapBase(
        uint64_t linkBase,
        uint64_t idOffset,
        uint32_t* resolvedId = nullptr) {
        if (resolvedId)
            *resolvedId = 0;
        if (!IsPlausibleUserPointer(linkBase))
            return 0;

        const uint32_t id = SDK->RPM<uint32_t>(linkBase + idOffset);
        if (id == 0)
            return 0;

        const uint64_t context = SDK->RPM<uint64_t>(linkBase + 0x8);
        if (!IsPlausibleUserPointer(context))
            return 0;

        const uint64_t bucketOwner = SDK->RPM<uint64_t>(context + 0x28);
        if (!IsPlausibleUserPointer(bucketOwner))
            return 0;

        const uint64_t bucketArray = SDK->RPM<uint64_t>(bucketOwner + 0x18);
        if (!IsPlausibleUserPointer(bucketArray))
            return 0;

        const uint64_t bucket = id & 0xFFFu;
        const uint64_t entries = SDK->RPM<uint64_t>(bucketArray + 16ull * bucket);
        const uint32_t count = SDK->RPM<uint32_t>(bucketArray + 16ull * bucket + 8);
        if (!IsPlausibleUserPointer(entries) || count == 0 || count > 4096)
            return 0;

        const uint32_t cappedCount = (std::min)(count, 512u);
        struct MapEntry { uint32_t id, padding; uint64_t pointer; };
        std::array<MapEntry,16> values{};
        for(uint32_t first=0;first<cappedCount;) {
            const auto address=entries+uint64_t(first)*sizeof(MapEntry);
            const auto pageEntries=uint32_t((0x1000-(address&0xFFF))/sizeof(MapEntry));
            const auto length=(std::min)({uint32_t(values.size()),cappedCount-first,pageEntries});
            const bool read=length && SDK->read_range(address,values.data(),size_t(length)*sizeof(MapEntry));
            if(read)for (uint32_t index = 0; index < length; ++index) {
            if(values[index].id!=id)continue;
            const uint64_t mapBase=values[index].pointer;
            if (IsPlausibleUserPointer(mapBase)) {
                if (resolvedId)
                    *resolvedId = id;
                return mapBase;
            }
            }
            first+=length?length:1;
        }

        return 0;
    }

    inline bool AddCnMapBaseCandidate(
        std::vector<uint64_t>& candidates,
        uint64_t value,
        uint64_t entityList,
        size_t entityListReadSize,
        uint64_t moduleBase,
        uint64_t moduleSize) {
        if (!IsLikelyCnEntityParent(
                value,
                entityList,
                entityListReadSize,
                moduleBase,
                moduleSize)) {
            return false;
        }
        if (std::find(candidates.begin(), candidates.end(), value) != candidates.end())
            return false;
        candidates.push_back(value);
        return true;
    }

    inline std::vector<uint64_t> CollectCnMapBaseCandidates(
        uint64_t parent,
        uint64_t entityList,
        size_t entityListReadSize,
        uint64_t moduleBase,
        uint64_t moduleSize,
        Diagnostics::EntityScanDetailStats* scanDetail = nullptr,
        bool mapDiagEnabled = false) {
        std::vector<uint64_t> candidates;
        candidates.reserve(3);

        if (AddCnMapBaseCandidate(
                candidates,
                parent,
                entityList,
                entityListReadSize,
                moduleBase,
                moduleSize) &&
            mapDiagEnabled &&
            scanDetail) {
            ++scanDetail->cnMapCandidateDirectSourceCount;
        }

        const uint64_t plus8 = SDK->RPM<uint64_t>(parent + 0x8);
        if (AddCnMapBaseCandidate(
                candidates,
                plus8,
                entityList,
                entityListReadSize,
                moduleBase,
                moduleSize) &&
            mapDiagEnabled &&
            scanDetail) {
            ++scanDetail->cnMapCandidatePlus8SourceCount;
        }

        const uint64_t wrapperMapBase = ResolveCnWrapperMapBase(parent);
        if (AddCnMapBaseCandidate(
                candidates,
                wrapperMapBase,
                entityList,
                entityListReadSize,
                moduleBase,
                moduleSize) &&
            mapDiagEnabled &&
            scanDetail) {
            ++scanDetail->cnMapCandidateWrapperSourceCount;
        }

        return candidates;
    }

    inline uint64_t DecryptSingletonList(uint64_t raw) {
        return ROR64(
            (raw ^ offset::Singleton_K1_xor) - offset::Singleton_K2_sub,
            offset::Singleton_Ror) + offset::Singleton_K3_add;
    }

    inline uint64_t DecryptLiveGameAdmin(uint64_t raw) {
        return ROR64(
            ((raw + offset::LiveGameAdmin_Add1) ^ offset::LiveGameAdmin_Xor1) +
                offset::LiveGameAdmin_Add2,
            offset::LiveGameAdmin_Ror);
    }

    inline uint64_t DecryptSingletonList151722(uint64_t raw) {
        uint64_t value = ROR64(raw, offset::Singleton151722_Ror1);
        value -= offset::Singleton151722_Sub1;
        value ^= offset::Singleton151722_Xor1;
        return ROR64(value, offset::Singleton151722_Ror2);
    }

    inline bool TryGetLiveGameAdmin(uint64_t& gameAdmin) {
        gameAdmin = 0;
        if (!SDK->dwGameBase)
            return false;

        const auto& activeOffsets = offset::Active();
        if (!activeOffsets.GlobalAdmin_RVA)
            return false;

        const uint64_t clientGame = SDK->RPM<uint64_t>(
            SDK->dwGameBase + activeOffsets.GlobalAdmin_RVA);
        if (!IsPlausibleUserPointer(clientGame))
            return false;

        const uint64_t raw = SDK->RPM<uint64_t>(
            clientGame + offset::LiveGameAdmin_InputOffset);
        if (!raw)
            return false;

        gameAdmin = DecryptLiveGameAdmin(raw);
        return IsPlausibleUserPointer(gameAdmin);
    }

    inline uint64_t ResolveSingletonList(uint64_t raw) {
        switch (offset::Active().singletonListMode) {
        case offset::SingletonListMode::Cn154504:
            return Cn154504::Singleton(raw);
        case offset::SingletonListMode::Plain:
            return raw;
        case offset::SingletonListMode::LiveGameAdminWorldBz:
            return raw;
        case offset::SingletonListMode::EncryptedWorldBz151293:
            return DecryptLiveGameAdmin(raw);
        case offset::SingletonListMode::EncryptedWorldBz151722:
            return DecryptSingletonList151722(raw);
        case offset::SingletonListMode::EncryptedWorldBz:
        default:
            return DecryptSingletonList(raw);
        }
    }

    inline bool TryGetLiveGameAdminSingleton(uint64_t index, uint64_t& object) {
        object = 0;

        uint64_t gameAdmin = 0;
        if (!TryGetLiveGameAdmin(gameAdmin))
            return false;

        const uint64_t listOffsets[] = {
            offset::Singleton_InputOffset,
            0x168,
            0x2E0,
            0x300,
            0x310,
        };

        for (const uint64_t listOffset : listOffsets) {
            const uint64_t raw = SDK->RPM<uint64_t>(gameAdmin + listOffset);
            const uint64_t candidates[] = {
                raw,
                DecryptSingletonList(raw),
                DecryptLiveGameAdmin(raw),
                DecryptSingletonList151722(raw),
            };
            for (const uint64_t list : candidates) {
                if (!IsPlausibleUserPointer(list))
                    continue;
                const uint64_t candidate = SDK->RPM<uint64_t>(list + 8ull * index);
                if (IsPlausibleUserPointer(candidate)) {
                    object = candidate;
                    return true;
                }
            }
        }

        return false;
    }

    inline bool TryGetGlobalAdminSingleton(uint64_t index, uint64_t& object) {
        object = 0;
        if (!SDK->dwGameBase)
            return false;

        const auto& activeOffsets = offset::Active();
        if (!activeOffsets.GlobalAdmin_RVA)
            return false;

        if (activeOffsets.singletonListMode == offset::SingletonListMode::LiveGameAdminWorldBz)
            return TryGetLiveGameAdminSingleton(index, object);

        const uint64_t admin = SDK->RPM<uint64_t>(
            SDK->dwGameBase + activeOffsets.GlobalAdmin_RVA);
        if (!IsPlausibleUserPointer(admin))
            return false;

        const uint64_t listRaw = SDK->RPM<uint64_t>(
            admin + offset::Singleton_InputOffset);
        const uint64_t list = ResolveSingletonList(listRaw);
        if (!IsPlausibleUserPointer(list))
            return false;

        object = SDK->RPM<uint64_t>(list + 8ull * index);
        return IsPlausibleUserPointer(object);
    }

    inline bool TryReadSensitivityFromObject(uint64_t object, float& value) {
        if (!IsPlausibleUserPointer(object))
            return false;

        const float sensitivity = SDK->RPM<float>(object + offset::Sensitivity);
        if (!IsPlausibleSensitivity(sensitivity))
            return false;

        const float sensX = SDK->RPM<float>(object + offset::SensX_Scale);
        const float sensY = SDK->RPM<float>(object + offset::SensY_Scale);
        if (!std::isfinite(sensX) || !std::isfinite(sensY))
            return false;

        value = sensitivity;
        return true;
    }

    inline bool TryReadInputMouseScaleSensitivity(float& value, uint64_t* sourceObject) {
        value = 0.0f;
        if (sourceObject)
            *sourceObject = 0;
        if (!SDK->dwGameBase)
            return false;

        const auto& activeOffsets = offset::Active();
        if (!activeOffsets.InputMouseScaleX_RVA || !activeOffsets.InputMouseScaleY_RVA)
            return false;

        const uint64_t xAddress = SDK->dwGameBase + activeOffsets.InputMouseScaleX_RVA;
        const uint64_t yAddress = SDK->dwGameBase + activeOffsets.InputMouseScaleY_RVA;
        const float sensX = SDK->RPM<float>(xAddress);
        const float sensY = SDK->RPM<float>(yAddress);
        if (!IsPlausibleSensitivity(sensX) || !IsPlausibleSensitivity(sensY))
            return false;

        const float tolerance = (std::max)(0.05f, (std::max)(sensX, sensY) * 0.02f);
        if (std::fabs(sensX - sensY) > tolerance)
            return false;

        value = (sensX + sensY) * 0.5f;
        if (sourceObject)
            *sourceObject = xAddress;
        return true;
    }

    inline bool TryReadNormalizedSensitivityObject(
        uint64_t object,
        float& value,
        uint64_t* sourceObject) {
        value = 0.0f;
        if (!IsPlausibleUserPointer(object))
            return false;

        const float normalized = SDK->RPM<float>(object + offset::Sensitivity);
        if (!std::isfinite(normalized) || normalized < 0.0001f || normalized > 1.0f)
            return false;

        value = normalized * offset::Sensitivity_NormalizedToUserScale;
        if (!IsPlausibleSensitivity(value))
            return false;

        if (sourceObject)
            *sourceObject = object;
        return true;
    }

    inline bool TryReadWorldBzGlobalAdminSensitivity(
        float& value,
        uint64_t* sourceObject) {
        value = 0.0f;
        if (sourceObject)
            *sourceObject = 0;
        if (!SDK->dwGameBase || !offset::SensitivityAdmin_WorldBz_RVA)
            return false;

        const uint64_t admin = SDK->RPM<uint64_t>(
            SDK->dwGameBase + offset::SensitivityAdmin_WorldBz_RVA);
        if (!IsPlausibleUserPointer(admin))
            return false;

        const uint64_t raw = SDK->RPM<uint64_t>(admin + offset::Singleton_InputOffset);
        if (!raw)
            return false;

        const uint64_t lists[] = {
            raw,
            DecryptLiveGameAdmin(raw),
            DecryptSingletonList(raw),
            DecryptSingletonList151722(raw),
        };
        for (uint64_t list : lists) {
            if (!IsPlausibleUserPointer(list))
                continue;

            const uint64_t object = SDK->RPM<uint64_t>(
                list + 8ull * offset::SensitivitySingletonIndex);
            if (TryReadNormalizedSensitivityObject(object, value, sourceObject))
                return true;
        }

        return false;
    }

    inline bool TryReadLiveGameAdminSensitivity(float& value, uint64_t* sourceObject) {
        value = 0.0f;
        if (sourceObject)
            *sourceObject = 0;

        if (TryReadWorldBzGlobalAdminSensitivity(value, sourceObject))
            return true;

        uint64_t gameAdmin = 0;
        if (!TryGetLiveGameAdmin(gameAdmin))
            return false;

        uint64_t sensitivityObject = 0;
        if (TryGetLiveGameAdminSingleton(offset::SensitivitySingletonIndex, sensitivityObject) &&
            TryReadSensitivityFromObject(sensitivityObject, value)) {
            if (sourceObject)
                *sourceObject = sensitivityObject;
            return true;
        }

        return false;
    }

    inline bool TryReadGameMouseSensitivity(float& value, uint64_t* sourceObject = nullptr) {
        value = 0.0f;
        if (sourceObject)
            *sourceObject = 0;

        if (offset::Active().singletonListMode == offset::SingletonListMode::LiveGameAdminWorldBz)
            return TryReadLiveGameAdminSensitivity(value, sourceObject) ||
                TryReadInputMouseScaleSensitivity(value, sourceObject);

        uint64_t sensitivityObject = 0;
        if (!TryGetGlobalAdminSingleton(offset::SensitivitySingletonIndex, sensitivityObject))
            return false;

        if (!TryReadSensitivityFromObject(sensitivityObject, value))
            return false;

        if (sourceObject)
            *sourceObject = sensitivityObject;
        return true;
    }

    inline uintptr_t GetSenstivePTR() {
        static std::mutex cacheMutex;
        std::lock_guard<std::mutex> cacheLock(cacheMutex);
        if (!SDK->dwGameBase)
            return 0;

        static uintptr_t cached = 0;
        static uint64_t cachedBase = 0;
        static uint64_t cachedEpoch = 0;
        static offset::RuntimeProfile cachedProfile = offset::RuntimeProfile::WorldBz;
        const offset::RuntimeProfile currentProfile = offset::ActiveProfile();
        const uint64_t epoch = ProcessConnection::ConnectionEpoch();
        if (cached && cachedEpoch == epoch && cachedBase == SDK->dwGameBase && cachedProfile == currentProfile)
            return cached;

        cached = 0;
        cachedBase = SDK->dwGameBase;
        cachedEpoch = epoch;
        cachedProfile = currentProfile;

        const auto& activeOffsets = offset::Active();
        const uintptr_t mouse_scale_x =
            SDK->dwGameBase + activeOffsets.InputMouseScaleX_RVA;
        if (IsPlausibleSensitivity(SDK->RPM<float>(mouse_scale_x))) {
            cached = mouse_scale_x;
            return cached;
        }

        return 0;
    }

    inline int ReadEntityScannerEnvFlagState(const char* name)
    {
        char buffer[16] = {};
        const DWORD length = GetCnEnvironmentVariableA(name, buffer, static_cast<DWORD>(sizeof(buffer)));
        if (length == 0 || length >= sizeof(buffer))
            return -1;
        return buffer[0] != '0' &&
            buffer[0] != 'n' &&
            buffer[0] != 'N' &&
            buffer[0] != 'f' &&
            buffer[0] != 'F'
            ? 1
            : 0;
    }

    inline bool EntityScannerEnvFlagEnabled(const char* name)
    {
        return ReadEntityScannerEnvFlagState(name) == 1;
    }

    inline bool EntityLightScanRequested()
    {
        static const int lightScanFlag =
            ReadEntityScannerEnvFlagState("UN_DMA_LIGHT_SCAN");
        if (lightScanFlag >= 0)
            return lightScanFlag == 1;
        return offset::IsCnProfile() ||
            EntityScannerEnvFlagEnabled("UN_DMA_ENTITY_PIPELINE_V2");
    }

    inline size_t EntityLightScanMaxCandidates()
    {
        char buffer[32] = {};
        const DWORD length = GetCnEnvironmentVariableA(
            "UN_DMA_LIGHT_SCAN_MAX_CANDIDATES",
            buffer,
            static_cast<DWORD>(sizeof(buffer)));
        if (length == 0 || length >= sizeof(buffer))
            return 128;

        char* end = nullptr;
        const unsigned long parsed = std::strtoul(buffer, &end, 10);
        if (end == buffer || (end && *end != '\0'))
            return 128;
        return (std::min<size_t>)((std::max<size_t>)(parsed, 16), 512);
    }

    inline DWORD EntityScannerPersistentMapCacheTtlMs()
    {
        static const DWORD ttlMs = []() {
            char buffer[32] = {};
            const DWORD length = GetCnEnvironmentVariableA(
                "UN_DMA_CN_STAGE4B_MAP_CACHE_PERSIST_MS",
                buffer,
                static_cast<DWORD>(sizeof(buffer)));
            if (length == 0 || length >= sizeof(buffer))
                return static_cast<DWORD>(0);

            char* end = nullptr;
            const unsigned long parsed = std::strtoul(buffer, &end, 10);
            if (end == buffer || (end && *end != '\0'))
                return static_cast<DWORD>(0);
            if (parsed == 0)
                return static_cast<DWORD>(0);
            return (std::min<DWORD>)((std::max<DWORD>)(parsed, 50), 2000);
        }();
        return ttlMs;
    }

    inline size_t EntityScannerPersistentMapCacheRefreshBudget()
    {
        static const size_t refreshBudget = []() {
            char buffer[32] = {};
            const DWORD length = GetCnEnvironmentVariableA(
                "UN_DMA_CN_STAGE4B_MAP_CACHE_REFRESH_BUDGET",
                buffer,
                static_cast<DWORD>(sizeof(buffer)));
            if (length == 0 || length >= sizeof(buffer))
                return static_cast<size_t>(0);

            char* end = nullptr;
            const unsigned long parsed = std::strtoul(buffer, &end, 10);
            if (end == buffer || (end && *end != '\0'))
                return static_cast<size_t>(0);
            if (parsed == 0)
                return static_cast<size_t>(0);
            return (std::min<size_t>)((std::max<size_t>)(parsed, 1), 64);
        }();
        return refreshBudget;
    }

    inline DWORD EntityScannerComponentNegativeCacheTtlMs()
    {
        static const DWORD ttlMs = []() {
            char buffer[32] = {};
            const DWORD length = GetCnEnvironmentVariableA(
                "UN_DMA_CN_COMPONENT_NEGATIVE_CACHE_MS",
                buffer,
                static_cast<DWORD>(sizeof(buffer)));
            if (length == 0 || length >= sizeof(buffer))
                return static_cast<DWORD>(0);

            char* end = nullptr;
            const unsigned long parsed = std::strtoul(buffer, &end, 10);
            if (end == buffer || (end && *end != '\0'))
                return static_cast<DWORD>(0);
            if (parsed == 0)
                return static_cast<DWORD>(0);
            return (std::min<DWORD>)((std::max<DWORD>)(parsed, 50), 2000);
        }();
        return ttlMs;
    }

    inline DWORD EntityScannerLinkDecryptNegativeCacheTtlMs()
    {
        static const DWORD ttlMs = []() {
            char buffer[32] = {};
            const DWORD length = GetCnEnvironmentVariableA(
                "UN_DMA_CN_LINK_DECRYPT_NEGATIVE_CACHE_MS",
                buffer,
                static_cast<DWORD>(sizeof(buffer)));
            if (length == 0 || length >= sizeof(buffer))
                return static_cast<DWORD>(0);

            char* end = nullptr;
            const unsigned long parsed = std::strtoul(buffer, &end, 10);
            if (end == buffer || (end && *end != '\0'))
                return static_cast<DWORD>(0);
            if (parsed == 0)
                return static_cast<DWORD>(0);
            return (std::min<DWORD>)((std::max<DWORD>)(parsed, 50), 2000);
        }();
        return ttlMs;
    }

    inline DWORD EntityScannerRecordSnapshotCacheTtlMs()
    {
        static const DWORD ttlMs = []() {
            char buffer[32] = {};
            const DWORD length = GetCnEnvironmentVariableA(
                "UN_DMA_CN_RECORD_SNAPSHOT_CACHE_MS",
                buffer,
                static_cast<DWORD>(sizeof(buffer)));
            if (length == 0 || length >= sizeof(buffer))
                return static_cast<DWORD>(0);

            char* end = nullptr;
            const unsigned long parsed = std::strtoul(buffer, &end, 10);
            if (end == buffer || (end && *end != '\0'))
                return static_cast<DWORD>(0);
            if (parsed == 0)
                return static_cast<DWORD>(0);
            return (std::min<DWORD>)((std::max<DWORD>)(parsed, 50), 2000);
        }();
        return ttlMs;
    }

    inline DWORD EntityScannerEntityListRootCacheTtlMs()
    {
        static const DWORD ttlMs = []() {
            char buffer[32] = {};
            const DWORD length = GetCnEnvironmentVariableA(
                "UN_DMA_CN_ENTITY_LIST_ROOT_CACHE_MS",
                buffer,
                static_cast<DWORD>(sizeof(buffer)));
            if (length == 0 || length >= sizeof(buffer))
                return static_cast<DWORD>(0);

            char* end = nullptr;
            const unsigned long parsed = std::strtoul(buffer, &end, 10);
            if (end == buffer || (end && *end != '\0'))
                return static_cast<DWORD>(0);
            if (parsed == 0)
                return static_cast<DWORD>(0);
            return (std::min<DWORD>)((std::max<DWORD>)(parsed, 50), 10000);
        }();
        return ttlMs;
    }

    inline DWORD EntityScannerEntityListReadNegativeCacheTtlMs()
    {
        static const DWORD ttlMs = []() {
            char buffer[32] = {};
            const DWORD length = GetCnEnvironmentVariableA(
                "UN_DMA_CN_ENTITY_LIST_READ_NEGATIVE_CACHE_MS",
                buffer,
                static_cast<DWORD>(sizeof(buffer)));
            if (length == 0 || length >= sizeof(buffer))
                return static_cast<DWORD>(0);

            char* end = nullptr;
            const unsigned long parsed = std::strtoul(buffer, &end, 10);
            if (end == buffer || (end && *end != '\0'))
                return static_cast<DWORD>(0);
            if (parsed == 0)
                return static_cast<DWORD>(0);
            return (std::min<DWORD>)((std::max<DWORD>)(parsed, 50), 2000);
        }();
        return ttlMs;
    }

    inline DWORD EntityScannerEntityListReadCacheTtlMs()
    {
        static const DWORD ttlMs = []() {
            char buffer[32] = {};
            const DWORD length = GetCnEnvironmentVariableA(
                "UN_DMA_CN_ENTITY_LIST_READ_CACHE_MS",
                buffer,
                static_cast<DWORD>(sizeof(buffer)));
            if (length == 0 || length >= sizeof(buffer))
                return static_cast<DWORD>(0);

            char* end = nullptr;
            const unsigned long parsed = std::strtoul(buffer, &end, 10);
            if (end == buffer || (end && *end != '\0'))
                return static_cast<DWORD>(0);
            if (parsed == 0)
                return static_cast<DWORD>(0);
            return (std::min<DWORD>)((std::max<DWORD>)(parsed, 16), 2000);
        }();
        return ttlMs;
    }

    inline size_t EntityScannerCnEntityListChunkSize()
    {
        static const size_t chunkSize = []() {
            char buffer[32] = {};
            const DWORD length = GetCnEnvironmentVariableA(
                "UN_DMA_CN_ENTITY_LIST_CHUNK_SIZE",
                buffer,
                static_cast<DWORD>(sizeof(buffer)));
            if (length == 0 || length >= sizeof(buffer))
                return static_cast<size_t>(0x10000);

            char* end = nullptr;
            const unsigned long parsed = std::strtoul(buffer, &end, 0);
            if (end == buffer || (end && *end != '\0'))
                return static_cast<size_t>(0x10000);
            size_t value = (std::min<size_t>)(
                (std::max<size_t>)(parsed, 0x1000),
                0x10000);
            value -= value % 0x1000;
            return value == 0 ? static_cast<size_t>(0x1000) : value;
        }();
        return chunkSize;
    }

    inline size_t EntityScannerRecordSnapshotCacheRefreshBudget()
    {
        static const size_t refreshBudget = []() {
            char buffer[32] = {};
            const DWORD length = GetCnEnvironmentVariableA(
                "UN_DMA_CN_RECORD_SNAPSHOT_CACHE_REFRESH_BUDGET",
                buffer,
                static_cast<DWORD>(sizeof(buffer)));
            if (length == 0 || length >= sizeof(buffer))
                return static_cast<size_t>(0);

            char* end = nullptr;
            const unsigned long parsed = std::strtoul(buffer, &end, 10);
            if (end == buffer || (end && *end != '\0'))
                return static_cast<size_t>(0);
            if (parsed == 0)
                return static_cast<size_t>(0);
            return (std::min<size_t>)((std::max<size_t>)(parsed, 1), 64);
        }();
        return refreshBudget;
    }

    inline size_t EntityScannerComponentNegativeCacheRefreshBudget()
    {
        static const size_t refreshBudget = []() {
            char buffer[32] = {};
            const DWORD length = GetCnEnvironmentVariableA(
                "UN_DMA_CN_COMPONENT_NEGATIVE_CACHE_REFRESH_BUDGET",
                buffer,
                static_cast<DWORD>(sizeof(buffer)));
            if (length == 0 || length >= sizeof(buffer))
                return static_cast<size_t>(0);

            char* end = nullptr;
            const unsigned long parsed = std::strtoul(buffer, &end, 10);
            if (end == buffer || (end && *end != '\0'))
                return static_cast<size_t>(0);
            if (parsed == 0)
                return static_cast<size_t>(0);
            return (std::min<size_t>)((std::max<size_t>)(parsed, 1), 64);
        }();
        return refreshBudget;
    }

    inline DWORD EntityScannerStaleMetadataMs()
    {
        static const DWORD staleMs = []() {
            char buffer[32] = {};
            const DWORD length = GetCnEnvironmentVariableA(
                "UN_DMA_CN_SCANNER_STALE_METADATA_MS",
                buffer,
                static_cast<DWORD>(sizeof(buffer)));
            if (length == 0 || length >= sizeof(buffer))
                return static_cast<DWORD>(0);

            char* end = nullptr;
            const unsigned long parsed = std::strtoul(buffer, &end, 10);
            if (end == buffer || (end && *end != '\0'))
                return static_cast<DWORD>(0);
            if (parsed == 0)
                return static_cast<DWORD>(0);
            return (std::min<DWORD>)((std::max<DWORD>)(parsed, 50), 30000);
        }();
        return staleMs;
    }

    // =========================================================================
    // Entity list scanning
    // =========================================================================

    inline bool DynamicEntityRegistryEnabled() {
        return offset::Active().componentTransform==offset::ComponentTransformMode::Cn154504;
    }
#ifdef UNLEASHED_REGISTRY_PRODUCER_TEST
    inline uint64_t (*registryProducerClock)()=&::GetTickCount64;
    inline void (*registryProducerEvent)(const char*)=nullptr;
#define REGISTRY_PRODUCER_EVENT(name) do { if(registryProducerEvent)registryProducerEvent(name); } while(false)
#else
#define REGISTRY_PRODUCER_EVENT(name) do {} while(false)
#endif
    inline uint64_t EntityRegistryNowMs() {
#ifdef UNLEASHED_REGISTRY_PRODUCER_TEST
        return registryProducerClock();
#else
        return GetTickCount64();
#endif
    }
#ifdef UNLEASHED_DISCOVERY_TEST
    inline std::chrono::steady_clock::time_point (*discoveryBudgetClock)()=nullptr;
    inline void (*discoveryProbeTest)(uint64_t,bool,bool,bool)=nullptr;
#endif
    inline std::chrono::steady_clock::time_point EntityDiscoveryBudgetNow() {
#ifdef UNLEASHED_DISCOVERY_TEST
        if(discoveryBudgetClock)return discoveryBudgetClock();
#endif
#ifdef UNLEASHED_REGISTRY_RELOCATION_TEST
        return std::chrono::steady_clock::time_point(std::chrono::milliseconds(EntityRegistryNowMs()));
#else
        return std::chrono::steady_clock::now();
#endif
    }
#ifdef UNLEASHED_REGISTRY_RELOCATION_TEST
    inline void (*registryRelocationEvent)(const char*)=nullptr;
#define REGISTRY_RELOCATION_EVENT(name) do { if(OW::registryRelocationEvent)OW::registryRelocationEvent(name); } while(false)
#else
#define REGISTRY_RELOCATION_EVENT(name) do {} while(false)
#endif
    inline EntityRegistry::State RefreshEntityRegistry(bool force=false) {
        if(!DynamicEntityRegistryEnabled()) {
            EntityRegistry::State legacy;legacy.available=true;legacy.error=EntityRegistry::Error::None;return legacy;
        }
        auto& monitor=entityRegistryMonitor;
        std::unique_lock<std::mutex> readerLock(monitor.readMutex,std::defer_lock);
        REGISTRY_PRODUCER_EVENT("reader_wait");
        if(force)readerLock.lock();
        else if(!readerLock.try_lock()) {
            std::lock_guard<std::mutex> lock(monitor.mutex);return monitor.state;
        }
        REGISTRY_PRODUCER_EVENT("reader_acquired");
#ifdef UNLEASHED_REGISTRY_PRODUCER_TEST
        const uint64_t now=registryProducerClock(),epoch=ProcessConnection::ConnectionEpoch(),module=SDK->dwGameBase;
#else
        const uint64_t now=GetTickCount64(),epoch=ProcessConnection::ConnectionEpoch(),module=SDK->dwGameBase;
#endif
        const bool connected=ProcessConnection::IsConnected() && RuntimeWorkersRunning();
        {
            std::lock_guard<std::mutex> lock(monitor.mutex);
            auto& state=monitor.state;
            if(!connected) { state.available=false;state.error=EntityRegistry::Error::Disconnected;return state; }
            if(!state.ShouldRead(epoch,module,now,force))return state;
        }
        const auto started=std::chrono::steady_clock::now();
        EntityRegistry::Snapshot incoming{};
        const auto contextCurrent=[&] {
            return RuntimeWorkersRunning() && ProcessConnection::IsConnected() &&
                ProcessConnection::ConnectionEpoch()==epoch && SDK->dwGameBase==module;
        };
        auto error=EntityRegistry::ReadSnapshotRetryBusy(epoch,module,[&](uint64_t address,void* output,size_t size) {
            if(!contextCurrent())return false;
            return mem.ReadFreshExact(address,output,size);
        },[&] {
            REGISTRY_PRODUCER_EVENT("pause_begin");
            const bool running=RuntimeWorkerSleep(1);
            REGISTRY_PRODUCER_EVENT("pause_end");
            return running && contextCurrent();
        },incoming);
        // V2 恢复定位只运行在串行 reader lane；正式扫描仍消费原有代次快照。
        if(error!=EntityRegistry::Error::None && error!=EntityRegistry::Error::Busy &&
           error!=EntityRegistry::Error::Unstable && contextCurrent()) {
            static EntityRegistry::RegistryResolver resolver;
            static uint64_t resetAt=0;
            static bool rangesReady=false;
            const bool terminal=resolver.status==EntityRegistry::ResolveStatus::NoMatch ||
                resolver.status==EntityRegistry::ResolveStatus::Incomplete ||
                resolver.status==EntityRegistry::ResolveStatus::Ambiguous ||
                resolver.status==EntityRegistry::ResolveStatus::EmptyUnconfirmed;
            if(!resolver.Matches(epoch,module) || (terminal && now-resetAt>5000)) {
                const auto ranges=V2::Memory::WritableSections(module,Cn154504::imageSize,
                    [&](uint64_t at,void* dst,size_t n){return contextCurrent() && mem.ReadFreshExact(at,dst,n);});
                std::vector<EntityRegistry::SearchRange> input;
                for(auto r:ranges)input.push_back({r.begin,r.end});
                rangesReady=!input.empty();
                resolver.Reset(epoch,module,std::move(input));resetAt=now;
                if(!rangesReady)resolver.status=EntityRegistry::ResolveStatus::Incomplete;
            }
            if(rangesReady) {
                error=resolver.Step([&](uint64_t at,void* dst,size_t n){return mem.ReadFreshExact(at,dst,n);},
                    contextCurrent,[&](uint64_t parent,const EntityRegistry::Snapshot& table) {
                        if(!IsLikelyCnEntityParent(parent,table.root,table.Bytes(),module,Cn154504::imageSize))return false;
                        // 候选必须具有当前 CN parent 的实读身份及组件位图，非零地址不算语义通过。
                        uint32_t match=0;std::array<uint64_t,2> components{};
                        return contextCurrent() && mem.ReadFreshExact(parent+offset::Entity_MatchId,&match,sizeof(match)) &&
                            IsLikelyEntityMatchId(match) &&
                            mem.ReadFreshExact(parent+0x110,components.data(),sizeof(components)) &&
                            (components[0] || components[1]);
                    },incoming);
                if(resolver.status==EntityRegistry::ResolveStatus::Recovered)
                    Diagnostics::Info("V2 registry recovered stateRva=0x%llX pages=%llu bytes=%llu.",
                        resolver.Location(),resolver.pages,resolver.bytes);
            }
        }
        REGISTRY_PRODUCER_EVENT("before_commit");
        // Never hold the publication-state mutex across device I/O.
        // Serialize the running check AND Observe with SetRuntimeWorkersRunning(false).
        std::unique_lock<std::mutex> runningLock(runtimeWorkerWaitMutex);
        std::lock_guard<std::mutex> lock(monitor.mutex);
        if(!contextCurrent())error=EntityRegistry::Error::Disconnected;
        auto& state=monitor.state;
        const auto oldError=state.error;const auto oldGeneration=state.snapshot.generation;
        state.attemptedEpoch=epoch;state.attemptedModule=module;
        state.Observe(error,incoming,now);
        REGISTRY_PRODUCER_EVENT("committed");
        state.readUs=std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now()-started).count();
        runningLock.unlock();
        if(error!=oldError || state.snapshot.generation!=oldGeneration)
            Diagnostics::Info("Entity registry status=%u generation=%llu root=0x%llX capacity=%u bytes=%llu pending=%d.",
                unsigned(error),state.snapshot.generation,state.snapshot.root,state.snapshot.capacity,state.snapshot.Bytes(),state.rescanPending);
        return state;
    }
#undef REGISTRY_PRODUCER_EVENT
    inline bool AcceptEntityRegistry(const EntityRegistry::Snapshot& snapshot) {
        if(!DynamicEntityRegistryEnabled())return true;
        std::lock_guard<std::mutex> lock(entityRegistryMonitor.mutex);
        return ProcessConnection::IsConnected() && snapshot.epoch==ProcessConnection::ConnectionEpoch() &&
            entityRegistryMonitor.state.Accept(snapshot);
    }
    inline bool AcknowledgeEntityRegistry(const EntityRegistry::Snapshot& snapshot,EntityRegistry::ScanStatus status) {
        if(!DynamicEntityRegistryEnabled())return status==EntityRegistry::ScanStatus::Complete;
        std::lock_guard<std::mutex> lock(entityRegistryMonitor.mutex);
        return entityRegistryMonitor.state.Acknowledge(snapshot,status);
    }
    struct EntityRegistryScanGuard {
        EntityRegistry::Snapshot snapshot{};
        bool dynamic=DynamicEntityRegistryEnabled(),valid=true;
        uint64_t lastCheckMs=EntityRegistryNowMs(),sliceMs=lastCheckMs;
        EntityRegistryScanGuard() {
            if(dynamic) { const auto state=RefreshEntityRegistry(true);snapshot=state.snapshot;valid=state.available; }
        }
        bool Check(bool force=false) {
            if(!valid)return false;
            const auto now=EntityRegistryNowMs();
            if(dynamic && (force || now-lastCheckMs>=EntityRegistry::pollMs)) {
                RefreshEntityRegistry(true);lastCheckMs=now;
            }
            if(dynamic)valid=AcceptEntityRegistry(snapshot);
            return valid && RuntimeWorkersRunning() && ProcessConnection::IsConnected();
        }
    };

    inline EntityTopologySample ProbeEntityTopologyMembers(bool readMemberIds) {
        EntityTopologySample sample{};
        struct Entity {
            uint64_t entity;
            uint64_t pad;
        };

        EntityRegistryScanGuard guard;
        if(!guard.valid)return sample;
        sample.registry=guard.snapshot;
        const uint64_t entity_list = guard.dynamic?guard.snapshot.root:SDK->RPM<uint64_t>(
            SDK->dwGameBase + offset::Active().Address_entity_base);
        if (!entity_list)
            return sample;

        sample.root=entity_list;
        sample.complete=true;
        const bool cnProfile = offset::IsCnProfile();
        const size_t listReadSize = guard.dynamic?guard.snapshot.Bytes():(cnProfile ? 0x40000 : 0x1E000);
        const size_t chunkSize = cnProfile ? 0x10000 : 0x1000;
        uint64_t moduleSize = 0;
        if (cnProfile)
            moduleSize = static_cast<uint64_t>(mem.GetBaseSize("Overwatch.exe"));

        std::unordered_set<uint64_t> seen{};
        bool membershipClassified=true;
        seen.reserve(256);
        std::vector<uint8_t> chunk(chunkSize);
        for (size_t offset = 0; offset < listReadSize; offset += chunkSize) {
            if(!guard.Check()){sample.complete=false;break;}
            if (!RuntimeWorkersRunning() || !ProcessConnection::IsConnected())
                break;
            const size_t remaining = listReadSize - offset;
            const size_t bytesToRead = guard.dynamic?EntityRegistry::ChunkBytes(guard.snapshot,offset,chunkSize):
                (remaining < chunkSize ? remaining : chunkSize);
            if (!(guard.dynamic?mem.ReadFreshExact(entity_list + offset,chunk.data(),bytesToRead):
                mem.Read(entity_list + offset, chunk.data(), bytesToRead))) { sample.complete=false; continue; }

            for (size_t slotOffset = 0; slotOffset + sizeof(Entity) <= bytesToRead;
                 slotOffset += sizeof(Entity)) {
                if (!RuntimeWorkersRunning() || !ProcessConnection::IsConnected())
                    break;
                uint64_t possibleParent = 0;
                std::memcpy(&possibleParent, chunk.data() + slotOffset, sizeof(possibleParent));
                if (!IsPlausibleUserPointer(possibleParent)) {
                    if(possibleParent!=0)membershipClassified=false;
                    continue;
                }
                if (cnProfile &&
                    !IsLikelyCnEntityParent(
                        possibleParent,
                        entity_list,
                        listReadSize,
                        SDK->dwGameBase,
                        moduleSize)) {
                    membershipClassified=false;
                    continue;
                }
                seen.insert(possibleParent);
            }
        }

        sample.members.assign(seen.begin(),seen.end());
        std::sort(sample.members.begin(),sample.members.end());
        sample.member_ids_complete=readMemberIds && membershipClassified;
        for(size_t begin=0;readMemberIds && begin<sample.members.size() && RuntimeWorkersRunning();begin+=8) {
            if(!guard.Check()){sample.complete=false;sample.member_ids_complete=false;break;}
            const auto count=(std::min)(size_t{8},sample.members.size()-begin);
            std::array<uint32_t,8> ids{};std::array<DWORD,8> bytes{};
            const auto handle=mem.CreateScatterHandle();
            if(!handle){sample.member_ids_complete=false;break;}
            for(size_t i=0;i<count;++i)mem.AddScatterReadRequest(handle,
                sample.members[begin+i]+offset::Entity_MatchId,&ids[i],sizeof(ids[i]),&bytes[i]);
            const bool ok=mem.ExecuteReadScatter(handle);
            mem.CloseScatterHandle(handle);
            if(!ok)sample.member_ids_complete=false;
            for(size_t i=0;i<count;++i)if(bytes[i]!=sizeof(ids[i]))sample.member_ids_complete=false;
            if(ok)for(size_t i=0;i<count;++i)if(bytes[i]==sizeof(ids[i]) && ids[i])
                sample.member_ids.emplace_back(sample.members[begin+i],ids[i]);
            // A mutex is not fair: yielding alone lets the cold scanner reacquire
            // it for every batch and starve the camera for the whole list walk.
            if(begin+count<sample.members.size())RuntimeWorkerSleep(1);
        }
        uint64_t currentRoot=0;
        sample.complete=sample.complete && (guard.dynamic?guard.Check(true):
            (SDK->read_range(SDK->dwGameBase+offset::Active().Address_entity_base,&currentRoot,sizeof(currentRoot)) && currentRoot==entity_list));
        sample.complete=sample.complete && RuntimeWorkersRunning() && ProcessConnection::IsConnected();
        return sample;
    }

    inline size_t ProbeEntityTopologyCandidateCount() { return ProbeEntityTopologyMembers(false).members.size(); }

    inline EntityRosterPolicy::SceneAudit AuditEntityScene(const EntityTopologySample& topology,
        const std::unordered_map<uint64_t,EntityHeaderSnapshot>* headers=nullptr) {
        using Audit = EntityRosterPolicy::SceneAudit;
        if (!topology.complete || !topology.member_ids_complete || !AcceptEntityRegistry(topology.registry))
            return Audit::Unknown;
        EntityRegistryScanGuard guard;
        if (!guard.valid || (guard.dynamic && !guard.snapshot.SameTable(topology.registry))) return Audit::Unknown;
        for (const auto parent : topology.members) {
            if (!guard.Check()) return Audit::Unknown;
            EntityHeaderSnapshot header{};
            if(headers) {
                const auto found=headers->find(parent);
                if(found==headers->end())return Audit::Unknown;
                header=found->second;
            } else if(!header.Read(parent))return Audit::Unknown;
            const auto has = [&](uint32_t type) {
                uint64_t bits = 0;
                return header.ReadParentOffset(0x110 + 8ull * (type >> 6), bits) && (bits & (1ull << (type & 63))) != 0;
            };
            const bool heroComponent=has(offset::Active().typeHeroId);
            const bool actorComponents=has(offset::Active().typeHealth) || has(offset::Active().typeLink) ||
                has(offset::Active().typePlayerController);
            bool knownHero=false,knownDynamic=false;
            if(heroComponent || actorComponents) {
                uint64_t pool=0,id=0;
                if(header.ReadParentOffset(offset::Entity_PoolPtr,pool) && IsPlausibleUserPointer(pool & ~0x3full) &&
                    SDK->read_range((pool & ~0x3full)+offset::Pool_PoolId,&id,sizeof(id)))
                    knownDynamic=id==0x400000000000060 || id==0x40000000000480A ||
                        id==0x40000000000005F || id==0x400000000002533;
            }
            if (heroComponent && !knownDynamic) {
                const auto base = DecryptComponent(parent, offset::Active().typeHeroId, &header);
                hero_compo_t hero{};
                knownHero=base && SDK->read_range(base,&hero,sizeof(hero)) && GameData::IsKnownHeroId(hero.heroid);
            }
            const auto classification=EntityRosterPolicy::ClassifySceneMember(true,heroComponent,knownHero,actorComponents,knownDynamic);
            if(classification!=Audit::Empty)return classification;
        }
        return guard.Check(true) && AcceptEntityRegistry(topology.registry) ? Audit::Empty : Audit::Unknown;
    }

    // Positive discovery only. Full audits retain responsibility for ambiguous
    // objects; an unresolved parent never removes an existing actor.
    inline std::vector<std::pair<uint64_t,uint64_t>> ProbePriorityActor(uint64_t parent,const EntityTopologySample& topology,bool knownLink,
        std::vector<std::pair<uint64_t,uint64_t>>* bindingMembers=nullptr,bool* bindingComplete=nullptr,
        std::vector<EntityRosterPolicy::Discovery>* discoveries=nullptr,
        const std::unordered_map<uint64_t,EntityHeaderSnapshot>* headers=nullptr,
        const std::vector<EntityRosterPolicy::Discovery>* hotActors=nullptr) {
        std::vector<std::pair<uint64_t,uint64_t>> result;
        const DWORD discoveryTick = static_cast<DWORD>(EntityRegistryNowMs());
        if (discoveries) discoveries->clear();
        if(bindingMembers)bindingMembers->clear();
        if(bindingComplete)*bindingComplete=false;
        const auto trace=[&](uint64_t reason,uint64_t component=0) {
            RuntimeTrace::Emit(RuntimeTrace::DiscoveryProbe,parent,reason,component,knownLink);
        };
        if(!offset::IsCnProfile() || !topology.complete ||
            !AcceptEntityRegistry(topology.registry) ||
            !std::binary_search(topology.members.begin(),topology.members.end(),parent))return result;
        EntityHeaderSnapshot header{};
        if(headers && headers->contains(parent))header=headers->at(parent);
        else if(!header.Read(parent)){trace(1);return result;}
        uint32_t match=0;if(!header.ReadParentOffset(offset::Entity_MatchId,match) || !match){trace(2);return result;}
        const auto link=DecryptComponent(parent,offset::Active().typeLink,&header,true);
        std::vector<uint64_t> components;
        std::array<uint32_t,2> linkIds{};
        const bool linkIdRead=link && SDK->read_range(link+offset::Link_TargetId,linkIds.data(),sizeof(linkIds));
        const bool targetIdRead=linkIdRead;
        const uint32_t targetId=linkIds[0],linkId=linkIds[1];
        if(linkIdRead && targetIdRead)
            for(const auto& [component,id]:topology.member_ids)if(id && (id==linkId || id==targetId) && component!=parent)
                components.push_back(component);
        bool allBindingsResolved=topology.member_ids_complete && linkIdRead && targetIdRead;
        if(link)for(auto idOffset:{offset::Link_TargetId,offset::Link_UniqueId}) {
            const auto id=idOffset==offset::Link_TargetId?targetId:linkId;
            if(id && std::any_of(topology.member_ids.begin(),topology.member_ids.end(),[&](const auto& pair){return pair.second==id;}))continue;
            const auto target=ResolveCnLinkTargetMapBase(link,idOffset);
            if(id && !IsPlausibleUserPointer(target) &&
                std::none_of(topology.member_ids.begin(),topology.member_ids.end(),[&](const auto& pair){return pair.second==id;}))
                allBindingsResolved=false;
            if(IsPlausibleUserPointer(target) &&
                std::find(components.begin(),components.end(),target)==components.end())components.push_back(target);
        }
        const auto resolvedComponents=components;
        if(components.empty())components.push_back(parent);
        for(auto component:components) {
            if(!RuntimeWorkersRunning() || !ProcessConnection::IsConnected())break;
            EntityHeaderSnapshot componentHeader{};
            if(component==parent)componentHeader=header;
            else if(headers && headers->contains(component))componentHeader=headers->at(component);
            else if(!componentHeader.Read(component)){trace(3,component);continue;}
            const auto matchedId=std::find_if(topology.member_ids.begin(),topology.member_ids.end(),[&](const auto& pair) {
                return pair.first==component && pair.second && (pair.second==linkId || pair.second==targetId);
            });
            const bool matchedById=matchedId!=topology.member_ids.end();
            const uint32_t expectedId=matchedById?matchedId->second:0;
            uint32_t componentMatch=0;
            if(matchedById && (!componentHeader.ReadParentOffset(offset::Entity_MatchId,componentMatch) || componentMatch!=expectedId)) {
                trace(8,component);continue;
            }
            auto heroBase=DecryptComponent(parent,offset::Active().typeHeroId,&header,true);
            if(!heroBase)heroBase=DecryptComponent(component,offset::Active().typeHeroId,&componentHeader,true);
            hero_compo_t hero{};
            if(!heroBase || !SDK->read_range(heroBase+0xD0,&hero.heroid,sizeof(hero.heroid)) ||
                !GameData::IsKnownHeroId(hero.heroid)){trace(6,component);continue;}
            uint32_t actorMatch=0;
            if(!componentHeader.ReadParentOffset(offset::Entity_MatchId,actorMatch)){trace(2,component);continue;}
            const bool hotBody=hotActors && std::any_of(hotActors->begin(),hotActors->end(),[&](const auto& actor) {
                return actor.address==component && actor.link==parent && actor.match==actorMatch && actor.hero==hero.heroid;
            });
            if(!hotBody) {
                const auto hpBase=DecryptComponent(component,offset::Active().typeHealth,&componentHeader,true);
                const auto velocityBase=DecryptComponent(component,TYPE_VELOCITY,&componentHeader,true);
                if(!hpBase || !velocityBase){trace(4,component);continue;}
                health_compo_t hp{};XMFLOAT3 location{};
                if(!SDK->read_range(hpBase,&hp,sizeof(hp)) || !SDK->read_range(velocityBase+0x200,&location,sizeof(location))) {
                    trace(5,component);continue;
                }
                HealthPayloadSummary health{};
                if(!TryParseHealthPayload(hp,health) || (!discoveries && health.total<=0)){trace(6,component);continue;}
                if(!std::isfinite(location.x)||!std::isfinite(location.y)||!std::isfinite(location.z)||
                    std::fabs(location.x)>100000 || std::fabs(location.y)>100000 || std::fabs(location.z)>100000){trace(7,component);continue;}
            }
            uint32_t currentMatch=0;
            if(!SDK->read_range(parent+offset::Entity_MatchId,&currentMatch,sizeof(currentMatch)) || currentMatch!=match){trace(8,component);continue;}
            if(matchedById) {
                uint32_t currentId=0,currentComponentId=0;
                if(!SDK->read_range(link+offset::Link_UniqueId,&currentId,sizeof(currentId)) || currentId!=linkId ||
                    !SDK->read_range(component+offset::Entity_MatchId,&currentComponentId,sizeof(currentComponentId)) || currentComponentId!=expectedId) {
                    trace(8,component);continue;
                }
            }
            result.emplace_back(component,parent);
            if (discoveries) {
                uint32_t actorMatch = 0;
                if (!componentHeader.ReadParentOffset(offset::Entity_MatchId, actorMatch)) {
                    result.pop_back(); continue;
                }
                if(component!=parent && actorMatch!=linkId && actorMatch!=targetId) {
                    result.pop_back(); continue;
                }
                discoveries->push_back({component, parent, hero.heroid, actorMatch, discoveryTick});
            }
            trace(0,component);
        }
        if(!result.empty()) {
            allBindingsResolved=allBindingsResolved && std::all_of(resolvedComponents.begin(),resolvedComponents.end(),[&](auto component) {
                return std::find(result.begin(),result.end(),std::make_pair(component,parent))!=result.end();
            });
            if(!AcceptEntityRegistry(topology.registry))return {};
            uint32_t currentLinkId=0,currentTargetId=0;
            const bool idsStable=linkIdRead && targetIdRead &&
                SDK->read_range(link+offset::Link_UniqueId,&currentLinkId,sizeof(currentLinkId)) && currentLinkId==linkId &&
                SDK->read_range(link+offset::Link_TargetId,&currentTargetId,sizeof(currentTargetId)) && currentTargetId==targetId;
            if(link && !idsStable){trace(8);return {};}
            allBindingsResolved=allBindingsResolved && idsStable;
            if(allBindingsResolved && bindingMembers)
                for(auto component:resolvedComponents)bindingMembers->emplace_back(component,parent);
            if(bindingComplete)*bindingComplete=allBindingsResolved;
        }
        return result;
    }

    enum class EntityScanPhase { Slots, Headers, Pairing };
    struct EntityScanProgress {
        EntityScanPhase phase;
        const std::vector<std::pair<uint64_t,uint64_t>>* pairs;
    };
    struct EntityScanTask {
        struct promise_type {
            std::vector<std::pair<uint64_t,uint64_t>> result;
            EntityScanProgress progress{EntityScanPhase::Slots,nullptr};
            std::exception_ptr failure;
            EntityScanTask get_return_object() { return EntityScanTask{std::coroutine_handle<promise_type>::from_promise(*this)}; }
            std::suspend_always initial_suspend() noexcept { return {}; }
            std::suspend_always final_suspend() noexcept { return {}; }
            std::suspend_always yield_value(EntityScanProgress value) noexcept { progress=value; return {}; }
            void return_value(std::vector<std::pair<uint64_t,uint64_t>> value) { result=std::move(value); progress.pairs=&result; }
            void unhandled_exception() noexcept { failure=std::current_exception(); }
        };
        std::coroutine_handle<promise_type> handle;
        explicit EntityScanTask(std::coroutine_handle<promise_type> value):handle(value) {}
        EntityScanTask(EntityScanTask&& other) noexcept:handle(std::exchange(other.handle,{})) {}
        EntityScanTask(const EntityScanTask&)=delete;
        ~EntityScanTask() { if(handle)handle.destroy(); }
        bool Done() const { return handle.done(); }
        void Resume() { if(!Done())handle.resume(); if(handle.promise().failure)std::rethrow_exception(handle.promise().failure); }
        const auto& Pairs() const { return handle.promise().progress.pairs ? *handle.promise().progress.pairs : handle.promise().result; }
    };
    // Shared by full scan and priority binding refresh on the acquisition thread.
    inline EntityTopologySample entityScanTopology;
    inline std::unordered_map<uint64_t,EntityHeaderSnapshot> entityScanHeaders;

    inline EntityScanTask BeginEntityScan() {
        entityScanComplete=false;
        entityScanReadComplete=false;
        entityScanStatus=EntityRegistry::ScanStatus::Unavailable;
        entityScanRegistry={};
        SDK->BeginFrame(Diagnostics::SdkFrameSource::Scan);
        const Diagnostics::ScopedDmaCallsite scannerCallsite(Diagnostics::DmaCallsite::EntityScan);
        (void)scannerCallsite;

        std::vector<std::pair<uint64_t, uint64_t>> result;

        struct Entity {
            uint64_t entity;
            uint64_t pad;
        };

        struct EntityScanRecord {
            uint64_t entity = 0;
            uint32_t unique_id = 0;
            uint64_t ptr = 0;
            uint64_t entity_id = 0;
            EntityHeaderSnapshot header{};
        };

        const Diagnostics::DmaReadStats scanDmaStart = Diagnostics::SnapshotDmaReadStats();
        const bool scanDmaRangeDiagEnabled =
            EntityScannerEnvFlagEnabled("UN_DMA_SCAN_DMA_RANGE_DIAG");
        const size_t scanDmaRangeStart =
            scanDmaRangeDiagEnabled ? Diagnostics::DmaSampleCursor() : 0;
        Diagnostics::EntityScanDetailStats scanDetail{};
        scanDetail.scanDmaRangeDiagEnabled = scanDmaRangeDiagEnabled;
        auto elapsedMs = [](std::chrono::steady_clock::time_point startedAt) {
            return static_cast<double>(
                std::chrono::duration_cast<std::chrono::microseconds>(
                    std::chrono::steady_clock::now() - startedAt).count()) / 1000.0;
        };
        auto finishScanDetail = [&]() {
            scanDetail.registryGeneration=entityScanRegistry.generation;
            scanDetail.registryCapacity=entityScanRegistry.capacity;
            scanDetail.plannedBytes=entityScanRegistry.Bytes();
            scanDetail.registryScanStatus=static_cast<uint8_t>(entityScanStatus);
            const auto finalizeStartedAt = std::chrono::steady_clock::now();
            const Diagnostics::DmaReadStats scanDmaEnd = Diagnostics::SnapshotDmaReadStats();
            scanDetail.scanDmaReadsDelta = scanDmaEnd.total >= scanDmaStart.total
                ? scanDmaEnd.total - scanDmaStart.total
                : 0;
            scanDetail.scanDmaFailDelta = scanDmaEnd.failed >= scanDmaStart.failed
                ? scanDmaEnd.failed - scanDmaStart.failed
                : 0;
            if (scanDmaRangeDiagEnabled) {
                const Diagnostics::DmaWindowStats scanDmaRange =
                    Diagnostics::GetDmaRangeStats(
                        scanDmaRangeStart,
                        Diagnostics::DmaSampleCursor());
                scanDetail.scanDmaRangeReads = scanDmaRange.totalReads;
                scanDetail.scanDmaRangeFailed = scanDmaRange.failedReads;
                scanDetail.scanDmaRangeMaxLatencyUs = scanDmaRange.maxLatencyUs;
                uint64_t maxCallsiteLatencyUs = 0;
                int maxCallsite = 0;
                for (int i = 0; i < static_cast<int>(Diagnostics::DmaCallsite::Count); ++i) {
                    if (scanDmaRange.perCallsiteMaxUs[i] > maxCallsiteLatencyUs) {
                        maxCallsiteLatencyUs = scanDmaRange.perCallsiteMaxUs[i];
                        maxCallsite = i;
                    }
                }
                scanDetail.scanDmaRangeMaxCallsite =
                    static_cast<uint8_t>(maxCallsite);
                auto isScannerCallsite = [](Diagnostics::DmaCallsite callsite) {
                    switch (callsite) {
                    case Diagnostics::DmaCallsite::EntityScan:
                    case Diagnostics::DmaCallsite::EntityScanRoot:
                    case Diagnostics::DmaCallsite::EntityScanListRead:
                    case Diagnostics::DmaCallsite::EntityScanRecordBuild:
                    case Diagnostics::DmaCallsite::EntityScanRecordMatchId:
                    case Diagnostics::DmaCallsite::EntityScanRecordHeader:
                    case Diagnostics::DmaCallsite::EntityScanRecordPoolPtr:
                    case Diagnostics::DmaCallsite::EntityScanRecordPoolId:
                    case Diagnostics::DmaCallsite::EntityScanMatchLink:
                    case Diagnostics::DmaCallsite::EntityScanTargetMap:
                    case Diagnostics::DmaCallsite::EntityScanMapCandidate:
                    case Diagnostics::DmaCallsite::EntityScanLinkTargetResolve:
                    case Diagnostics::DmaCallsite::EntityScanSelfValidation:
                    case Diagnostics::DmaCallsite::EntityScanComponentValidation:
                        return true;
                    default:
                        return false;
                    }
                };
                for (int i = 0; i < static_cast<int>(Diagnostics::DmaCallsite::Count); ++i) {
                    const uint64_t reads = scanDmaRange.perCallsiteReads[i];
                    if (reads == 0)
                        continue;
                    const uint64_t maxUs = scanDmaRange.perCallsiteMaxUs[i];
                    const auto callsite = static_cast<Diagnostics::DmaCallsite>(i);
                    if (isScannerCallsite(callsite)) {
                        scanDetail.scanDmaRangeScannerReads += reads;
                        if (maxUs > scanDetail.scanDmaRangeScannerMaxLatencyUs) {
                            scanDetail.scanDmaRangeScannerMaxLatencyUs = maxUs;
                            scanDetail.scanDmaRangeScannerMaxCallsite =
                                static_cast<uint8_t>(i);
                        }
                    } else {
                        scanDetail.scanDmaRangeForeignReads += reads;
                        if (maxUs > scanDetail.scanDmaRangeForeignMaxLatencyUs) {
                            scanDetail.scanDmaRangeForeignMaxLatencyUs = maxUs;
                            scanDetail.scanDmaRangeForeignMaxCallsite =
                                static_cast<uint8_t>(i);
                        }
                    }
                }
                auto callsiteMax = [&](Diagnostics::DmaCallsite callsite) {
                    return scanDmaRange.perCallsiteMaxUs[static_cast<int>(callsite)];
                };
                scanDetail.scanDmaRangeRootMaxUs =
                    callsiteMax(Diagnostics::DmaCallsite::EntityScanRoot);
                scanDetail.scanDmaRangeListReadMaxUs =
                    callsiteMax(Diagnostics::DmaCallsite::EntityScanListRead);
                scanDetail.scanDmaRangeRecordHeaderMaxUs =
                    callsiteMax(Diagnostics::DmaCallsite::EntityScanRecordHeader);
                scanDetail.scanDmaRangeRecordPoolIdMaxUs =
                    callsiteMax(Diagnostics::DmaCallsite::EntityScanRecordPoolId);
                scanDetail.scanDmaRangeTargetMapMaxUs =
                    callsiteMax(Diagnostics::DmaCallsite::EntityScanTargetMap);
                scanDetail.scanDmaRangeComponentValidationMaxUs =
                    callsiteMax(Diagnostics::DmaCallsite::EntityScanComponentValidation);
                scanDetail.scanDmaRangeViewMatrixMaxUs =
                    callsiteMax(Diagnostics::DmaCallsite::ViewMatrix);
            }
            scanDetail.scanFinalizeMs += elapsedMs(finalizeStartedAt);
            Diagnostics::SetEntityScanDetailStats(scanDetail);
        };

        auto isDynamicEntityId = [](uint64_t entity_id) {
            return entity_id == 0x400000000000060 ||
                   entity_id == 0x40000000000480A ||
                   entity_id == 0x40000000000005F ||
                   entity_id == 0x400000000002533;
        };

        const bool cnProfile = offset::IsCnProfile();
        const DWORD rootCacheTtlMs =
            cnProfile && !DynamicEntityRegistryEnabled() ? EntityScannerEntityListRootCacheTtlMs() : 0;
        const bool rootCacheEnabled = cnProfile && rootCacheTtlMs > 0;
        scanDetail.cnEntityListRootCacheEnabled = rootCacheEnabled;
        scanDetail.cnEntityListRootCacheTtlMs = rootCacheTtlMs;
        const DWORD staleMetadataMs =
            cnProfile ? EntityScannerStaleMetadataMs() : 0;
        scanDetail.cnScannerStaleMetadataMs = staleMetadataMs;
        const bool staleMetadataOnly =
            cnProfile &&
            staleMetadataMs > 0 &&
            EntityScannerEnvFlagEnabled("UN_DMA_CN_SCANNER_STALE_METADATA_ONLY");
        scanDetail.cnScannerStaleMetadataOnlyEnabled = staleMetadataOnly;

        static uint64_t cachedCnEntityListRootModuleBase = 0;
        static uint64_t cachedCnEntityListRootPointer = 0;
        static DWORD cachedCnEntityListRootTick = 0;
        static uint64_t cachedRootEpoch = 0;
        EntityRegistryScanGuard registryGuard;
        entityScanRegistry=registryGuard.snapshot;
        if(!registryGuard.valid){finishScanDetail();co_return result;}
        const uint64_t scanEpoch = registryGuard.dynamic?registryGuard.snapshot.generation:ProcessConnection::ConnectionEpoch();
        if (cachedRootEpoch != scanEpoch || (rootCacheEnabled &&
            cachedCnEntityListRootModuleBase != 0 &&
            cachedCnEntityListRootModuleBase != SDK->dwGameBase)) {
            cachedRootEpoch = scanEpoch;
            cachedCnEntityListRootModuleBase = 0;
            cachedCnEntityListRootPointer = 0;
            cachedCnEntityListRootTick = 0;
        }

        const auto rootPhaseStartedAt = std::chrono::steady_clock::now();
        uint64_t entity_list = 0;
        const DWORD rootCacheNowTick = static_cast<DWORD>(EntityRegistryNowMs());
        const bool rootCacheContextMatch =
            rootCacheEnabled &&
            cachedCnEntityListRootModuleBase == SDK->dwGameBase &&
            cachedCnEntityListRootPointer != 0;
        const DWORD rootCacheAgeMs =
            rootCacheContextMatch ?
                (rootCacheNowTick - cachedCnEntityListRootTick) : 0;
        if(registryGuard.dynamic) {
            entity_list=registryGuard.snapshot.root;
        } else if (rootCacheContextMatch &&
            rootCacheAgeMs <= rootCacheTtlMs) {
            entity_list = cachedCnEntityListRootPointer;
            scanDetail.cnEntityListRootCacheHitCount = 1;
        } else if (
            rootCacheContextMatch &&
            staleMetadataMs > 0 &&
            (staleMetadataOnly ||
             static_cast<uint64_t>(rootCacheAgeMs) <=
                static_cast<uint64_t>(rootCacheTtlMs) + staleMetadataMs)) {
            entity_list = cachedCnEntityListRootPointer;
            scanDetail.cnEntityListRootCacheExpiredCount = 1;
            scanDetail.cnEntityListRootCacheStaleHitCount = 1;
        } else {
            if (rootCacheContextMatch) {
                scanDetail.cnEntityListRootCacheExpiredCount = 1;
            }
            scanDetail.cnEntityListRootCacheReadCount = 1;
            const Diagnostics::ScopedDmaCallsite rootCallsite(
                Diagnostics::DmaCallsite::EntityScanRoot);
            entity_list = SDK->RPM<uint64_t>(
                SDK->dwGameBase + offset::Active().Address_entity_base);
            if (rootCacheEnabled && entity_list != 0) {
                cachedCnEntityListRootModuleBase = SDK->dwGameBase;
                cachedCnEntityListRootPointer = entity_list;
                cachedCnEntityListRootTick = rootCacheNowTick;
                scanDetail.cnEntityListRootCacheStoreCount = 1;
            }
        }
        scanDetail.entityList = entity_list;
        if (!entity_list) {
            scanDetail.scanRootMs += elapsedMs(rootPhaseStartedAt);
            finishScanDetail();
            Diagnostics::Trace("Entity scan skipped: entity list pointer is null.");
            co_return result;
        }

        std::vector<uint64_t> candidateParents;
        struct RecordMetadata { uint64_t plus8=0, poolId=0; uint32_t wrapperId=0; bool poolRead=false; };
        std::unordered_map<uint64_t,RecordMetadata> recordMetadata;
        std::vector<EntityScanRecord> records{};
        std::unordered_map<uint32_t, std::vector<uint64_t>> match_index{};
        std::unordered_set<uint64_t> seen_entities{};
        records.reserve(4096);
        match_index.reserve(4096);
        seen_entities.reserve(4096);

        const size_t kEntityListReadSize =
            registryGuard.dynamic ? registryGuard.snapshot.Bytes() : (cnProfile ? 0x40000 : 0x1E000);
        const size_t kEntityListChunkSize = 0x1000;
        scanDetail.cnEntityListChunkSize = cnProfile ? kEntityListChunkSize : 0;
        constexpr size_t kEntityListFallbackChunkSize = 0x1000;
        const bool lightScanRequested = EntityLightScanRequested();
        const bool lightScan = lightScanRequested;
        const size_t lightScanMaxCandidates =
            lightScan ? EntityLightScanMaxCandidates() : 0;
        const bool cnMapCandidateCacheEnabled = cnProfile;
        scanDetail.cnMapCandidateCacheEnabled = cnMapCandidateCacheEnabled;
        const bool cnMapDiagEnabled =
            cnProfile && EntityScannerEnvFlagEnabled("UN_DMA_CN_MAP_DIAG");
        scanDetail.cnMapDiagEnabled = cnMapDiagEnabled;
        const bool cnRecordMatchIdFromHeaderEnabled = cnProfile;
        scanDetail.cnRecordMatchIdFromHeaderEnabled = cnRecordMatchIdFromHeaderEnabled;
        static uint64_t cachedModuleBase = 0;
        static uint64_t cachedModuleSize = 0;
        static uint64_t cachedModuleEpoch = 0;
        if (cnProfile && (cachedModuleEpoch != scanEpoch || cachedModuleBase != SDK->dwGameBase)) {
            cachedModuleEpoch = scanEpoch;
            cachedModuleBase = SDK->dwGameBase;
            cachedModuleSize = static_cast<uint64_t>(mem.GetBaseSize("Overwatch.exe"));
        }
        const uint64_t moduleSize = cnProfile ? cachedModuleSize : 0;
        scanDetail.scanRootMs += elapsedMs(rootPhaseStartedAt);

        struct PersistentCnRecordSnapshotCacheEntry {
            uint64_t entityList = 0;
            uint64_t moduleBase = 0;
            uint64_t moduleSize = 0;
            DWORD updateTick = 0;
            EntityScanRecord record{};
        };
        static std::unordered_map<uint64_t, PersistentCnRecordSnapshotCacheEntry>
            persistentCnRecordSnapshotCache{};
        static uint64_t persistentCnRecordSnapshotEpoch = 0;
        if (persistentCnRecordSnapshotEpoch != scanEpoch) {
            persistentCnRecordSnapshotCache.clear();
            persistentCnRecordSnapshotEpoch = scanEpoch;
        }
        static uint64_t persistentCnRecordSnapshotEntityList = 0;
        static uint64_t persistentCnRecordSnapshotModuleBase = 0;
        static uint64_t persistentCnRecordSnapshotModuleSize = 0;
        const DWORD recordSnapshotCacheTtlMs =
            cnProfile ? EntityScannerRecordSnapshotCacheTtlMs() : 0;
        const bool recordSnapshotCacheEnabled =
            cnProfile && recordSnapshotCacheTtlMs > 0;
        const size_t recordSnapshotCacheRefreshBudget =
            recordSnapshotCacheEnabled ? EntityScannerRecordSnapshotCacheRefreshBudget() : 0;
        const bool recordSnapshotCacheRefreshBudgetEnabled =
            recordSnapshotCacheRefreshBudget > 0;
        size_t recordSnapshotCacheRefreshCount = 0;
        scanDetail.cnRecordSnapshotCacheEnabled = recordSnapshotCacheEnabled;
        scanDetail.cnRecordSnapshotCacheTtlMs = recordSnapshotCacheTtlMs;
        scanDetail.cnRecordSnapshotCacheRefreshBudget =
            recordSnapshotCacheRefreshBudget;
        if (recordSnapshotCacheEnabled &&
            (persistentCnRecordSnapshotEntityList != entity_list ||
             persistentCnRecordSnapshotModuleBase != SDK->dwGameBase ||
             persistentCnRecordSnapshotModuleSize != moduleSize)) {
            persistentCnRecordSnapshotCache.clear();
            persistentCnRecordSnapshotEntityList = entity_list;
            persistentCnRecordSnapshotModuleBase = SDK->dwGameBase;
            persistentCnRecordSnapshotModuleSize = moduleSize;
        }
        if (recordSnapshotCacheEnabled &&
            persistentCnRecordSnapshotCache.size() > 8192) {
            persistentCnRecordSnapshotCache.clear();
        }

        static std::unordered_map<uint64_t, DWORD>
            persistentCnEntityListReadNegativeCache{};
        static uint64_t persistentCnEntityListReadNegativeCacheEpoch = 0;
        if (persistentCnEntityListReadNegativeCacheEpoch != scanEpoch) {
            persistentCnEntityListReadNegativeCache.clear();
            persistentCnEntityListReadNegativeCacheEpoch = scanEpoch;
        }
        static uint64_t persistentCnEntityListReadNegativeCacheEntityList = 0;
        static uint64_t persistentCnEntityListReadNegativeCacheModuleBase = 0;
        const DWORD listReadNegativeCacheTtlMs =
            cnProfile ? EntityScannerEntityListReadNegativeCacheTtlMs() : 0;
        const bool listReadNegativeCacheEnabled =
            cnProfile && listReadNegativeCacheTtlMs > 0;
        scanDetail.cnEntityListReadNegativeCacheEnabled =
            listReadNegativeCacheEnabled;
        scanDetail.cnEntityListReadNegativeCacheTtlMs =
            listReadNegativeCacheTtlMs;
        if (listReadNegativeCacheEnabled &&
            (persistentCnEntityListReadNegativeCacheEntityList != entity_list ||
             persistentCnEntityListReadNegativeCacheModuleBase != SDK->dwGameBase)) {
            persistentCnEntityListReadNegativeCache.clear();
            persistentCnEntityListReadNegativeCacheEntityList = entity_list;
            persistentCnEntityListReadNegativeCacheModuleBase = SDK->dwGameBase;
        }
        if (listReadNegativeCacheEnabled &&
            persistentCnEntityListReadNegativeCache.size() > 512) {
            persistentCnEntityListReadNegativeCache.clear();
        }
        auto entityListReadNegativeCacheKey = [](size_t offset, size_t size) {
            return static_cast<uint64_t>(offset) ^
                (static_cast<uint64_t>(size) << 32);
        };
        auto shouldSkipEntityListRead = [&](size_t offset, size_t size) {
            if (!listReadNegativeCacheEnabled)
                return false;
            ++scanDetail.cnEntityListReadNegativeCacheLookupCount;
            const uint64_t key = entityListReadNegativeCacheKey(offset, size);
            auto it = persistentCnEntityListReadNegativeCache.find(key);
            if (it == persistentCnEntityListReadNegativeCache.end())
                return false;
            const DWORD now = static_cast<DWORD>(EntityRegistryNowMs());
            const DWORD ageMs = now - it->second;
            const bool fresh = ageMs <= listReadNegativeCacheTtlMs;
            const bool staleAllowed =
                !fresh &&
                staleMetadataMs > 0 &&
                (staleMetadataOnly ||
                 static_cast<uint64_t>(ageMs) <=
                    static_cast<uint64_t>(listReadNegativeCacheTtlMs) +
                        staleMetadataMs);
            if (fresh || staleAllowed) {
                ++scanDetail.cnEntityListReadNegativeCacheHitCount;
                if (staleAllowed)
                    ++scanDetail.cnEntityListReadNegativeCacheStaleHitCount;
                ++scanDetail.listReadSkippedCount;
                return true;
            }
            ++scanDetail.cnEntityListReadNegativeCacheExpiredCount;
            persistentCnEntityListReadNegativeCache.erase(it);
            return false;
        };
        auto rememberEntityListReadFailure = [&](size_t offset, size_t size) {
            if (!listReadNegativeCacheEnabled)
                return;
            persistentCnEntityListReadNegativeCache.insert_or_assign(
                entityListReadNegativeCacheKey(offset, size),
                static_cast<DWORD>(EntityRegistryNowMs()));
            ++scanDetail.cnEntityListReadNegativeCacheStoreCount;
        };

        struct PersistentCnEntityListReadCacheEntry {
            DWORD updateTick = 0;
            std::vector<uint8_t> bytes{};
        };
        static std::unordered_map<uint64_t, PersistentCnEntityListReadCacheEntry>
            persistentCnEntityListReadCache{};
        static uint64_t persistentCnEntityListReadCacheEpoch = 0;
        if (persistentCnEntityListReadCacheEpoch != scanEpoch) {
            persistentCnEntityListReadCache.clear();
            persistentCnEntityListReadCacheEpoch = scanEpoch;
        }
        static uint64_t persistentCnEntityListReadCacheEntityList = 0;
        static uint64_t persistentCnEntityListReadCacheModuleBase = 0;
        const DWORD listReadCacheTtlMs =
            cnProfile ? EntityScannerEntityListReadCacheTtlMs() : 0;
        const bool listReadCacheEnabled =
            cnProfile && listReadCacheTtlMs > 0;
        scanDetail.cnEntityListReadCacheEnabled = listReadCacheEnabled;
        scanDetail.cnEntityListReadCacheTtlMs = listReadCacheTtlMs;
        if (listReadCacheEnabled &&
            (persistentCnEntityListReadCacheEntityList != entity_list ||
             persistentCnEntityListReadCacheModuleBase != SDK->dwGameBase)) {
            persistentCnEntityListReadCache.clear();
            persistentCnEntityListReadCacheEntityList = entity_list;
            persistentCnEntityListReadCacheModuleBase = SDK->dwGameBase;
        }
        if (listReadCacheEnabled &&
            persistentCnEntityListReadCache.size() > 512) {
            persistentCnEntityListReadCache.clear();
        }
        auto loadEntityListReadCache =
            [&](size_t offset, size_t size, std::vector<uint8_t>& destination) {
                if (!listReadCacheEnabled)
                    return false;
                ++scanDetail.cnEntityListReadCacheLookupCount;
                const uint64_t key = entityListReadNegativeCacheKey(offset, size);
                auto it = persistentCnEntityListReadCache.find(key);
                if (it == persistentCnEntityListReadCache.end())
                    return false;

                const DWORD now = static_cast<DWORD>(EntityRegistryNowMs());
                PersistentCnEntityListReadCacheEntry& entry = it->second;
                const DWORD ageMs = now - entry.updateTick;
                if (entry.bytes.size() != size) {
                    ++scanDetail.cnEntityListReadCacheExpiredCount;
                    persistentCnEntityListReadCache.erase(it);
                    return false;
                }

                if (ageMs > listReadCacheTtlMs) {
                    ++scanDetail.cnEntityListReadCacheExpiredCount;
                    if (staleMetadataMs == 0 ||
                        (!staleMetadataOnly &&
                         static_cast<uint64_t>(ageMs) >
                            static_cast<uint64_t>(listReadCacheTtlMs) +
                                staleMetadataMs)) {
                        persistentCnEntityListReadCache.erase(it);
                        return false;
                    }

                    std::memcpy(destination.data(), entry.bytes.data(), size);
                    ++scanDetail.cnEntityListReadCacheHitCount;
                    ++scanDetail.cnEntityListReadCacheStaleHitCount;
                    return true;
                }

                std::memcpy(destination.data(), entry.bytes.data(), size);
                ++scanDetail.cnEntityListReadCacheHitCount;
                return true;
            };
        auto rememberEntityListReadSuccess =
            [&](size_t offset, size_t size, const std::vector<uint8_t>& source) {
                if (!listReadCacheEnabled)
                    return;
                PersistentCnEntityListReadCacheEntry entry{};
                entry.updateTick = static_cast<DWORD>(EntityRegistryNowMs());
                entry.bytes.assign(source.begin(), source.begin() + size);
                persistentCnEntityListReadCache.insert_or_assign(
                    entityListReadNegativeCacheKey(offset, size),
                    std::move(entry));
                ++scanDetail.cnEntityListReadCacheStoreCount;
            };

        auto addRecord = [&](uint64_t possible_common) {
            const auto recordBuildStartedAt = std::chrono::steady_clock::now();
            const Diagnostics::ScopedDmaCallsite recordBuildCallsite(
                Diagnostics::DmaCallsite::EntityScanRecordBuild);
            ++scanDetail.recordAddAttemptCount;
            auto finishRecordBuild = [&]() {
                scanDetail.scanRecordBuildMs += elapsedMs(recordBuildStartedAt);
            };
            if (!possible_common)
            {
                finishRecordBuild();
                return;
            }
            if (cnProfile &&
                !IsLikelyCnEntityParent(
                    possible_common,
                    entity_list,
                    kEntityListReadSize,
                    SDK->dwGameBase,
                    moduleSize)) {
                finishRecordBuild();
                return;
            }

            if (recordSnapshotCacheEnabled) {
                ++scanDetail.cnRecordSnapshotCacheLookupCount;
                const DWORD now = static_cast<DWORD>(EntityRegistryNowMs());
                auto snapshotIt = persistentCnRecordSnapshotCache.find(possible_common);
                if (snapshotIt != persistentCnRecordSnapshotCache.end()) {
                    const PersistentCnRecordSnapshotCacheEntry& entry =
                        snapshotIt->second;
                    const bool sameContext =
                        entry.entityList == entity_list &&
                        entry.moduleBase == SDK->dwGameBase &&
                        entry.moduleSize == moduleSize;
                    const DWORD ageMs = now - entry.updateTick;
                    const bool fresh = sameContext && ageMs <= recordSnapshotCacheTtlMs;
                    const bool staleMetadataAllowed =
                        sameContext &&
                        !fresh &&
                        staleMetadataMs > 0 &&
                        (staleMetadataOnly || ageMs <= staleMetadataMs);
                    const bool staleAllowed =
                        staleMetadataAllowed ||
                        (sameContext &&
                         !fresh &&
                         recordSnapshotCacheRefreshBudgetEnabled &&
                         recordSnapshotCacheRefreshCount >= recordSnapshotCacheRefreshBudget);
                    if (fresh || staleAllowed) {
                        EntityScanRecord cached = entry.record;
                        if (cached.entity == possible_common && cached.unique_id != 0) {
                            if (!seen_entities.insert(possible_common).second) {
                                ++scanDetail.recordDuplicateCount;
                                finishRecordBuild();
                                return;
                            }
                            ++scanDetail.cnRecordSnapshotCacheHitCount;
                            if (staleAllowed)
                                ++scanDetail.cnRecordSnapshotCacheStaleHitCount;
                            match_index[cached.unique_id].push_back(cached.entity);
                            records.push_back(std::move(cached));
                            finishRecordBuild();
                            return;
                        }
                    }
                    if (sameContext)
                        ++scanDetail.cnRecordSnapshotCacheExpiredCount;
                    persistentCnRecordSnapshotCache.erase(snapshotIt);
                }
            }
            if (recordSnapshotCacheEnabled) {
                ++recordSnapshotCacheRefreshCount;
                ++scanDetail.cnRecordSnapshotCacheRefreshCount;
            }

            EntityScanRecord record{};
            record.entity = possible_common;
            bool recordHeaderLoaded = false;
            auto readRecordHeader = [&]() {
                if (recordHeaderLoaded)
                    return;
                ++scanDetail.recordHeaderReadCount;
                {
                    const Diagnostics::ScopedDmaCallsite headerCallsite(
                        Diagnostics::DmaCallsite::EntityScanRecordHeader);
                    const auto loaded=entityScanHeaders.find(possible_common);
                    if(loaded!=entityScanHeaders.end())record.header=loaded->second;
                    else record.header.Read(possible_common);
                }
                if (!record.header.valid)
                    ++scanDetail.recordHeaderFailCount;
                recordHeaderLoaded = true;
            };

            bool headerMatchIdObserved = false;
            bool headerHasUniqueId = false;
            uint32_t headerUniqueId = 0;
            auto observeHeaderMatchId = [&](uint32_t compareValue, bool haveCompareValue) {
                if (headerMatchIdObserved)
                    return;
                headerHasUniqueId =
                    record.header.ReadParentOffset(offset::Entity_MatchId, headerUniqueId);
                if (headerHasUniqueId) {
                    ++scanDetail.recordMatchIdHeaderHitCount;
                    if (haveCompareValue) {
                        if (headerUniqueId == compareValue)
                            ++scanDetail.recordMatchIdHeaderMatchCount;
                        else
                            ++scanDetail.recordMatchIdHeaderMismatchCount;
                    }
                } else {
                    ++scanDetail.recordMatchIdHeaderMissCount;
                }
                headerMatchIdObserved = true;
            };

            uint32_t cnUniqueId = 0;
            if (cnProfile && cnRecordMatchIdFromHeaderEnabled) {
                readRecordHeader();
                observeHeaderMatchId(0, false);
                if (headerHasUniqueId && headerUniqueId != 0) {
                    cnUniqueId = headerUniqueId;
                    ++scanDetail.recordMatchIdHeaderUseCount;
                } else {
                    ++scanDetail.recordRemoteFallbackReadCount;
                    ++scanDetail.recordMatchIdDirectReadCount;
                    {
                        const Diagnostics::ScopedDmaCallsite matchIdCallsite(
                            Diagnostics::DmaCallsite::EntityScanRecordMatchId);
                        cnUniqueId = SDK->RPM<uint32_t>(
                            possible_common + offset::Entity_MatchId);
                    }
                    if (cnUniqueId == 0) {
                        ++scanDetail.recordMatchIdDirectZeroCount;
                        finishRecordBuild();
                        return;
                    }
                }
            } else if (cnProfile) {
                ++scanDetail.recordRemoteFallbackReadCount;
                ++scanDetail.recordMatchIdDirectReadCount;
                {
                    const Diagnostics::ScopedDmaCallsite matchIdCallsite(
                        Diagnostics::DmaCallsite::EntityScanRecordMatchId);
                    cnUniqueId = SDK->RPM<uint32_t>(
                        possible_common + offset::Entity_MatchId);
                }
                if (cnUniqueId == 0) {
                    ++scanDetail.recordMatchIdDirectZeroCount;
                    finishRecordBuild();
                    return;
                }
            }

            if (!seen_entities.insert(possible_common).second) {
                ++scanDetail.recordDuplicateCount;
                finishRecordBuild();
                return;
            }

            readRecordHeader();
            observeHeaderMatchId(cnUniqueId, cnProfile && cnUniqueId != 0);

            if (cnProfile) {
                record.unique_id = cnUniqueId;
            } else if (headerHasUniqueId) {
                record.unique_id = headerUniqueId;
            } else {
                ++scanDetail.recordRemoteFallbackReadCount;
                ++scanDetail.recordMatchIdDirectReadCount;
                const Diagnostics::ScopedDmaCallsite matchIdCallsite(
                    Diagnostics::DmaCallsite::EntityScanRecordMatchId);
                record.unique_id = SDK->RPM<uint32_t>(possible_common + offset::Entity_MatchId);
                if (record.unique_id == 0)
                    ++scanDetail.recordMatchIdDirectZeroCount;
            }

            uint64_t ptr_value = 0;
            if (!record.header.ReadParentOffset(offset::Entity_PoolPtr, ptr_value)) {
                ++scanDetail.recordRemoteFallbackReadCount;
                const Diagnostics::ScopedDmaCallsite poolPtrCallsite(
                    Diagnostics::DmaCallsite::EntityScanRecordPoolPtr);
                ptr_value = SDK->RPM<uint64_t>(possible_common + offset::Entity_PoolPtr);
            }

            record.ptr = ptr_value & 0xFFFFFFFFFFFFFFC0;
            if (record.ptr && record.ptr < 0xFFFFFFFFFFFFFFEF) {
                ++scanDetail.poolIdReadCount;
                const Diagnostics::ScopedDmaCallsite poolIdCallsite(
                    Diagnostics::DmaCallsite::EntityScanRecordPoolId);
                const auto metadata=recordMetadata.find(possible_common);
                record.entity_id = metadata!=recordMetadata.end() && metadata->second.poolRead ? metadata->second.poolId :
                    SDK->RPM<uint64_t>(record.ptr + offset::Pool_PoolId);
            }

            if (record.unique_id != 0)
                match_index[record.unique_id].push_back(record.entity);
            if (recordSnapshotCacheEnabled && record.unique_id != 0) {
                PersistentCnRecordSnapshotCacheEntry entry{};
                entry.entityList = entity_list;
                entry.moduleBase = SDK->dwGameBase;
                entry.moduleSize = moduleSize;
                entry.updateTick = static_cast<DWORD>(EntityRegistryNowMs());
                entry.record = record;
                persistentCnRecordSnapshotCache.insert_or_assign(
                    record.entity,
                    entry);
                ++scanDetail.cnRecordSnapshotCacheStoreCount;
            }
            records.push_back(std::move(record));
            finishRecordBuild();
        };

        std::vector<uint8_t> entity_chunk(kEntityListChunkSize);
        size_t readable_bytes = 0;
        size_t readable_chunks = 0;
        size_t slots_scanned = 0;
        size_t nonzero_slots = 0;
        size_t plausible_slots = 0;
        auto processEntityChunk = [&](const std::vector<uint8_t>& chunk,
                                      size_t chunk_size) {
            for (size_t slot_offset = 0; slot_offset + sizeof(Entity) <= chunk_size;
                 slot_offset += sizeof(Entity)) {
                if (!RuntimeWorkersRunning() || !ProcessConnection::IsConnected() || !registryGuard.Check())
                    break;
                const auto slotWalkStartedAt = std::chrono::steady_clock::now();
                uint64_t possible_common = 0;
                std::memcpy(&possible_common, chunk.data() + slot_offset, sizeof(possible_common));
                ++slots_scanned;
                if (possible_common)
                    ++nonzero_slots;
                if (IsPlausibleUserPointer(possible_common))
                    ++plausible_slots;
                scanDetail.scanSlotWalkMs += elapsedMs(slotWalkStartedAt);
                if(IsPlausibleUserPointer(possible_common) && (!cnProfile || IsLikelyCnEntityParent(
                    possible_common,entity_list,kEntityListReadSize,SDK->dwGameBase,moduleSize)))
                    candidateParents.push_back(possible_common);
            }
        };

        for (size_t offset = 0; offset < kEntityListReadSize; offset += kEntityListChunkSize) {
            co_yield EntityScanProgress{EntityScanPhase::Slots, &result};
            if (!RuntimeWorkersRunning() || !ProcessConnection::IsConnected() || !registryGuard.Check())
                break;
            const size_t remaining = kEntityListReadSize - offset;
            const size_t chunk_size =
                registryGuard.dynamic?EntityRegistry::ChunkBytes(registryGuard.snapshot,offset,kEntityListChunkSize):
                (remaining < kEntityListChunkSize ? remaining : kEntityListChunkSize);
            bool chunkRead = !registryGuard.dynamic && loadEntityListReadCache(offset, chunk_size, entity_chunk);
            const bool chunkSkipped = !registryGuard.dynamic && !chunkRead && shouldSkipEntityListRead(offset, chunk_size);
            if (!chunkSkipped) {
                if (!chunkRead) {
                    ++scanDetail.listReadCount;
                    const auto listReadStartedAt = std::chrono::steady_clock::now();
                    {
                        const Diagnostics::ScopedDmaCallsite listReadCallsite(
                            Diagnostics::DmaCallsite::EntityScanListRead);
                        chunkRead = registryGuard.dynamic ? mem.ReadFreshExact(entity_list + offset, entity_chunk.data(), chunk_size) : mem.Read(entity_list + offset, entity_chunk.data(), chunk_size);
                    }
                    scanDetail.scanListReadMs += elapsedMs(listReadStartedAt);
                    if (chunkRead)
                        rememberEntityListReadSuccess(offset, chunk_size, entity_chunk);
                }
            }
            if (chunkRead) {
                readable_bytes += chunk_size;
                ++readable_chunks;
                processEntityChunk(entity_chunk, chunk_size);
                continue;
            }
            if (!chunkSkipped) {
                ++scanDetail.listReadFailCount;
                rememberEntityListReadFailure(offset, chunk_size);
            }

            if (!cnProfile || kEntityListFallbackChunkSize >= chunk_size)
                continue;

            for (size_t sub_offset = 0; sub_offset < chunk_size;
                 sub_offset += kEntityListFallbackChunkSize) {
                if (!RuntimeWorkersRunning() || !ProcessConnection::IsConnected() || !registryGuard.Check())
                    break;
                const size_t sub_remaining = chunk_size - sub_offset;
                const size_t sub_size =
                    sub_remaining < kEntityListFallbackChunkSize
                        ? sub_remaining
                        : kEntityListFallbackChunkSize;
                const size_t absoluteSubOffset = offset + sub_offset;
                bool subChunkRead =
                    !registryGuard.dynamic && loadEntityListReadCache(absoluteSubOffset, sub_size, entity_chunk);
                const bool subChunkSkipped = subChunkRead || registryGuard.dynamic
                    ? false
                    : shouldSkipEntityListRead(absoluteSubOffset, sub_size);
                if (!subChunkSkipped) {
                    if (!subChunkRead) {
                        ++scanDetail.listReadCount;
                        ++scanDetail.listFallbackReadCount;
                        const auto subListReadStartedAt = std::chrono::steady_clock::now();
                        {
                            const Diagnostics::ScopedDmaCallsite listReadCallsite(
                                Diagnostics::DmaCallsite::EntityScanListRead);
                            subChunkRead = mem.ReadFreshExact(entity_list + absoluteSubOffset,
                                    entity_chunk.data(),
                                    sub_size);
                        }
                        scanDetail.scanListReadMs += elapsedMs(subListReadStartedAt);
                        if (subChunkRead)
                            rememberEntityListReadSuccess(
                                absoluteSubOffset,
                                sub_size,
                                entity_chunk);
                    }
                }
                if (!subChunkRead) {
                    if (!subChunkSkipped) {
                        ++scanDetail.listReadFailCount;
                        rememberEntityListReadFailure(absoluteSubOffset, sub_size);
                    }
                    continue;
                }
                readable_bytes += sub_size;
                ++readable_chunks;
                processEntityChunk(entity_chunk, sub_size);
            }
        }

        entityScanHeaders.clear();
        struct ScanHandle {
            VMMDLL_SCATTER_HANDLE value=mem.CreateScatterHandle();
            ~ScanHandle(){if(value)mem.CloseScatterHandle(value);}
        } scanHandle;
        struct HeaderRead {
            std::array<uint8_t,0x150> data{};
            uint32_t wrapperId=0;
            uint64_t poolId=0;
            DWORD bytes=0,wrapperBytes=0,poolBytes=0;
        };
        std::array<HeaderRead,16> headers{};
        bool recordReadsComplete=true;
        for(size_t first=0;first<candidateParents.size();first+=headers.size()) {
            co_yield EntityScanProgress{EntityScanPhase::Headers, &result};
            if(!registryGuard.Check() || !scanHandle.value) break;
            headers={};
            const size_t count=(std::min)(headers.size(),candidateParents.size()-first);
            for(size_t i=0;i<count;++i) {
                const auto parent=candidateParents[first+i];auto& read=headers[i];
                mem.AddScatterReadRequest(scanHandle.value,parent,read.data.data(),read.data.size(),&read.bytes);
                mem.AddScatterReadRequest(scanHandle.value,parent+0x2F0,&read.wrapperId,sizeof(read.wrapperId),&read.wrapperBytes);
            }
            const bool readOk=mem.ExecuteReadScatter(scanHandle.value);
            for(size_t i=0;i<count;++i) {
                auto& read=headers[i];const auto parent=candidateParents[first+i];
                if(!readOk || read.bytes!=read.data.size()) { recordReadsComplete=false;continue; }
                EntityHeaderSnapshot snapshot;snapshot.parent=parent;snapshot.valid=true;
                snapshot.range.Assign(parent,read.data.data(),read.data.size());
                uint64_t pool=0;snapshot.ReadParentOffset(offset::Entity_PoolPtr,pool);pool&=~0x3Full;
                if(IsPlausibleUserPointer(pool))mem.AddScatterReadRequest(scanHandle.value,pool+offset::Pool_PoolId,
                    &read.poolId,sizeof(read.poolId),&read.poolBytes);
                entityScanHeaders.insert_or_assign(parent,std::move(snapshot));
            }
            const bool poolOk=mem.ExecuteReadScatter(scanHandle.value);
            for(size_t i=0;i<count;++i) {
                const auto parent=candidateParents[first+i];auto& read=headers[i];
                auto& meta=recordMetadata[parent];
                std::memcpy(&meta.plus8,read.data.data()+8,sizeof(meta.plus8));
                meta.wrapperId=read.wrapperId;

                meta.poolId=read.poolId;meta.poolRead=poolOk && read.poolBytes==sizeof(read.poolId);
                if(entityScanHeaders.contains(parent))addRecord(parent);
            }
        }
        entityScanTopology={};
        entityScanTopology.registry=registryGuard.snapshot;
        entityScanTopology.root=entity_list;
        entityScanTopology.complete=readable_bytes==kEntityListReadSize;
        entityScanTopology.member_ids_complete=entityScanTopology.complete && recordReadsComplete;
        entityScanTopology.members=candidateParents;
        for(const auto& record:records) {
            if(record.unique_id)entityScanTopology.member_ids.emplace_back(record.entity,record.unique_id);
            if(record.header.valid)entityScanHeaders.emplace(record.entity,record.header);
            else entityScanTopology.member_ids_complete=false;
        }
        std::sort(entityScanTopology.members.begin(),entityScanTopology.members.end());
        entityScanTopology.members.erase(std::unique(entityScanTopology.members.begin(),entityScanTopology.members.end()),entityScanTopology.members.end());
        entityScanComplete = recordReadsComplete && readable_bytes == kEntityListReadSize && registryGuard.Check(true);
        entityScanReadComplete=entityScanComplete;
        scanDetail.readableBytes = readable_bytes;
        scanDetail.readableChunks = readable_chunks;
        scanDetail.slotsScanned = slots_scanned;
        scanDetail.nonZeroSlots = nonzero_slots;
        scanDetail.plausibleSlots = plausible_slots;
        scanDetail.records = records.size();
        scanDetail.matchIds = match_index.size();
        scanDetail.lightScanRequested = lightScanRequested;
        scanDetail.lightScanEnabled = lightScan;
        scanDetail.lightScanPairCap = lightScanMaxCandidates;

        if (records.empty()) {
            if(nonzero_slots!=0)entityScanComplete=false;
            entityScanStatus=!registryGuard.valid?EntityRegistry::ScanStatus::Invalidated:
                (entityScanComplete?EntityRegistry::ScanStatus::Complete:EntityRegistry::ScanStatus::Partial);
            Diagnostics::Trace("Entity scan skipped: no records from list=0x%llX chunks=%zu bytes=%zu.",
                static_cast<unsigned long long>(entity_list),
                readable_chunks,
                readable_bytes);
            finishScanDetail();
            co_return result;
        }

        auto addPair = [&](uint64_t component_parent,
                           uint64_t link_parent,
                           bool lightCandidate = false) {
            ++scanDetail.addPairAttemptCount;
            if (!component_parent || !link_parent)
                return false;
            if (lightCandidate &&
                lightScanMaxCandidates != 0 &&
                result.size() >= lightScanMaxCandidates) {
                ++scanDetail.lightScanCapHits;
                return false;
            }
            const auto pair = std::make_pair(component_parent, link_parent);
            if (std::find(result.begin(), result.end(), pair) != result.end()) {
                ++scanDetail.addPairDuplicateCount;
                return false;
            }
            result.push_back(pair);
            if (lightCandidate)
                ++scanDetail.lightScanUnvalidatedPairs;
            return true;
        };

        auto hasPlayableComponents = [&](uint64_t component_parent) {
            ++scanDetail.playableValidationAttemptCount;
            if (!component_parent)
                return false;
            const uint64_t health =
                DecryptComponent(component_parent, offset::Active().typeHealth);
            if (!health)
                return false;
            const uint64_t velocity =
                DecryptComponent(component_parent, TYPE_VELOCITY);
            if (!velocity)
                return false;
            ++scanDetail.playableValidationSuccessCount;
            return true;
        };

        auto hasCnPlayableComponents = [&](const EntityScanRecord& current) {
            const auto validationStartedAt = std::chrono::steady_clock::now();
            const Diagnostics::ScopedDmaCallsite validationCallsite(
                Diagnostics::DmaCallsite::EntityScanSelfValidation);
            ++scanDetail.playableValidationAttemptCount;
            auto finishValidation = [&]() {
                scanDetail.scanCnSelfValidationMs += elapsedMs(validationStartedAt);
            };
            if (!current.entity)
            {
                finishValidation();
                return false;
            }
            const EntityHeaderSnapshot* snapshot =
                current.header.valid ? &current.header : nullptr;
            auto rememberReject = [&](int reason,
                                      uint64_t healthBase,
                                      uint64_t heroBase,
                                      uint64_t heroId,
                                      uint64_t velocityBase,
                                      uint64_t boneBase,
                                      float healthValue,
                                      float healthMaxValue) {
                if (scanDetail.sampleRejectParent != 0)
                    return;
                scanDetail.sampleRejectReason = reason;
                scanDetail.sampleRejectParent = current.entity;
                scanDetail.sampleRejectMatchId = current.unique_id;
                scanDetail.sampleRejectHealthBase = healthBase;
                scanDetail.sampleRejectHeroBase = heroBase;
                scanDetail.sampleRejectHeroId = heroId;
                scanDetail.sampleRejectVelocityBase = velocityBase;
                scanDetail.sampleRejectBoneBase = boneBase;
                scanDetail.sampleRejectHealthCm = std::isfinite(healthValue)
                    ? static_cast<int>(healthValue * 100.0f)
                    : 0;
                scanDetail.sampleRejectHealthMaxCm = std::isfinite(healthMaxValue)
                    ? static_cast<int>(healthMaxValue * 100.0f)
                    : 0;
            };

            const uint64_t health =
                DecryptComponent(current.entity, offset::Active().typeHealth, snapshot);
            if (IsPlausibleUserPointer(health))
                ++scanDetail.selfHealthBase;
            if (!IsPlausibleUserPointer(health)) {
                rememberReject(1, health, 0, 0, 0, 0, 0.0f, 0.0f);
                finishValidation();
                return false;
            }

            health_compo_t health_compo{};
            if (!SDK->read_range(health, &health_compo, sizeof(health_compo))) {
                rememberReject(2, health, 0, 0, 0, 0, 0.0f, 0.0f);
                finishValidation();
                return false;
            }
            ++scanDetail.selfHealthRead;

            HealthPayloadSummary parsedHealth{};
            const bool validHealth = TryParseHealthPayload(health_compo, parsedHealth);
            const float totalHealth = parsedHealth.total;
            const float totalHealthMax = parsedHealth.total_max;
            if (!validHealth || totalHealth <= 0.0f) {
                rememberReject(3, health, 0, 0, 0, 0, totalHealth, totalHealthMax);
                finishValidation();
                return false;
            }
            ++scanDetail.selfHealthPlausible;

            const uint64_t hero =
                DecryptComponent(current.entity, offset::Active().typeHeroId, snapshot);
            if (IsPlausibleUserPointer(hero))
                ++scanDetail.selfHeroBase;
            if (!IsPlausibleUserPointer(hero)) {
                rememberReject(4, health, hero, 0, 0, 0, totalHealth, totalHealthMax);
                finishValidation();
                return false;
            }
            hero_compo_t hero_compo{};
            if (!SDK->read_range(hero+0xD0, &hero_compo.heroid, sizeof(hero_compo.heroid))) {
                rememberReject(5, health, hero, 0, 0, 0, totalHealth, totalHealthMax);
                finishValidation();
                return false;
            }
            ++scanDetail.selfHeroRead;
            if (!GameData::IsKnownHeroId(hero_compo.heroid)) {
                rememberReject(6, health, hero, hero_compo.heroid, 0, 0, totalHealth, totalHealthMax);
                finishValidation();
                return false;
            }
            ++scanDetail.selfHeroKnown;

            const uint64_t velocity =
                DecryptComponent(current.entity, TYPE_VELOCITY, snapshot);
            const uint64_t bone =
                DecryptComponent(current.entity, TYPE_BONE, snapshot);
            if (IsPlausibleUserPointer(velocity))
                ++scanDetail.selfVelocityBase;
            if (IsPlausibleUserPointer(bone))
                ++scanDetail.selfBoneBase;
            if (!IsPlausibleUserPointer(velocity) && !IsPlausibleUserPointer(bone)) {
                rememberReject(7, health, hero, hero_compo.heroid, velocity, bone, totalHealth, totalHealthMax);
                finishValidation();
                return false;
            }
            ++scanDetail.selfPlayable;
            ++scanDetail.playableValidationSuccessCount;
            finishValidation();
            return true;
        };

        struct PersistentCnComponentNegativeCacheEntry {
            uint64_t entityList = 0;
            uint64_t moduleBase = 0;
            uint64_t moduleSize = 0;
            DWORD updateTick = 0;
        };
        static std::unordered_map<uint64_t, PersistentCnComponentNegativeCacheEntry>
            persistentCnComponentNegativeCache{};
        static uint64_t persistentCnComponentNegativeEpoch = 0;
        if (persistentCnComponentNegativeEpoch != scanEpoch) {
            persistentCnComponentNegativeCache.clear();
            persistentCnComponentNegativeEpoch = scanEpoch;
        }
        static uint64_t persistentCnComponentNegativeEntityList = 0;
        static uint64_t persistentCnComponentNegativeModuleBase = 0;
        static uint64_t persistentCnComponentNegativeModuleSize = 0;
        const DWORD componentNegativeCacheTtlMs =
            cnProfile ? EntityScannerComponentNegativeCacheTtlMs() : 0;
        const bool componentNegativeCacheEnabled =
            cnProfile && componentNegativeCacheTtlMs > 0;
        const size_t componentNegativeCacheRefreshBudget =
            componentNegativeCacheEnabled ? EntityScannerComponentNegativeCacheRefreshBudget() : 0;
        const bool componentNegativeCacheRefreshBudgetEnabled =
            componentNegativeCacheRefreshBudget > 0;
        size_t componentNegativeCacheRefreshCount = 0;
        scanDetail.cnComponentNegativeCacheEnabled = componentNegativeCacheEnabled;
        scanDetail.cnComponentNegativeCacheTtlMs = componentNegativeCacheTtlMs;
        scanDetail.cnComponentNegativeCacheRefreshBudget =
            componentNegativeCacheRefreshBudget;
        if (componentNegativeCacheEnabled &&
            (persistentCnComponentNegativeEntityList != entity_list ||
             persistentCnComponentNegativeModuleBase != SDK->dwGameBase ||
             persistentCnComponentNegativeModuleSize != moduleSize)) {
            persistentCnComponentNegativeCache.clear();
            persistentCnComponentNegativeEntityList = entity_list;
            persistentCnComponentNegativeModuleBase = SDK->dwGameBase;
            persistentCnComponentNegativeModuleSize = moduleSize;
        }
        if (componentNegativeCacheEnabled &&
            persistentCnComponentNegativeCache.size() > 8192) {
            persistentCnComponentNegativeCache.clear();
        }

        auto hasCnComponentOnlyPlayable = [&](const EntityScanRecord& source,
                                                uint64_t component_parent) {
            const auto validationStartedAt = std::chrono::steady_clock::now();
            const Diagnostics::ScopedDmaCallsite validationCallsite(
                Diagnostics::DmaCallsite::EntityScanComponentValidation);
            ++scanDetail.componentOnlyValidationAttemptCount;
            auto finishValidation = [&]() {
                scanDetail.scanComponentOnlyValidationMs += elapsedMs(validationStartedAt);
            };
            if (!component_parent)
            {
                finishValidation();
                return false;
            }

            if (componentNegativeCacheEnabled) {
                ++scanDetail.cnComponentNegativeCacheLookupCount;
                const DWORD now = static_cast<DWORD>(EntityRegistryNowMs());
                auto negativeIt = persistentCnComponentNegativeCache.find(component_parent);
                if (negativeIt != persistentCnComponentNegativeCache.end()) {
                    const PersistentCnComponentNegativeCacheEntry& entry =
                        negativeIt->second;
                    const bool sameContext =
                        entry.entityList == entity_list &&
                        entry.moduleBase == SDK->dwGameBase &&
                        entry.moduleSize == moduleSize;
                    const DWORD ageMs = now - entry.updateTick;
                    const bool fresh = sameContext && ageMs <= componentNegativeCacheTtlMs;
                    const bool staleMetadataAllowed =
                        sameContext &&
                        !fresh &&
                        staleMetadataMs > 0 &&
                        (staleMetadataOnly || ageMs <= staleMetadataMs);
                    const bool staleAllowed =
                        staleMetadataAllowed ||
                        (sameContext &&
                         !fresh &&
                         componentNegativeCacheRefreshBudgetEnabled &&
                         componentNegativeCacheRefreshCount >= componentNegativeCacheRefreshBudget);
                    if (fresh || staleAllowed) {
                        ++scanDetail.cnComponentNegativeCacheHitCount;
                        if (staleAllowed)
                            ++scanDetail.cnComponentNegativeCacheStaleHitCount;
                        finishValidation();
                        return false;
                    }
                    if (sameContext)
                        ++scanDetail.cnComponentNegativeCacheExpiredCount;
                    persistentCnComponentNegativeCache.erase(negativeIt);
                }
            }
            if (componentNegativeCacheEnabled) {
                ++componentNegativeCacheRefreshCount;
                ++scanDetail.cnComponentNegativeCacheRefreshCount;
            }

            EntityHeaderSnapshot componentHeader{};
            const EntityHeaderSnapshot* snapshot =
                entityScanHeaders.contains(component_parent) ? &entityScanHeaders.at(component_parent) :
                (componentHeader.Read(component_parent) ? &componentHeader : nullptr);
            auto rememberNegative = [&]() {
                if (!componentNegativeCacheEnabled)
                    return;
                PersistentCnComponentNegativeCacheEntry entry{};
                entry.entityList = entity_list;
                entry.moduleBase = SDK->dwGameBase;
                entry.moduleSize = moduleSize;
                entry.updateTick = static_cast<DWORD>(EntityRegistryNowMs());
                persistentCnComponentNegativeCache.insert_or_assign(
                    component_parent,
                    entry);
                ++scanDetail.cnComponentNegativeCacheStoreCount;
            };
            auto failValidation = [&]() {
                rememberNegative();
                finishValidation();
                return false;
            };
            auto rememberReject = [&](int reason,
                                      uint64_t healthBase,
                                      uint64_t velocityBase,
                                      uint64_t boneBase,
                                      float healthValue,
                                      float healthMaxValue) {
                if (scanDetail.sampleRejectParent != 0)
                    return;
                scanDetail.sampleRejectReason = reason;
                scanDetail.sampleRejectParent = component_parent;
                scanDetail.sampleRejectMatchId = source.unique_id;
                scanDetail.sampleRejectHealthBase = healthBase;
                scanDetail.sampleRejectVelocityBase = velocityBase;
                scanDetail.sampleRejectBoneBase = boneBase;
                scanDetail.sampleRejectHealthCm = std::isfinite(healthValue)
                    ? static_cast<int>(healthValue * 100.0f)
                    : 0;
                scanDetail.sampleRejectHealthMaxCm = std::isfinite(healthMaxValue)
                    ? static_cast<int>(healthMaxValue * 100.0f)
                    : 0;
            };

            const uint64_t health =
                DecryptComponent(component_parent, offset::Active().typeHealth, snapshot);
            if (IsPlausibleUserPointer(health))
                ++scanDetail.selfHealthBase;
            if (!IsPlausibleUserPointer(health)) {
                rememberReject(11, health, 0, 0, 0.0f, 0.0f);
                return failValidation();
            }

            health_compo_t health_compo{};
            if (!SDK->read_range(health, &health_compo, sizeof(health_compo))) {
                rememberReject(12, health, 0, 0, 0.0f, 0.0f);
                return failValidation();
            }
            ++scanDetail.selfHealthRead;

            HealthPayloadSummary parsedHealth{};
            const bool validHealth = TryParseHealthPayload(health_compo, parsedHealth);
            const float totalHealth = parsedHealth.total;
            const float totalHealthMax = parsedHealth.total_max;
            const bool plausibleHealthLayout = validHealth && totalHealth > 0.0f;
            if (!plausibleHealthLayout) {
                rememberReject(13, health, 0, 0, totalHealth, totalHealthMax);
                return failValidation();
            }
            ++scanDetail.selfHealthPlausible;

            const bool trainingBotSized =
                std::fabs(totalHealthMax - 200.0f) <= 5.0f ||
                std::fabs(totalHealthMax - 500.0f) <= 10.0f;
            if (!trainingBotSized) {
                rememberReject(14, health, 0, 0, totalHealth, totalHealthMax);
                return failValidation();
            }

            const uint64_t velocity =
                DecryptComponent(component_parent, TYPE_VELOCITY, snapshot);
            const uint64_t team =
                DecryptComponent(component_parent, offset::Active().typeTeam, snapshot);
            const uint64_t bone =
                DecryptComponent(component_parent, TYPE_BONE, snapshot);
            if (IsPlausibleUserPointer(velocity))
                ++scanDetail.selfVelocityBase;
            if (IsPlausibleUserPointer(bone))
                ++scanDetail.selfBoneBase;
            if (!IsPlausibleUserPointer(velocity)) {
                rememberReject(15, health, velocity, bone, totalHealth, totalHealthMax);
                return failValidation();
            }

            velocity_compo_t velocity_compo{};
            if (!SDK->read_range(velocity+0x200, &velocity_compo.location, sizeof(velocity_compo.location))) {
                rememberReject(16, health, velocity, bone, totalHealth, totalHealthMax);
                return failValidation();
            }
            const XMFLOAT3 location = velocity_compo.location;
            const bool finiteLocation =
                std::isfinite(location.x) &&
                std::isfinite(location.y) &&
                std::isfinite(location.z) &&
                std::fabs(location.x) < 100000.0f &&
                std::fabs(location.y) < 100000.0f &&
                std::fabs(location.z) < 100000.0f &&
                (std::fabs(location.x) > 0.001f ||
                 std::fabs(location.y) > 0.001f ||
                 std::fabs(location.z) > 0.001f);
            if (!finiteLocation) {
                rememberReject(17, health, velocity, bone, totalHealth, totalHealthMax);
                return failValidation();
            }

            bool teamOk = false;
            if (IsPlausibleUserPointer(team)) {
                const uint32_t teamBits =
                    SDK->RPM<uint32_t>(team + 0x58) & 0x0F800000u;
                teamOk = teamBits != 0;
            }
            if (!teamOk && !IsPlausibleUserPointer(bone)) {
                rememberReject(18, health, velocity, bone, totalHealth, totalHealthMax);
                return failValidation();
            }

            if (componentNegativeCacheEnabled)
                persistentCnComponentNegativeCache.erase(component_parent);
            ++scanDetail.selfPlayable;
            ++scanDetail.componentOnlyValidationSuccessCount;
            finishValidation();
            return true;
        };

        auto hasCnPlausibleHealth = [&](uint64_t component_parent) {
            if (!component_parent)
                return false;

            EntityHeaderSnapshot componentHeader{};
            const EntityHeaderSnapshot* snapshot =
                entityScanHeaders.contains(component_parent) ? &entityScanHeaders.at(component_parent) :
                (componentHeader.Read(component_parent) ? &componentHeader : nullptr);
            const uint64_t health =
                DecryptComponent(component_parent, offset::Active().typeHealth, snapshot);
            if (!IsPlausibleUserPointer(health))
                return false;

            health_compo_t health_compo{};
            if (!SDK->read_range(health, &health_compo, sizeof(health_compo)))
                return false;

            HealthPayloadSummary parsedHealth{};
            return TryParseHealthPayload(health_compo, parsedHealth) && parsedHealth.total > 0.0f;
        };

        auto hasCnKnownHero = [&](uint64_t link_parent) {
            if (!link_parent)
                return false;

            EntityHeaderSnapshot linkHeader{};
            const EntityHeaderSnapshot* snapshot =
                entityScanHeaders.contains(link_parent) ? &entityScanHeaders.at(link_parent) :
                (linkHeader.Read(link_parent) ? &linkHeader : nullptr);
            const uint64_t hero =
                DecryptComponent(link_parent, offset::Active().typeHeroId, snapshot);
            if (!IsPlausibleUserPointer(hero))
                return false;

            hero_compo_t hero_compo{};
            return SDK->read_range(hero+0xD0, &hero_compo.heroid, sizeof(hero_compo.heroid)) &&
                GameData::IsKnownHeroId(hero_compo.heroid);
        };

        auto addPlayableMatches = [&](uint32_t match_id,
                                      uint64_t link_parent,
                                      bool trustMatchOnly = false) {
            if (match_id == 0)
                return false;
            ++scanDetail.matchLookupCount;
            const auto match = match_index.find(match_id);
            if (match == match_index.end())
                return false;
            ++scanDetail.matchLookupHitCount;

            bool added = false;
            for (uint64_t component_parent : match->second) {
                if (component_parent == link_parent)
                    continue;
                if (!trustMatchOnly && !hasPlayableComponents(component_parent))
                    continue;
                added = addPair(component_parent, link_parent, trustMatchOnly) || added;
            }
            return added;
        };

        std::unordered_map<uint64_t, std::vector<uint64_t>> cnMapCandidateCache{};
        if (cnMapCandidateCacheEnabled)
            cnMapCandidateCache.reserve(records.size());
        struct PersistentCnMapCandidateCacheEntry {
            uint64_t entityList = 0;
            uint64_t moduleBase = 0;
            uint64_t moduleSize = 0;
            DWORD updateTick = 0;
            std::vector<uint64_t> candidates{};
        };
        static std::unordered_map<uint64_t, PersistentCnMapCandidateCacheEntry>
            persistentCnMapCandidateCache{};
        static uint64_t persistentCnMapCandidateEpoch = 0;
        if (persistentCnMapCandidateEpoch != scanEpoch) {
            persistentCnMapCandidateCache.clear();
            persistentCnMapCandidateEpoch = scanEpoch;
        }
        static uint64_t persistentCnMapCandidateEntityList = 0;
        static uint64_t persistentCnMapCandidateModuleBase = 0;
        static uint64_t persistentCnMapCandidateModuleSize = 0;
        const DWORD persistentMapCacheTtlMs =
            cnMapCandidateCacheEnabled ? EntityScannerPersistentMapCacheTtlMs() : 0;
        const bool persistentMapCacheEnabled =
            cnProfile && cnMapCandidateCacheEnabled && persistentMapCacheTtlMs > 0;
        const size_t persistentMapCacheRefreshBudget =
            persistentMapCacheEnabled ? EntityScannerPersistentMapCacheRefreshBudget() : 0;
        const bool persistentMapCacheRefreshBudgetEnabled =
            persistentMapCacheRefreshBudget > 0;
        size_t persistentMapCacheRefreshCount = 0;
        scanDetail.cnMapCandidatePersistentCacheEnabled = persistentMapCacheEnabled;
        scanDetail.cnMapCandidatePersistentCacheTtlMs = persistentMapCacheTtlMs;
        scanDetail.cnMapCandidatePersistentCacheRefreshBudget =
            persistentMapCacheRefreshBudget;
        if (persistentMapCacheEnabled &&
            (persistentCnMapCandidateEntityList != entity_list ||
             persistentCnMapCandidateModuleBase != SDK->dwGameBase ||
             persistentCnMapCandidateModuleSize != moduleSize)) {
            persistentCnMapCandidateCache.clear();
            persistentCnMapCandidateEntityList = entity_list;
            persistentCnMapCandidateModuleBase = SDK->dwGameBase;
            persistentCnMapCandidateModuleSize = moduleSize;
        }
        if (persistentMapCacheEnabled &&
            persistentCnMapCandidateCache.size() > 4096) {
            persistentCnMapCandidateCache.clear();
        }
        std::unordered_set<uint64_t> cnMapDiagParents{};
        if (cnMapDiagEnabled)
            cnMapDiagParents.reserve(records.size());

        auto collectCnMapBaseCandidatesForParent =
            [&](uint64_t parent) -> std::vector<uint64_t> {
                if (cnMapDiagEnabled) {
                    ++scanDetail.cnMapCandidateParentLookupCount;
                    if (cnMapDiagParents.insert(parent).second)
                        ++scanDetail.cnMapCandidateUniqueParentCount;
                    else
                        ++scanDetail.cnMapCandidateDuplicateParentCount;
                }

                if(const auto metadata=recordMetadata.find(parent);metadata!=recordMetadata.end()) {
                    const auto found=cnMapCandidateCache.find(parent);
                    if(found!=cnMapCandidateCache.end())return found->second;
                    std::vector<uint64_t> candidates;
                    AddCnMapBaseCandidate(candidates,parent,entity_list,kEntityListReadSize,SDK->dwGameBase,moduleSize);
                    AddCnMapBaseCandidate(candidates,metadata->second.plus8,entity_list,kEntityListReadSize,SDK->dwGameBase,moduleSize);
                    if(metadata->second.wrapperId)AddCnMapBaseCandidate(candidates,ResolveCnWrapperMapBase(parent),
                        entity_list,kEntityListReadSize,SDK->dwGameBase,moduleSize);
                    cnMapCandidateCache.emplace(parent,candidates);return candidates;
                }
                if (!cnMapCandidateCacheEnabled) {
                    return CollectCnMapBaseCandidates(
                        parent,
                        entity_list,
                        kEntityListReadSize,
                        SDK->dwGameBase,
                        moduleSize,
                        &scanDetail,
                        cnMapDiagEnabled);
                }

                ++scanDetail.cnMapCandidateCacheLookupCount;
                const auto cachedIt = cnMapCandidateCache.find(parent);
                if (cachedIt != cnMapCandidateCache.end()) {
                    ++scanDetail.cnMapCandidateCacheHitCount;
                    return cachedIt->second;
                }

                ++scanDetail.cnMapCandidateCacheMissCount;
                if (persistentMapCacheEnabled) {
                    ++scanDetail.cnMapCandidatePersistentCacheLookupCount;
                    const DWORD now = static_cast<DWORD>(EntityRegistryNowMs());
                    auto persistentIt = persistentCnMapCandidateCache.find(parent);
                    if (persistentIt != persistentCnMapCandidateCache.end()) {
                        const PersistentCnMapCandidateCacheEntry& entry =
                            persistentIt->second;
                        const bool sameContext =
                            entry.entityList == entity_list &&
                            entry.moduleBase == SDK->dwGameBase &&
                            entry.moduleSize == moduleSize;
                        const DWORD ageMs = now - entry.updateTick;
                        if (sameContext && ageMs <= persistentMapCacheTtlMs) {
                            ++scanDetail.cnMapCandidatePersistentCacheHitCount;
                            cnMapCandidateCache.emplace(parent, entry.candidates);
                            return entry.candidates;
                        }
                        if (sameContext &&
                            staleMetadataMs > 0 &&
                            (staleMetadataOnly || ageMs <= staleMetadataMs) &&
                            !entry.candidates.empty()) {
                            ++scanDetail.cnMapCandidatePersistentCacheStaleHitCount;
                            cnMapCandidateCache.emplace(parent, entry.candidates);
                            return entry.candidates;
                        }
                        if (sameContext)
                            ++scanDetail.cnMapCandidatePersistentCacheExpiredCount;
                        if (sameContext &&
                            persistentMapCacheRefreshBudgetEnabled &&
                            persistentMapCacheRefreshCount >= persistentMapCacheRefreshBudget &&
                            !entry.candidates.empty()) {
                            ++scanDetail.cnMapCandidatePersistentCacheStaleHitCount;
                            cnMapCandidateCache.emplace(parent, entry.candidates);
                            return entry.candidates;
                        }
                        persistentCnMapCandidateCache.erase(persistentIt);
                    }
                    ++scanDetail.cnMapCandidatePersistentCacheMissCount;
                }
                if (persistentMapCacheEnabled) {
                    ++persistentMapCacheRefreshCount;
                    ++scanDetail.cnMapCandidatePersistentCacheRefreshCount;
                }
                std::vector<uint64_t> candidates = CollectCnMapBaseCandidates(
                    parent,
                    entity_list,
                    kEntityListReadSize,
                    SDK->dwGameBase,
                    moduleSize,
                    &scanDetail,
                    cnMapDiagEnabled);
                cnMapCandidateCache.emplace(parent, candidates);
                if (persistentMapCacheEnabled) {
                    PersistentCnMapCandidateCacheEntry entry{};
                    entry.entityList = entity_list;
                    entry.moduleBase = SDK->dwGameBase;
                    entry.moduleSize = moduleSize;
                    entry.updateTick = static_cast<DWORD>(EntityRegistryNowMs());
                    entry.candidates = candidates;
                    persistentCnMapCandidateCache.insert_or_assign(parent, std::move(entry));
                    ++scanDetail.cnMapCandidatePersistentCacheStoreCount;
                }
                return candidates;
            };

        const bool worldBzProfile = !offset::IsCnProfile();
        std::unordered_set<uint64_t> cnLinkTargetPairs{};

        struct PersistentCnLinkDecryptNegativeCacheEntry {
            uint64_t entityList = 0;
            uint64_t moduleBase = 0;
            uint64_t moduleSize = 0;
            DWORD updateTick = 0;
        };
        static std::unordered_map<uint64_t, PersistentCnLinkDecryptNegativeCacheEntry>
            persistentCnLinkDecryptNegativeCache{};
        static uint64_t persistentCnLinkDecryptNegativeEpoch = 0;
        if (persistentCnLinkDecryptNegativeEpoch != scanEpoch) {
            persistentCnLinkDecryptNegativeCache.clear();
            persistentCnLinkDecryptNegativeEpoch = scanEpoch;
        }
        static uint64_t persistentCnLinkDecryptNegativeEntityList = 0;
        static uint64_t persistentCnLinkDecryptNegativeModuleBase = 0;
        static uint64_t persistentCnLinkDecryptNegativeModuleSize = 0;
        const DWORD linkDecryptNegativeCacheTtlMs =
            cnProfile ? EntityScannerLinkDecryptNegativeCacheTtlMs() : 0;
        const bool linkDecryptNegativeCacheEnabled =
            cnProfile && linkDecryptNegativeCacheTtlMs > 0;
        scanDetail.cnLinkDecryptNegativeCacheEnabled =
            linkDecryptNegativeCacheEnabled;
        scanDetail.cnLinkDecryptNegativeCacheTtlMs =
            linkDecryptNegativeCacheTtlMs;
        if (linkDecryptNegativeCacheEnabled &&
            (persistentCnLinkDecryptNegativeEntityList != entity_list ||
             persistentCnLinkDecryptNegativeModuleBase != SDK->dwGameBase ||
             persistentCnLinkDecryptNegativeModuleSize != moduleSize)) {
            persistentCnLinkDecryptNegativeCache.clear();
            persistentCnLinkDecryptNegativeEntityList = entity_list;
            persistentCnLinkDecryptNegativeModuleBase = SDK->dwGameBase;
            persistentCnLinkDecryptNegativeModuleSize = moduleSize;
        }
        if (linkDecryptNegativeCacheEnabled &&
            persistentCnLinkDecryptNegativeCache.size() > 8192) {
            persistentCnLinkDecryptNegativeCache.clear();
        }
        auto linkDecryptNegativeCacheHit = [&](uint64_t parent) {
            if (!linkDecryptNegativeCacheEnabled || !parent)
                return false;
            ++scanDetail.cnLinkDecryptNegativeCacheLookupCount;
            const DWORD now = static_cast<DWORD>(EntityRegistryNowMs());
            auto negativeIt = persistentCnLinkDecryptNegativeCache.find(parent);
            if (negativeIt == persistentCnLinkDecryptNegativeCache.end())
                return false;

            const PersistentCnLinkDecryptNegativeCacheEntry& entry =
                negativeIt->second;
            const bool sameContext =
                entry.entityList == entity_list &&
                entry.moduleBase == SDK->dwGameBase &&
                entry.moduleSize == moduleSize;
            const DWORD ageMs = now - entry.updateTick;
            const bool fresh = sameContext && ageMs <= linkDecryptNegativeCacheTtlMs;
            const bool staleAllowed =
                sameContext &&
                !fresh &&
                staleMetadataMs > 0 &&
                (staleMetadataOnly || ageMs <= staleMetadataMs);
            if (fresh || staleAllowed) {
                ++scanDetail.cnLinkDecryptNegativeCacheHitCount;
                if (staleAllowed)
                    ++scanDetail.cnLinkDecryptNegativeCacheStaleHitCount;
                return true;
            }

            if (sameContext)
                ++scanDetail.cnLinkDecryptNegativeCacheExpiredCount;
            persistentCnLinkDecryptNegativeCache.erase(negativeIt);
            return false;
        };
        auto storeLinkDecryptNegativeCache = [&](uint64_t parent) {
            if (!linkDecryptNegativeCacheEnabled || !parent)
                return;
            PersistentCnLinkDecryptNegativeCacheEntry entry{};
            entry.entityList = entity_list;
            entry.moduleBase = SDK->dwGameBase;
            entry.moduleSize = moduleSize;
            entry.updateTick = static_cast<DWORD>(EntityRegistryNowMs());
            persistentCnLinkDecryptNegativeCache.insert_or_assign(parent, entry);
            ++scanDetail.cnLinkDecryptNegativeCacheStoreCount;
        };
        auto decryptCnLink = [&](uint64_t parent,
                                   const EntityHeaderSnapshot* snapshot) {
            if (linkDecryptNegativeCacheHit(parent))
                return static_cast<uint64_t>(0);
            ++scanDetail.linkDecryptAttemptCount;
            const uint64_t link = DecryptComponent(parent, offset::Active().typeLink, snapshot);
            if (!link)
                storeLinkDecryptNegativeCache(parent);
            return link;
        };

        for (const EntityScanRecord& current : records) {
            co_yield EntityScanProgress{EntityScanPhase::Pairing, &result};
            if (!RuntimeWorkersRunning() || !ProcessConnection::IsConnected() || !registryGuard.Check())
                break;
            const auto matchLinkStartedAt = std::chrono::steady_clock::now();
            const Diagnostics::ScopedDmaCallsite matchLinkCallsite(
                Diagnostics::DmaCallsite::EntityScanMatchLink);
            uint64_t cur_entity = current.entity;
            if (!cur_entity) {
                scanDetail.scanMatchLinkMs += elapsedMs(matchLinkStartedAt);
                continue;
            }

            if (worldBzProfile && lightScan) {
                bool added = false;
                if (current.unique_id != 0)
                    added = addPlayableMatches(current.unique_id + 1, cur_entity, true);
                if (!added && current.unique_id != 0)
                    added = addPlayableMatches(current.unique_id, cur_entity, true);
                if (!added &&
                    current.unique_id != 0 &&
                    (current.unique_id & 0x80000000u) == 0) {
                    addPlayableMatches(current.unique_id | 0x80000000u, cur_entity, true);
                }
                scanDetail.scanMatchLinkMs += elapsedMs(matchLinkStartedAt);
                continue;
            }

            uint64_t common_linker = decryptCnLink(
                cur_entity,
                current.header.valid ? &current.header : nullptr);
            if (!common_linker) {
                if (worldBzProfile) {
                    Diagnostics::RecordInvalidEntity();
                    scanDetail.scanMatchLinkMs += elapsedMs(matchLinkStartedAt);
                    continue;
                }
            } else {
                ++scanDetail.linkDecryptSuccessCount;
                ++scanDetail.linkPresent;
            }

            if (worldBzProfile) {
                uint32_t unique_id = SDK->RPM<uint32_t>(common_linker + offset::Link_UniqueId);
                if (unique_id != 0)
                    ++scanDetail.linkUidNonZero;
                bool added = false;
                if (current.unique_id != 0)
                    added = addPlayableMatches(current.unique_id + 1, cur_entity);
                if (!added && unique_id != 0)
                    added = addPlayableMatches(unique_id, cur_entity);
                if (!added && unique_id != 0 && (unique_id & 0x80000000u) == 0)
                    addPlayableMatches(unique_id | 0x80000000u, cur_entity);
                scanDetail.scanMatchLinkMs += elapsedMs(matchLinkStartedAt);
                continue;
            }

            bool processedCnLink = false;
            auto processCnLinkParent = [&](uint64_t link_parent,
                                             const EntityHeaderSnapshot* snapshot) {
                if (!link_parent)
                    return;

                const uint64_t linkBase =
                    link_parent == cur_entity && common_linker
                        ? common_linker
                        : decryptCnLink(link_parent, snapshot);
                if (!linkBase)
                    return;
                if (!(link_parent == cur_entity && common_linker))
                    ++scanDetail.linkDecryptSuccessCount;

                processedCnLink = true;
                ++scanDetail.linkPresent;

                const uint32_t unique_id =
                    SDK->RPM<uint32_t>(linkBase + offset::Link_UniqueId);
                if (unique_id != 0) {
                    ++scanDetail.linkUidNonZero;
                    ++scanDetail.matchLookupCount;
                    const auto match = match_index.find(unique_id);
                    if (match != match_index.end()) {
                        ++scanDetail.matchLookupHitCount;
                        ++scanDetail.linkMatched;
                        scanDetail.cnBucketEntryScanCount += match->second.size();
                        for (uint64_t component_parent : match->second) {
                            if (addPair(component_parent, link_parent, lightScan))
                                ++scanDetail.linkPairs;
                        }
                    }
                }

                const bool linkHeroKnown = !lightScan && hasCnKnownHero(link_parent);
                for (uint64_t idOffset : { offset::Link_TargetId, offset::Link_UniqueId }) {
                    uint32_t resolvedId = 0;
                    uint64_t targetMapBase = 0;
                    {
                        const auto targetMapStartedAt = std::chrono::steady_clock::now();
                        const Diagnostics::ScopedDmaCallsite targetResolveCallsite(
                            Diagnostics::DmaCallsite::EntityScanLinkTargetResolve);
                        ++scanDetail.cnTargetMapAttemptCount;
                        targetMapBase =
                            ResolveCnLinkTargetMapBase(linkBase, idOffset, &resolvedId);
                        scanDetail.scanCnTargetMapMs += elapsedMs(targetMapStartedAt);
                    }
                    if (!IsLikelyCnEntityParent(
                            targetMapBase,
                            entity_list,
                            kEntityListReadSize,
                            SDK->dwGameBase,
                            moduleSize)) {
                        continue;
                    }
                    ++scanDetail.cnTargetMapSuccessCount;

                    const uint64_t pairKey =
                        (link_parent >> 4) ^ (targetMapBase << 17) ^ resolvedId;
                    if (!cnLinkTargetPairs.insert(pairKey).second)
                        continue;

                    if (!lightScan && !linkHeroKnown && !hasCnPlausibleHealth(targetMapBase))
                        continue;

                    if (addPair(targetMapBase, link_parent, lightScan)) {
                        ++scanDetail.linkMatched;
                        ++scanDetail.linkPairs;
                    }
                }
            };

            if (common_linker)
                processCnLinkParent(
                    cur_entity,
                    current.header.valid ? &current.header : nullptr);

            {
                std::vector<uint64_t> linkParentCandidates{};
                {
                    const auto targetMapStartedAt = std::chrono::steady_clock::now();
                    const Diagnostics::ScopedDmaCallsite mapCandidateCallsite(
                        Diagnostics::DmaCallsite::EntityScanMapCandidate);
                    linkParentCandidates =
                        collectCnMapBaseCandidatesForParent(cur_entity);
                    scanDetail.scanCnTargetMapMs += elapsedMs(targetMapStartedAt);
                }
                scanDetail.cnMapCandidateCount += linkParentCandidates.size();
                for (uint64_t link_parent : linkParentCandidates) {
                    if (link_parent == cur_entity)
                        continue;
                    if (linkDecryptNegativeCacheHit(link_parent))
                        continue;

                    EntityHeaderSnapshot linkHeader{};
                    const EntityHeaderSnapshot* snapshot =
                        entityScanHeaders.contains(link_parent) ? &entityScanHeaders.at(link_parent) :
                (linkHeader.Read(link_parent) ? &linkHeader : nullptr);
                    processCnLinkParent(link_parent, snapshot);
                }
            }

            if (!processedCnLink)
                Diagnostics::RecordInvalidEntity();
            scanDetail.scanMatchLinkMs += elapsedMs(matchLinkStartedAt);
        }

        if (offset::IsCnProfile() && !lightScan) {
            for (const EntityScanRecord& current : records) {
                co_yield EntityScanProgress{EntityScanPhase::Pairing, &result};
                if (!RuntimeWorkersRunning() || !ProcessConnection::IsConnected() || !registryGuard.Check())
                    break;
                if (hasCnPlayableComponents(current))
                    addPair(current.entity, current.entity);
                std::vector<uint64_t> componentCandidates{};
                {
                    const auto targetMapStartedAt = std::chrono::steady_clock::now();
                    const Diagnostics::ScopedDmaCallsite mapCandidateCallsite(
                        Diagnostics::DmaCallsite::EntityScanMapCandidate);
                    componentCandidates =
                        collectCnMapBaseCandidatesForParent(current.entity);
                    scanDetail.scanCnTargetMapMs += elapsedMs(targetMapStartedAt);
                }
                scanDetail.cnMapCandidateCount += componentCandidates.size();
                for (uint64_t component_parent : componentCandidates) {
                    if (hasCnComponentOnlyPlayable(current, component_parent))
                        addPair(component_parent, component_parent);
                }
            }
        }

        const auto dynamicPairStartedAt = std::chrono::steady_clock::now();
        for (const EntityScanRecord& current : records) {
            co_yield EntityScanProgress{EntityScanPhase::Pairing, &result};
            if (!RuntimeWorkersRunning() || !ProcessConnection::IsConnected() || !registryGuard.Check())
                break;
            if (isDynamicEntityId(current.entity_id)) {
                addPair(current.entity, current.entity);
                ++scanDetail.dynamicPairs;
            }
        }
        scanDetail.scanDynamicPairMs += elapsedMs(dynamicPairStartedAt);

        if(!records.empty() && result.empty())entityScanComplete=false;
        if(!registryGuard.Check(true)) {
            result.clear();entityScanComplete=false;entityScanStatus=EntityRegistry::ScanStatus::Invalidated;
        } else entityScanStatus=entityScanComplete?EntityRegistry::ScanStatus::Complete:EntityRegistry::ScanStatus::Partial;
        scanDetail.totalPairs = result.size();
        finishScanDetail();

        Diagnostics::Trace("Entity scan list=0x%llX bytes=%zu records=%zu pairs=%zu.",
            static_cast<unsigned long long>(entity_list),
            readable_bytes,
            records.size(),
            result.size());
        co_return result;
    }

    inline std::vector<std::pair<uint64_t,uint64_t>> get_ow_entities() {
        auto scan=BeginEntityScan();
        while(!scan.Done())scan.Resume();
        return scan.Pairs();
    }

    inline std::string GetHeroEngNames(uint64_t HeroID, uint64_t LinkBase) {
        if (HeroID == eHero::HERO_DVA) {
            return SDK->RPM<uint16_t>(LinkBase + 0xD4) != SDK->RPM<uint16_t>(LinkBase + 0xD8)
                ? "D.Va"
                : "Hana";
        }

        if (const char* name = GameData::HeroName(HeroID); name[0] != '\0')
            return name;

        switch (HeroID) {
            case eHero::TOBTERT: return "Tob";
            case eHero::SYMTERT: return "Sym";
            case eHero::Bob:     return "Bob";
            default:
                if (GameData::HasHeroIdPrefix(HeroID)) {
                    char fallback[16] = {};
                    std::snprintf(fallback, sizeof(fallback), "Hero_%04llX",
                        static_cast<unsigned long long>(HeroID & 0xFFFFull));
                    return fallback;
                }
                if (HeroID != 0) {
                    return GameData::UnknownHeroFallbackName(HeroID);
                }
                return "Unknown";
        }
    }

    inline bool IsSkillActive(uint64_t base, uint16_t index, uint16_t id) {
        if (id == 0) return false;
        uint64_t skillList = 0;

        if (index == 0) {
            skillList = base + 0x570;
        } else {
            uint32_t count = SDK->RPM<uint32_t>(base + 0x378);
            if (count == 0 || count >= 0xFF) return false;
            uint64_t entry = SDK->RPM<uint64_t>(base + 0x370);
            if (!entry) return false;
            for (uint32_t i = 0; i < count; i++, entry += 0x10) {
                if (SDK->RPM<uint16_t>(entry + 0x8) == index) {
                    uint64_t listStruct = SDK->RPM<uint64_t>(entry);
                    if (!listStruct) return false;
                    skillList = SDK->RPM<uint64_t>(listStruct + 0xA8);
                    break;
                }
            }
        }
        if (!skillList) return false;

        uint64_t listEntry = skillList + 0x20 * ((id & 0xF) + 1);
        uint64_t structList = SDK->RPM<uint64_t>(listEntry);
        if (!structList) return false;

        int32_t listIndex = (index == 0) ? 0 : SDK->RPM<int32_t>(listEntry + 0x8) - 1;
        if (listIndex < 0 || listIndex >= 0xFF) return false;

        uint64_t skillEntry = structList + 0x10 * listIndex;
        if (SDK->RPM<uint16_t>(skillEntry) == id) {
            uint64_t skill = SDK->RPM<uint64_t>(skillEntry + 0x8);
            if (!skill) return false;
            return SDK->RPM<uint8_t>(skill + 0x48) == 1;
        }
        return false;
    }

    inline uintptr_t SkillStructCheck(uint64_t a1, uint16_t a2) {
        const uint32_t count = SDK->RPM<uint32_t>(a1 + 0x2A8);
        if (count == 0 || count >= 0xFF) return 0;

        uint64_t entry = SDK->RPM<uint64_t>(a1 + 0x2A0);
        if (!entry) return 0;

        for (uint32_t scanned = 0; scanned < count; ++scanned, entry += 16) {
            if (SDK->RPM<uint16_t>(entry + 8) == a2)
                return SDK->RPM<uint64_t>(entry);
        }
        return 0;
    }

    inline uint64_t FnSkillStruct(__m128* a1, uint16_t* a2) {
        uint16_t* v3 = a2;
        if (!a2[1]) return 0;
        uint16_t v4 = *a2;
        uint64_t v5 = a1->m128_i64[1];
        uint64_t v6 = 0;
        if (!v4) {
            v6 = v5 + 0x4A0;
            goto LABEL_6;
        }
        {
            uint64_t v7 = SkillStructCheck(v5, v4);
            if (!v7) return 0;
            v6 = SDK->RPM<uint64_t>(v7 + 0xB8);
        }
    LABEL_6:
        uint16_t v8 = v3[1];
        uint64_t v9 = 32 * ((v3[1] & 0xF) + 1);
        const uint32_t entryCount = SDK->RPM<uint32_t>(v9 + v6 + 8);
        if (entryCount == 0 || entryCount >= 0xFF) return 0;

        const uint64_t listBase = SDK->RPM<uint64_t>(v9 + v6);
        if (!listBase) return 0;

        for (uint32_t remaining = entryCount; remaining > 0; --remaining) {
            const uint64_t entry = listBase + 16ull * (remaining - 1);
            if (SDK->RPM<uint16_t>(entry) != v8)
                continue;

            uint64_t v13 = SDK->RPM<uint64_t>(entry + 8);
            if (!v13) return 0;
            return v13;
        }
        return 0;
    }

    inline bool IsSkillActivate1(uint64_t base, uint16_t skillIdx, uint16_t skillIdx2) {
        __m128 skillStruct{};
        uint16_t skillId[15] = { skillIdx, skillIdx2 };
        skillStruct.m128_u64[1] = base + 0xD0;
        uint64_t skill = FnSkillStruct(&skillStruct, skillId);
        if (!skill) return false;
        return SDK->RPM<uint8_t>(skill + 0x48) == 1;
    }

    inline float GetSkill(uint64_t base, uint16_t skillIdx, uint16_t skillIdx2, uint16_t offset) {
        __m128 skillStruct{};
        uint16_t skillId[15] = { skillIdx, skillIdx2 };
        skillStruct.m128_u64[1] = base + 0xD0;
        uint64_t skill = FnSkillStruct(&skillStruct, skillId);
        if (!skill) return 0.f;
        return SDK->RPM<float>(skill + offset);
    }

    inline float readskillcd(uint64_t base, uint16_t skillIdx, uint16_t skillIdx2) {
        __m128 skillStruct{};
        uint16_t skillId[15] = { skillIdx, skillIdx2 };
        skillStruct.m128_u64[1] = base + 0xD0;
        uint64_t skill = FnSkillStruct(&skillStruct, skillId);
        if (!skill) return false;
        float ret = SDK->RPM<float>(skill + 0x48);
        if (!ret) return ret;
        ret = SDK->RPM<float>(skill + 0x60);
        return (ret != 0) ? ret : 1.f;
    }

    inline bool TryReadUlt(uint64_t base, uint16_t skillIdx, uint16_t skillIdx2, float& ultimate) {
        __m128 skillStruct{};
        uint16_t skillId[15] = { skillIdx, skillIdx2 };
        skillStruct.m128_u64[1] = base + 0xD0;
        uint64_t skill = FnSkillStruct(&skillStruct, skillId);
        if (!skill)
            return false;
        return SDK->read_range(skill + 0x60, &ultimate, sizeof(ultimate));
    }

    inline float readult(uint64_t base, uint16_t skillIdx, uint16_t skillIdx2) {
        float ultimate = 0.0f;
        return TryReadUlt(base, skillIdx, skillIdx2, ultimate) ? ultimate : 0.0f;
    }

    inline int readammo(uint64_t base, uint16_t skillIdx, uint16_t skillIdx2) {
        __m128 skillStruct{};
        uint16_t skillId[15] = { skillIdx, skillIdx2 };
        skillStruct.m128_u64[1] = base + 0xD0;
        const uint64_t skill = FnSkillStruct(&skillStruct, skillId);
        if (!skill) return -1;

        auto roundedAmmo = [](float value) -> int {
            if (!std::isfinite(value) || value < 0.0f || value > 300.0f)
                return -1;
            if (value > 0.0f && value < 0.01f)
                return -1;
            return static_cast<int>(std::lround(value));
        };

        int zeroCandidate = -1;
        auto chooseAmmo = [&zeroCandidate](int value) -> int {
            if (value > 0)
                return value;
            if (value == 0)
                zeroCandidate = 0;
            return -1;
        };

        int ammo = chooseAmmo(roundedAmmo(SDK->RPM<float>(skill + 0x60)));
        if (ammo > 0)
            return ammo;

        ammo = chooseAmmo(roundedAmmo(SDK->RPM<float>(skill + 0x50)));
        if (ammo > 0)
            return ammo;

        const int rawAmmo50 = SDK->RPM<int>(skill + 0x50);
        if (rawAmmo50 >= 0 && rawAmmo50 <= 300) {
            ammo = chooseAmmo(rawAmmo50);
            if (ammo > 0)
                return ammo;
        }

        const int rawAmmo60 = SDK->RPM<int>(skill + 0x60);
        if (rawAmmo60 >= 0 && rawAmmo60 <= 300) {
            ammo = chooseAmmo(rawAmmo60);
            if (ammo > 0)
                return ammo;
        }

        return zeroCandidate;
    }

} // namespace OW
