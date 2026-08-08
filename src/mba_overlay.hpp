#pragma once

#include "common.hpp"

#include <map>
#include <set>

namespace mobigo {

struct MbaOverlayReport {
    size_t mba_bytes = 0;
    size_t snapshots = 0;
    size_t filesystem_snapshots = 0;
    size_t added_logical_blocks = 0;
    uint32_t entry_address = 0;
    MbaTarget target = MbaTarget::Auto;
    std::string role;
    std::vector<std::string> paths;
};

struct MbaMetadata {
    uint32_t declared_words = 0;
    uint32_t profile_field_0c = 0;
    uint32_t compatibility_address = 0;
    uint32_t entry_address = 0;
    uint32_t body_load_address = 0;
    std::string role;
    std::optional<MbaTarget> detected_target;
};

inline bool ascii_folded_starts_with(std::string_view value, std::string_view prefix) {
    if (value.size() < prefix.size()) return false;
    for (size_t i = 0; i < prefix.size(); ++i) {
        const auto fold = [](unsigned char c) {
            return char(c >= 'a' && c <= 'z' ? c - ('a' - 'A') : c);
        };
        if (fold(static_cast<unsigned char>(value[i])) !=
            fold(static_cast<unsigned char>(prefix[i]))) return false;
    }
    return true;
}

inline bool ascii_folded_ends_with(std::string_view value, std::string_view suffix) {
    return value.size() >= suffix.size() &&
           ascii_folded_starts_with(value.substr(value.size() - suffix.size()), suffix);
}

inline bool mba_target_matches_path(MbaTarget target, std::string_view path) {
    switch (target) {
    case MbaTarget::System:
        return ascii_folded_starts_with(path, "/BUNDLE/SY/") &&
               ascii_folded_ends_with(path, "SY.MBA");
    case MbaTarget::G1:
        return ascii_folded_starts_with(path, "/BUNDLE/G1/") &&
               ascii_folded_ends_with(path, "G1.MBA");
    case MbaTarget::Menu:
        return ascii_folded_ends_with(path, "/MM.MBA");
    case MbaTarget::Auto:
        return false;
    }
    return false;
}

inline MbaMetadata inspect_mba_metadata(const std::vector<uint8_t> &mba) {
    if (mba.empty()) die("MBA overlay: supplied MBA is empty");
    if (mba.size() & 1) die("MBA overlay: supplied MBA has an odd byte size");
    static constexpr std::array<uint8_t, 8> magic{
        'b', 'M', '_', 'g', 'b', 'M', 'Q', 'a'
    };
    if (mba.size() < 0xa0 || !std::equal(magic.begin(), magic.end(), mba.begin()))
        die("MBA overlay: supplied file does not have a supported MBA header");
    auto read32 = [&](size_t offset) {
        return uint32_t(mba[offset]) |
               (uint32_t(mba[offset + 1]) << 8) |
               (uint32_t(mba[offset + 2]) << 16) |
               (uint32_t(mba[offset + 3]) << 24);
    };

    MbaMetadata metadata;
    metadata.declared_words = read32(0x08);
    metadata.profile_field_0c = read32(0x0c);
    metadata.compatibility_address = read32(0x10) & kAddrMask;
    metadata.entry_address = read32(0x14) & kAddrMask;
    metadata.body_load_address = read32(0x18) & kAddrMask;
    if (metadata.declared_words != mba.size() / 2)
        die("MBA overlay: header word count does not match the supplied file size");
    if (metadata.entry_address == 0)
        die("MBA overlay: header entry address is zero");
    for (size_t offset = 0x80; offset < 0xa0 && mba[offset] != 0; ++offset) {
        const uint8_t value = mba[offset];
        if (value < 0x20 || value > 0x7e)
            die("MBA overlay: header role/title is not printable ASCII");
        metadata.role.push_back(char(value));
    }
    if (metadata.role.size() == 0x20)
        die("MBA overlay: header role/title is not NUL terminated");

    const std::string folded = ascii_lower(metadata.role);
    if (folded == "mgb_sys") {
        if (mba.size() != 0x174000 || metadata.profile_field_0c != 0x5387a ||
            metadata.compatibility_address != 0x0f3e60 ||
            metadata.entry_address != 0x0dfc1d ||
            metadata.body_load_address != 0x0c8800) {
            die("MBA overlay: MGB_SYS title conflicts with the validated SY profile");
        }
        metadata.detected_target = MbaTarget::System;
    } else if (folded == "mgb_g1") {
        if (mba.size() != 0x214000 || metadata.profile_field_0c != 0x3bc0b ||
            metadata.compatibility_address != 0x0f3e5c ||
            metadata.entry_address != 0x0e1a55 ||
            metadata.body_load_address != 0x0c8800) {
            die("MBA overlay: MGB_G1 title conflicts with the validated G1 profile");
        }
        metadata.detected_target = MbaTarget::G1;
    }
    return metadata;
}

inline MbaTarget resolve_mba_target(const MbaMetadata &metadata,
                                    MbaTarget requested) {
    if (requested == MbaTarget::Auto) {
        if (!metadata.detected_target) {
            die("MBA overlay: automatic target selection requires validated "
                "MGB_SYS or MGB_G1 profile metadata; use "
                "--mba-target explicitly for a verified nonstandard MBA");
        }
        return *metadata.detected_target;
    }
    if (metadata.detected_target && *metadata.detected_target != requested) {
        die(std::string("MBA overlay: header role ") + metadata.role +
            " conflicts with requested target " + mba_target_name(requested));
    }
    return requested;
}

namespace mba_overlay_detail {

constexpr size_t kPageData = 2048;
constexpr size_t kPageOob = 64;
constexpr size_t kPageRaw = kPageData + kPageOob;
constexpr size_t kPagesPerEraseBlock = 64;
constexpr size_t kEraseBlockData = kPageData * kPagesPerEraseBlock;
constexpr size_t kFsBlock = 0x4000;
constexpr size_t kFsHalf = 0x2000;
constexpr size_t kRecord = 0x200;
constexpr size_t kRecordPayload = kRecord - 8;
constexpr size_t kHalfRecords = kFsHalf / kRecord;
constexpr size_t kHalfPayload = kHalfRecords * kRecordPayload;
constexpr uint32_t kMetadataBlocks = 0x20;

inline void require_range(const std::vector<uint8_t> &bytes, size_t offset,
                          size_t count, const char *what) {
    if (offset > bytes.size() || count > bytes.size() - offset)
        die(std::string("MBA overlay: ") + what + " is outside the NAND filesystem");
}

inline uint16_t get16(const std::vector<uint8_t> &bytes, size_t offset) {
    require_range(bytes, offset, 2, "16-bit field");
    return uint16_t(bytes[offset] | (uint16_t(bytes[offset + 1]) << 8));
}

inline uint32_t get32(const std::vector<uint8_t> &bytes, size_t offset) {
    require_range(bytes, offset, 4, "32-bit field");
    return uint32_t(bytes[offset]) |
           (uint32_t(bytes[offset + 1]) << 8) |
           (uint32_t(bytes[offset + 2]) << 16) |
           (uint32_t(bytes[offset + 3]) << 24);
}

inline void put16(std::vector<uint8_t> &bytes, size_t offset, uint16_t value) {
    require_range(bytes, offset, 2, "16-bit field");
    bytes[offset] = uint8_t(value);
    bytes[offset + 1] = uint8_t(value >> 8);
}

inline void put32(std::vector<uint8_t> &bytes, size_t offset, uint32_t value) {
    require_range(bytes, offset, 4, "32-bit field");
    bytes[offset] = uint8_t(value);
    bytes[offset + 1] = uint8_t(value >> 8);
    bytes[offset + 2] = uint8_t(value >> 16);
    bytes[offset + 3] = uint8_t(value >> 24);
}

inline bool equal_ascii_folded(const std::string &a, const std::string &b) {
    if (a.size() != b.size()) return false;
    for (size_t i = 0; i < a.size(); ++i) {
        const auto fold = [](unsigned char c) {
            return char(c >= 'a' && c <= 'z' ? c - ('a' - 'A') : c);
        };
        if (fold(static_cast<unsigned char>(a[i])) !=
            fold(static_cast<unsigned char>(b[i]))) return false;
    }
    return true;
}

inline std::string decode_name(const std::vector<uint8_t> &logical, size_t offset) {
    require_range(logical, offset, 12, "directory name");
    std::string result;
    result.reserve(12);
    for (size_t i = 0; i < 12; i += 2) {
        const uint8_t pair[2]{logical[offset + i + 1], logical[offset + i]};
        for (uint8_t value : pair) {
            if (value == 0) return result;
            result.push_back(char(value));
        }
    }
    while (!result.empty() && result.back() == ' ') result.pop_back();
    return result;
}

struct RawNand {
    std::vector<uint8_t> &raw;
    size_t physical_blocks = 0;
    std::vector<int32_t> physical_for_logical;
    uint32_t max_logical = 0;
    std::vector<uint8_t> logical;

    explicit RawNand(std::vector<uint8_t> &bytes) : raw(bytes) {
        if (raw.empty() || raw.size() % kPageRaw != 0)
            die("MBA overlay: NAND size is not a multiple of the 2048+64-byte page size");
        const size_t pages = raw.size() / kPageRaw;
        if (pages % kPagesPerEraseBlock != 0)
            die("MBA overlay: NAND contains a partial erase block");
        physical_blocks = pages / kPagesPerEraseBlock;
        physical_for_logical.assign(physical_blocks, -1);
        std::vector<int> generations(physical_blocks, -1);

        bool found_zero = false;
        for (size_t physical = 0; physical < physical_blocks; ++physical) {
            const size_t oob = physical * kPagesPerEraseBlock * kPageRaw + kPageData;
            if (raw[oob] != 0xff) continue;
            const uint16_t logical_number = get16(raw, oob + 2);
            if (logical_number == 0xffff || logical_number >= physical_blocks) continue;
            const int generation = raw[oob + 1];
            if (generation >= generations[logical_number]) {
                generations[logical_number] = generation;
                physical_for_logical[logical_number] = int32_t(physical);
            }
            found_zero |= logical_number == 0;
            max_logical = std::max<uint32_t>(max_logical, logical_number);
        }
        if (!found_zero) die("MBA overlay: MobiGo logical-block tags were not found");
        build_logical();
    }

    size_t page_oob(size_t physical_page) const {
        return physical_page * kPageRaw + kPageData;
    }

    uint32_t logical_page_for(size_t physical_page_in_block, size_t oob) const {
        const uint16_t tagged = get16(raw, oob + 6);
        bool all_zero = true;
        for (size_t i = 0; i < 12; ++i) all_zero &= raw[oob + i] == 0;
        if (!all_zero && tagged != 0xffff && tagged < kPagesPerEraseBlock)
            return tagged;
        return uint32_t(physical_page_in_block);
    }

    void build_logical() {
        logical.assign((size_t(max_logical) + 1) * kEraseBlockData, 0xff);
        for (uint32_t logical_block = 0; logical_block <= max_logical; ++logical_block) {
            const int32_t physical = physical_for_logical[logical_block];
            if (physical < 0) continue;
            for (size_t page = 0; page < kPagesPerEraseBlock; ++page) {
                const size_t raw_page = (size_t(physical) * kPagesPerEraseBlock + page);
                const size_t oob = page_oob(raw_page);
                const uint32_t logical_page = logical_page_for(page, oob);
                const size_t source = raw_page * kPageRaw;
                const size_t destination = size_t(logical_block) * kEraseBlockData +
                                           size_t(logical_page) * kPageData;
                std::copy_n(raw.begin() + source, kPageData, logical.begin() + destination);
            }
        }
    }

    bool physical_block_is_erased(size_t physical) const {
        const size_t first_page = physical * kPagesPerEraseBlock;
        const size_t first_oob = page_oob(first_page);
        if (raw[first_oob] != 0xff || get16(raw, first_oob + 2) != 0xffff) return false;
        for (size_t page = 0; page < kPagesPerEraseBlock; ++page) {
            const size_t data = (first_page + page) * kPageRaw;
            if (!std::all_of(raw.begin() + data, raw.begin() + data + kPageData,
                             [](uint8_t value) { return value == 0xff; })) return false;
        }
        return true;
    }

    std::vector<uint32_t> add_logical_blocks(size_t count) {
        std::set<size_t> used;
        for (int32_t physical : physical_for_logical)
            if (physical >= 0) used.insert(size_t(physical));

        std::vector<uint32_t> added;
        for (size_t physical = physical_blocks; physical-- > 0 && added.size() < count;) {
            if (used.contains(physical) || !physical_block_is_erased(physical)) continue;
            const uint32_t logical_block = ++max_logical;
            if (logical_block >= physical_for_logical.size())
                die("MBA overlay: no logical NAND block numbers remain");
            for (size_t page = 0; page < kPagesPerEraseBlock; ++page) {
                const size_t oob = page_oob(physical * kPagesPerEraseBlock + page);
                raw[oob + 1] = 1;
                put16(raw, oob + 2, uint16_t(logical_block));
            }
            physical_for_logical[logical_block] = int32_t(physical);
            logical.resize((size_t(max_logical) + 1) * kEraseBlockData, 0xff);
            added.push_back(logical_block);
            used.insert(physical);
        }
        if (added.size() != count)
            die("MBA overlay: not enough erased physical NAND blocks for this MBA");
        return added;
    }

    void copy_logical_to_raw() {
        for (uint32_t logical_block = 0; logical_block <= max_logical; ++logical_block) {
            const int32_t physical = physical_for_logical[logical_block];
            if (physical < 0) continue;
            for (size_t page = 0; page < kPagesPerEraseBlock; ++page) {
                const size_t raw_page = size_t(physical) * kPagesPerEraseBlock + page;
                const size_t oob = page_oob(raw_page);
                const uint32_t logical_page = logical_page_for(page, oob);
                const size_t source = size_t(logical_block) * kEraseBlockData +
                                      size_t(logical_page) * kPageData;
                const size_t destination = raw_page * kPageRaw;
                std::copy_n(logical.begin() + source, kPageData, raw.begin() + destination);
            }
        }
    }
};

struct Snapshot {
    size_t base = 0;
    uint32_t generation = 0;
};

inline std::vector<Snapshot> find_snapshots(const std::vector<uint8_t> &logical) {
    static constexpr std::array<uint8_t, 22> signature{
        'M', 0, 'O', 0, 'B', 0, 'I', 0, 'G', 0, 'O', 0,
        'F', 0, 'S', 0, '3', 0, '.', 0, '0', 0
    };
    std::vector<Snapshot> result;
    auto cursor = logical.begin();
    while (cursor != logical.end()) {
        const auto found = std::search(cursor, logical.end(), signature.begin(), signature.end());
        if (found == logical.end()) break;
        const size_t position = size_t(found - logical.begin());
        cursor = found + 1;
        if (position < 4) continue;
        const size_t base = position - 4;
        if (base % kFsBlock != 0) continue;
        const size_t root = base + 2 * kFsBlock;
        if (root + kFsHalf > logical.size()) continue;
        const uint32_t generation = get32(logical, root);
        if (generation < 0x10000) result.push_back({base, generation});
    }
    if (result.empty()) die("MBA overlay: MOBIGOFS3.0 signature was not found");
    return result;
}

inline size_t directory_half(const std::vector<uint8_t> &logical,
                             size_t snapshot_base, uint32_t block) {
    const size_t block_offset = snapshot_base + size_t(block) * kFsBlock;
    bool found = false;
    uint32_t best_generation = 0;
    size_t best_offset = 0;
    for (size_t half : {block_offset, block_offset + kFsHalf}) {
        if (half + kFsHalf > logical.size()) continue;
        const uint32_t generation = get32(logical, half);
        if (generation < 0x10000 && (!found || generation >= best_generation)) {
            found = true;
            best_generation = generation;
            best_offset = half;
        }
    }
    if (!found) die("MBA overlay: directory has no valid metadata half");
    return best_offset;
}

struct DirectoryEntry {
    std::string name;
    uint32_t target = 0;
    bool is_directory = false;
};

inline std::vector<DirectoryEntry> read_directory(const std::vector<uint8_t> &logical,
                                                  size_t snapshot_base, uint32_t block) {
    const size_t half = directory_half(logical, snapshot_base, block);
    std::vector<DirectoryEntry> entries;
    for (size_t record = 0; record < kHalfRecords; ++record) {
        const size_t payload = half + record * kRecord + 4;
        for (size_t slot = 0; slot < 20; ++slot) {
            const size_t item = payload + 4 + slot * 24;
            require_range(logical, item, 24, "directory entry");
            bool empty = true;
            for (size_t i = 0; i < 12; ++i) empty &= logical[item + i] == 0;
            if (empty) continue;
            const std::string name = decode_name(logical, item);
            if (name.empty()) continue;
            const uint32_t target = get32(logical, item + 16);
            entries.push_back({name, target, target < kMetadataBlocks});
        }
    }
    return entries;
}

inline void collect_target_files(const std::vector<uint8_t> &logical,
                                 size_t snapshot_base, uint32_t directory,
                                 const std::string &path, MbaTarget selected_target,
                                 std::set<uint32_t> &seen_directories,
                                 std::map<uint32_t, std::set<std::string>> &targets) {
    if (!seen_directories.insert(directory).second)
        die("MBA overlay: directory cycle detected");
    for (const DirectoryEntry &entry : read_directory(logical, snapshot_base, directory)) {
        const std::string child = path == "/" ? "/" + entry.name : path + "/" + entry.name;
        if (entry.is_directory) {
            collect_target_files(logical, snapshot_base, entry.target, child,
                                 selected_target, seen_directories, targets);
        } else if (mba_target_matches_path(selected_target, child)) {
            targets[entry.target].insert(child);
        }
    }
    seen_directories.erase(directory);
}

struct FileLayout {
    uint32_t index_block = 0;
    std::vector<uint32_t> data_blocks;
};

inline FileLayout read_layout(const std::vector<uint8_t> &logical, uint32_t index_block) {
    const size_t index_offset = size_t(index_block) * kFsBlock;
    require_range(logical, index_offset, kFsBlock, "file index");
    if (get32(logical, index_offset + 8) != 0)
        die("MBA overlay: chained file indexes are not supported");
    FileLayout layout;
    layout.index_block = index_block;
    for (size_t sector = 0; sector < kHalfRecords; ++sector) {
        const size_t payload = index_offset + sector * kRecord + 4;
        const size_t first = sector == 0 ? 8 : 4;
        for (size_t offset = first; offset + 4 <= kRecordPayload; offset += 4) {
            const uint32_t block = get32(logical, payload + offset);
            if (block == 0) break;
            if (size_t(block) * kFsBlock >= logical.size())
                die("MBA overlay: file index references an invalid data block");
            layout.data_blocks.push_back(block);
        }
    }
    return layout;
}

inline uint16_t record_checksum(const std::vector<uint8_t> &logical, size_t offset) {
    require_range(logical, offset, kRecord, "filesystem record");
    uint32_t sum = 0;
    for (size_t i = 0; i < 255; ++i) sum += get16(logical, offset + i * 2);
    return uint16_t(sum);
}

inline void refresh_record(std::vector<uint8_t> &logical, size_t offset) {
    require_range(logical, offset, kRecord, "filesystem record");
    logical[offset + kRecord - 4] = logical[offset];
    logical[offset + kRecord - 3] = logical[offset + 1];
    put16(logical, offset + kRecord - 2, record_checksum(logical, offset));
}

inline void initialize_half(std::vector<uint8_t> &logical, size_t half,
                            size_t half_number) {
    require_range(logical, half, kFsHalf, "new storage half");
    for (size_t record = 0; record < kHalfRecords; ++record) {
        const uint16_t group = uint16_t(half_number * 4 + record / 4 + 1);
        put16(logical, half + record * kRecord, group);
        put16(logical, half + record * kRecord + 2, 1);
    }
}

inline void write_half(std::vector<uint8_t> &logical, size_t half,
                       const std::vector<uint8_t> &mba, size_t &cursor) {
    require_range(logical, half, kFsHalf, "storage half");
    for (size_t record = 0; record < kHalfRecords; ++record) {
        const size_t payload = half + record * kRecord + 4;
        for (size_t i = 0; i < kRecordPayload; ++i)
            logical[payload + i] = cursor < mba.size() ? mba[cursor++] : 0xff;
        refresh_record(logical, half + record * kRecord);
    }
}

inline std::vector<size_t> index_slots(uint32_t index_block) {
    const size_t index = size_t(index_block) * kFsBlock;
    std::vector<size_t> slots;
    for (size_t sector = 0; sector < kHalfRecords; ++sector) {
        const size_t payload = index + sector * kRecord + 4;
        const size_t first = sector == 0 ? 8 : 4;
        for (size_t offset = first; offset + 4 <= kRecordPayload; offset += 4)
            slots.push_back(payload + offset);
    }
    return slots;
}

inline void write_file(std::vector<uint8_t> &logical, const FileLayout &original,
                       const std::vector<uint32_t> &new_blocks,
                       const std::vector<uint8_t> &mba) {
    std::vector<uint32_t> blocks = original.data_blocks;
    blocks.insert(blocks.end(), new_blocks.begin(), new_blocks.end());
    const std::vector<size_t> slots = index_slots(original.index_block);
    if (blocks.size() >= slots.size())
        die("MBA overlay: selected MBA is too large for one file index");
    for (size_t slot : slots) put32(logical, slot, 0);
    for (size_t i = 0; i < blocks.size(); ++i) put32(logical, slots[i], blocks[i]);

    const size_t index = size_t(original.index_block) * kFsBlock;
    put32(logical, index + 4, uint32_t(mba.size() / 2));
    put32(logical, index + 8, 0);
    for (size_t record = 0; record < kHalfRecords; ++record)
        refresh_record(logical, index + record * kRecord);

    std::vector<size_t> halves{index + kFsHalf};
    for (uint32_t block : blocks) {
        halves.push_back(size_t(block) * kFsBlock);
        halves.push_back(size_t(block) * kFsBlock + kFsHalf);
    }
    const size_t first_new_half = 1 + original.data_blocks.size() * 2;
    for (size_t half_number = first_new_half; half_number < halves.size(); ++half_number)
        initialize_half(logical, halves[half_number], half_number);

    size_t cursor = 0;
    for (size_t half : halves) write_half(logical, half, mba, cursor);
    if (cursor < mba.size()) die("MBA overlay: internal allocation error");
}

inline std::vector<uint8_t> read_file(const std::vector<uint8_t> &logical,
                                      uint32_t index_block) {
    const FileLayout layout = read_layout(logical, index_block);
    const size_t size = size_t(get32(logical, size_t(index_block) * kFsBlock + 4)) * 2;
    std::vector<size_t> halves{size_t(index_block) * kFsBlock + kFsHalf};
    for (uint32_t block : layout.data_blocks) {
        halves.push_back(size_t(block) * kFsBlock);
        halves.push_back(size_t(block) * kFsBlock + kFsHalf);
    }
    std::vector<uint8_t> output;
    output.reserve(size);
    for (size_t half : halves) {
        require_range(logical, half, kFsHalf, "storage half");
        for (size_t record = 0; record < kHalfRecords && output.size() < size; ++record) {
            const size_t payload = half + record * kRecord + 4;
            const size_t count = std::min(kRecordPayload, size - output.size());
            output.insert(output.end(), logical.begin() + payload,
                          logical.begin() + payload + count);
        }
    }
    if (output.size() != size) die("MBA overlay: round-trip file is truncated");
    return output;
}

} // namespace mba_overlay_detail

inline MbaOverlayReport apply_mba_overlay(std::vector<uint8_t> &raw_nand,
                                          const std::vector<uint8_t> &mba,
                                          MbaTarget requested_target = MbaTarget::Auto) {
    using namespace mba_overlay_detail;
    const MbaMetadata metadata = inspect_mba_metadata(mba);
    const MbaTarget selected_target = resolve_mba_target(metadata, requested_target);

    RawNand nand(raw_nand);
    const std::vector<Snapshot> snapshots = find_snapshots(nand.logical);
    std::map<uint32_t, std::set<std::string>> targets;
    size_t matched_snapshots = 0;
    for (const Snapshot &snapshot : snapshots) {
        std::set<uint32_t> seen;
        std::map<uint32_t, std::set<std::string>> snapshot_targets;
        collect_target_files(nand.logical, snapshot.base, 2, "/", selected_target,
                             seen, snapshot_targets);
        if (!snapshot_targets.empty()) ++matched_snapshots;
        for (auto &[target, paths] : snapshot_targets)
            targets[target].insert(paths.begin(), paths.end());
    }
    if (targets.empty()) {
        die(std::string("MBA overlay: no files matching target ") +
            mba_target_name(selected_target) + " were found in the NAND");
    }

    struct Work {
        uint32_t target = 0;
        FileLayout layout;
        size_t extra_blocks = 0;
    };
    std::vector<Work> work;
    size_t total_extra_blocks = 0;
    for (const auto &[target, paths] : targets) {
        (void)paths;
        const FileLayout layout = read_layout(nand.logical, target);
        size_t extra = 0;
        while ((1 + 2 * (layout.data_blocks.size() + extra)) * kHalfPayload < mba.size())
            ++extra;
        total_extra_blocks += extra;
        work.push_back({target, layout, extra});
    }

    std::vector<uint32_t> free_fs_blocks;
    const size_t logical_blocks_needed = (total_extra_blocks + 7) / 8;
    for (uint32_t logical_block : nand.add_logical_blocks(logical_blocks_needed))
        for (uint32_t within = 0; within < 8; ++within)
            free_fs_blocks.push_back(logical_block * 8 + within);

    size_t allocation_cursor = 0;
    for (const Work &item : work) {
        const auto first = free_fs_blocks.begin() + std::ptrdiff_t(allocation_cursor);
        const std::vector<uint32_t> extra(first, first + std::ptrdiff_t(item.extra_blocks));
        allocation_cursor += item.extra_blocks;
        write_file(nand.logical, item.layout, extra, mba);
        if (read_file(nand.logical, item.target) != mba)
            die("MBA overlay: in-memory round-trip verification failed");
    }
    nand.copy_logical_to_raw();

    MbaOverlayReport report;
    report.mba_bytes = mba.size();
    report.snapshots = matched_snapshots;
    report.filesystem_snapshots = snapshots.size();
    report.added_logical_blocks = logical_blocks_needed;
    report.entry_address = metadata.entry_address;
    report.target = selected_target;
    report.role = metadata.role;
    for (const auto &[target, paths] : targets) {
        (void)target;
        report.paths.insert(report.paths.end(), paths.begin(), paths.end());
    }
    std::sort(report.paths.begin(), report.paths.end());
    report.paths.erase(std::unique(report.paths.begin(), report.paths.end()), report.paths.end());
    return report;
}

} // namespace mobigo
