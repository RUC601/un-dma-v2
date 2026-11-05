#pragma once
/*
用户指定永久保留的研究成果 — 2026-11-05
====================================
此文件是注释档案，不参与正式运行。不要以“死代码/实验已删除/清理无用文件”为由
自行删除、缩写、迁出或改写下面的完整实现；删除必须获得用户明确指令。
正式 V2 已按用户决定采用 active 状态 + 计时器。本档案不授权恢复直接 CD 轮询。

复现实验：CN 2.25.1.0.154905N，timestamp 1792635532 / image 0x5A98000。
静态 RVA 0x2747500 / 0x2751E50 证明子对象 +0xC8 的非零技能索引路线。
当前房间非本地 bot 上已直接读到多个持续递减、归零/重置的 0x3D 计时值；
不是采集端模拟。证据：03_DOWNP/vertifytool/cooldown-20261105/。
正式 D3D 实验版截图及 API：04_SCENARIO/un-dma-v2/reports/cd-experiment-20261105/。
未验：全部英雄映射、真人/所有模式/版本。映射内的论坛候选不是全量验收。
重要：部分值结束后残留约0.015；+0x48 不能统一当作数值可读性门控；
+0x6C=3 按float、=2按int32；1.0秒不能当成就绪哨兵。

下方完整 C++ 可独立复制为头文件，向 TryRead 传入严格完整读取的函数即可。
保留的实现 SHA256（按UTF-8文本）：2cc83f8e8169820d2e75d9b89b047516bf467c48e1ab4da05064473c3ab7c63a

BEGIN PRESERVED DIRECT-CD IMPLEMENTATION
#pragma once
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>

namespace OW::ExperimentalCooldown {
    struct Source {
        uint16_t index = 0;
        uint32_t id = 0;
    };
    using Sources = std::array<Source, 2>;

    // Candidate hero/slot mappings: forum 516727 p367. Unknown mappings remain unknown.
    inline Sources ForHero(uint64_t hero) {
        if ((hero & 0xffffffffffff0000ull) != 0x02e0000000000000ull) return {};
        auto pair = [](uint16_t a, uint16_t b) -> Sources { return {{{a,0x3d},{b,0x3d}}}; };
        switch (hero & 0xffff) {
        case 0x002: return pair(1,5);   // Reaper
        case 0x003: return pair(3,2);   // Tracer
        case 0x004: return pair(4,6);   // Mercy
        case 0x005: return pair(5,6);   // Hanzo
        case 0x006: return {{{0,0x1f89},{0,0x189c}}}; // Turret / Overload icon order.
        case 0x007: return pair(3,8);   // Reinhardt
        case 0x008: return pair(2,7);   // Pharah
        case 0x009: return pair(7,4);   // Winston
        case 0x00a: return pair(4,3);   // Widowmaker
        case 0x015: return pair(4,8);   // Bastion
        case 0x016: return pair(6,8);   // Symmetra
        case 0x029: return pair(7,8);   // Genji
        case 0x040: return pair(4,0);   // Hook; Take a Breather uses a resource, not the Pig Pen timer.
        case 0x042: return pair(6,7);   // Cassidy
        case 0x065: return pair(6,7);   // Junkrat
        case 0x068: return pair(0,0);   // Zarya: retain unknown until its charge source is established.
        case 0x06e: return pair(0,7);   // Soldier sprint has no numeric CD source here.
        case 0x079: return pair(0,6);   // Lucio crossfade has no numeric CD source here.
        case 0x07a: return pair(6,9);   // D.Va
        case 0x0dd: return pair(4,5);   // Mei
        case 0x12e: return pair(7,6);   // Sombra
        case 0x12f: return pair(6,8);   // Doomfist
        case 0x13b: return pair(7,5);   // Ana
        case 0x13e: return pair(6,4);   // Orisa: Fortify / Javelin Spin icon order.
        case 0x195: return pair(4,7);   // Brigitte icon order: Whip Shot / Repair Pack.
        case 0x1a2: return pair(4,6);   // Moira
        case 0x1ca: return pair(0,9);   // Ball roll has no numeric CD source here.
        case 0x1ec: return pair(6,7);   // Sojourn
        case 0x200: return pair(6,3);   // Ashe
        case 0x206: return pair(4,6);   // Echo
        case 0x221: return pair(6,5);   // Baptiste
        case 0x231: return pair(4,5);   // Kiriko: candidate mapping, unconfirmed entries show --.
        case 0x236: return pair(5,6);   // Junker Queen
        case 0x23b: return pair(6,7);   // Sigma
        case 0x28d: return pair(5,8);   // Ramattra
        case 0x291: return pair(0,4);   // Petal Platform source unknown; Life Grip index4.
        case 0x30a: return pair(5,6);   // Mauga
        case 0x31c: return pair(5,4);   // Illari
        case 0x32a: return pair(6,7);   // Freja: nodes observed live, ability labels remain experimental.
        case 0x32b: return pair(6,5);   // Venture icon order: Burrow / Drill Dash.
        case 0x362: return pair(6,7);   // Hazard
        case 0x365: return pair(8,9);   // Juno
        case 0x3c3: return pair(6,7);   // Wuyang
        case 0x472: return pair(6,7);   // Vendetta
        case 0x4c4: return pair(9,10);  // Domina
        case 0x4d2: return pair(6,8);   // Sierra
        case 0x4d8: return pair(5,6);   // Emre
        case 0x4dd: return pair(6,7);   // Anran
        case 0x4e3: return pair(4,5);   // Mizuki
        case 0x516: return pair(7,8);   // Jetpack Cat
        case 0x52c: return pair(9,8);   // Shion
        default: return {};
        }
    }

    inline bool Pointer(uint64_t p) { return p >= 0x10000 && p < 0x800000000000ull && (p & 7) == 0; }
    template<class T> inline T At(const void* p, size_t offset = 0) {
        T result{};
        std::memcpy(&result, static_cast<const unsigned char*>(p) + offset, sizeof(T));
        return result;
    }

    // Exact reads, bounded batched tables, no creation fallback and no active-state gate.
    template<class Read> bool TryRead(Read&& read, uint64_t skill, Source source, float& seconds) {
        if (!Pointer(skill) || !source.id || (!source.index && source.id == 0x3d)) return false;
        const uint64_t root = skill + 0x110;
        uint64_t table = root + 0x4a0;
        std::array<unsigned char, 16 * 254> entries{};
        std::array<unsigned char, 16> header{};
        if (source.index) {
            if (!read(root + 0x2a0, header.data(), header.size())) return false;
            const auto pointer = At<uint64_t>(header.data());
            const auto count = At<uint32_t>(header.data(), 8);
            if (!Pointer(pointer) || !count || count > 254 || !read(pointer, entries.data(), count * 16)) return false;
            uint64_t child = 0;
            for (uint32_t i = 0; i < count; ++i)
                if (At<uint16_t>(entries.data(), i * 16 + 8) == source.index) {
                    child = At<uint64_t>(entries.data(), i * 16); break;
                }
            if (!Pointer(child) || !read(child + 0xc8, &table, sizeof(table)) || !Pointer(table)) return false;
        }
        const uint64_t bucket = table + 0x20 * ((source.id & 15) + 1);
        if (!read(bucket, header.data(), header.size())) return false;
        const auto pointer = At<uint64_t>(header.data());
        const auto count = At<uint32_t>(header.data(), 8);
        if (!Pointer(pointer) || !count || count > 254 || !read(pointer, entries.data(), count * 16)) return false;
        uint64_t node = 0;
        for (uint32_t i = count; i > 0; --i)
            if (At<uint32_t>(entries.data(), (i - 1) * 16) == source.id) {
                node = At<uint64_t>(entries.data(), (i - 1) * 16 + 8); break;
            }
        std::array<unsigned char, 16> payload{};
        if (!Pointer(node) || !read(node + 0x60, payload.data(), payload.size())) return false;
        const auto type = At<uint32_t>(payload.data(), 12);
        float value = type == 3 ? At<float>(payload.data()) : type == 2 ? float(At<int32_t>(payload.data())) : -1;
        if (!std::isfinite(value) || value < 0 || value > 600) return false;
        seconds = value <= .05f ? 0 : value;
        return true;
    }
}

END PRESERVED DIRECT-CD IMPLEMENTATION
*/
