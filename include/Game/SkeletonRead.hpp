#pragma once

#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <limits>

namespace OW::Skeleton {

    inline constexpr std::size_t MaxBones = 1024;
    inline std::atomic<uint64_t> RevisionSequence{0};

    inline uint64_t AddressOffset(uint64_t address, uint64_t offset) {
        return address && address <= (std::numeric_limits<uint64_t>::max)() - offset
            ? address + offset : 0;
    }

    struct ReadMetrics {
        uint64_t requests = 0;
        uint64_t bytes = 0;
        uint64_t failures = 0;
    };

    // The callback must succeed only when every requested byte was read.
    // Production binds ReadFreshExact, replay binds an address-space fixture.
    struct ReadSource {
        void* context = nullptr;
        bool (*read)(void*, uint64_t, void*, std::size_t) = nullptr;
        ReadMetrics* metrics = nullptr;

        bool Exact(uint64_t address, void* destination, std::size_t size) const {
            if (metrics) {
                ++metrics->requests;
                metrics->bytes += size;
            }
            const bool valid = read && address && size &&
                address <= (std::numeric_limits<uint64_t>::max)() - size;
            const bool ok = valid && read(context, address, destination, size);
            if (!ok) {
                std::memset(destination, 0, size);
                if (metrics) ++metrics->failures;
            }
            return ok;
        }

        template<class T> bool Value(uint64_t address, T& value) const {
            return Exact(address, &value, sizeof(value));
        }
    };

    struct BodyIdentity {
        uint64_t address = 0, linkParent = 0, hero = 0, generation = 0;
        uint64_t connection = 0, scene = 0, absence = 0;
        uint64_t velocity = 0, bone = 0, link = 0;
        uint32_t match = 0;
        bool alive = false;
        bool operator==(const BodyIdentity&) const = default;
    };

    enum class ReadStatus : uint8_t {
        Empty, Ready, InvalidBody, RootReadFailed, DescriptorReadFailed,
        InvalidCount, IdTableReadFailed, FormReadFailed, LayoutChanged, PoseReadFailed,
        UnknownForm, NoMappedBones, InvalidTransform, PartialPose
    };

    inline const char* StatusName(ReadStatus status) {
        switch (status) {
        case ReadStatus::Ready: return "ready";
        case ReadStatus::InvalidBody: return "invalid_body";
        case ReadStatus::RootReadFailed: return "root_read_failed";
        case ReadStatus::DescriptorReadFailed: return "descriptor_read_failed";
        case ReadStatus::InvalidCount: return "invalid_count";
        case ReadStatus::IdTableReadFailed: return "id_table_read_failed";
        case ReadStatus::FormReadFailed: return "form_read_failed";
        case ReadStatus::LayoutChanged: return "layout_changed";
        case ReadStatus::PoseReadFailed: return "pose_read_failed";
        case ReadStatus::UnknownForm: return "unknown_form";
        case ReadStatus::NoMappedBones: return "no_mapped_bones";
        case ReadStatus::InvalidTransform: return "invalid_transform";
        case ReadStatus::PartialPose: return "partial_pose";
        default: return "empty";
        }
    }

    struct Layout {
        uint64_t sourceAddress = 0, boneData = 0, bonesBase = 0;
        uint64_t bonePtr = 0, idTable = 0;
        uint16_t count = 0;
        uint16_t linkD4 = 0, linkD8 = 0;
        bool formKnown = false;
        std::array<uint32_t, MaxBones> ids{};
        bool operator==(const Layout&) const = default;
    };

    // A packed hot read preserves the byte count of every scatter request.
    struct PoseRange {
        int first = 0, last = 0;
        std::size_t offset = 0, size = 0;
        unsigned long bytes = 0;
    };

    template<class T, std::size_t N>
    T Field(const std::array<uint8_t, N>& bytes, std::size_t offset) {
        T value{};
        std::memcpy(&value, bytes.data() + offset, sizeof(value));
        return value;
    }

    inline bool ReadDescriptor(const ReadSource& source, uint64_t boneData,
                               Layout& layout, ReadStatus& status) {
        std::array<uint8_t, 0x28> header{};
        std::array<uint8_t, 0x68> descriptor{};
        layout.boneData = boneData;
        if (!source.Exact(boneData, header.data(), header.size())) {
            status = ReadStatus::DescriptorReadFailed;
            return false;
        }
        layout.bonePtr = Field<uint64_t>(header, 0);
        layout.bonesBase = Field<uint64_t>(header, 0x20);
        if (!AddressOffset(layout.bonesBase, MaxBones * 0x30) ||
            !source.Exact(layout.bonePtr, descriptor.data(), descriptor.size())) {
            status = ReadStatus::DescriptorReadFailed;
            return false;
        }
        layout.idTable = Field<uint64_t>(descriptor, 0x38);
        // UN V2 layout at the pinned baseline. Pure's +0x66 is a different source.
        layout.count = Field<uint16_t>(descriptor, 0x64);
        if (!layout.count || layout.count > MaxBones) {
            status = ReadStatus::InvalidCount;
            return false;
        }
        if (!source.Exact(layout.idTable, layout.ids.data(), layout.count * sizeof(uint32_t))) {
            status = ReadStatus::IdTableReadFailed;
            return false;
        }
        return true;
    }

    inline bool ObserveLayout(const ReadSource& source, const BodyIdentity& body,
                              bool isDva, Layout& layout, ReadStatus& status) {
        layout = {};
        if (!body.alive) {
            status = ReadStatus::InvalidBody;
            return false;
        }
        const std::array<uint64_t, 4> sources{
            AddressOffset(body.velocity, 0x8B0), AddressOffset(body.bone, 0x20),
            body.bone, AddressOffset(body.bone, 0x10)};
        bool resolved = false;
        status = ReadStatus::RootReadFailed;
        for (const auto sourceAddress : sources) {
            if (!sourceAddress) continue;
            uint64_t root = 0;
            if (!source.Value(sourceAddress, root) || !root) continue;
            Layout candidate{};
            candidate.sourceAddress = sourceAddress;
            if (!ReadDescriptor(source, root, candidate, status)) continue;
            layout = candidate;
            resolved = true;
            break;
        }
        if (!resolved) return false;
        if (isDva) {
            std::array<uint8_t, 8> form{};
            if (!source.Exact(AddressOffset(body.link, 0xD4), form.data(), form.size())) {
                status = ReadStatus::FormReadFailed;
                return false;
            }
            layout.linkD4 = Field<uint16_t>(form, 0);
            layout.linkD8 = Field<uint16_t>(form, 4);
            layout.formKnown = true;
        }
        status = ReadStatus::Ready;
        return true;
    }
}
