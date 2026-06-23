#pragma once
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "bloom_filter.h"

// ---------------------------------------------------------------------------
// SSTable (Sorted String Table) — immutable on-disk file.
//
// File layout:
//
//  Offset 0
//  ┌────────────────────────────────────────────┐
//  │ Magic  (4 B)   = 0x4C534D54  "LSMT"       │
//  ├────────────────────────────────────────────┤
//  │ Data Block                                  │
//  │   For each entry:                           │
//  │     key_len  (uint32_t, 4 B)               │
//  │     key      (key_len  bytes)              │
//  │     val_len  (uint32_t, 4 B)               │
//  │     val      (val_len  bytes)              │
//  ├────────────────────────────────────────────┤
//  │ Index Block  (sparse: 1 entry / 16 keys)   │
//  │   For each index entry:                     │
//  │     key_len  (uint32_t, 4 B)               │
//  │     key      (key_len  bytes)              │
//  │     offset   (uint64_t, 8 B) data-block    │
//  ├────────────────────────────────────────────┤
//  │ Bloom Block                                 │
//  │   num_bits   (uint64_t, 8 B)               │
//  │   num_hashes (uint64_t, 8 B)               │
//  │   byte_count (uint64_t, 8 B)               │
//  │   bytes      (byte_count bytes)            │
//  ├────────────────────────────────────────────┤
//  │ Footer (fixed 44 B)                         │
//  │   index_block_offset (uint64_t, 8 B)       │
//  │   index_block_size   (uint64_t, 8 B)       │
//  │   bloom_block_offset (uint64_t, 8 B)       │
//  │   bloom_block_size   (uint64_t, 8 B)       │
//  │   data_crc32         (uint32_t, 4 B)       │
//  │   magic              (uint32_t, 4 B)       │
//  └────────────────────────────────────────────┘
//
// Design tradeoffs:
//   • Sparse index (1 entry per kIndexInterval = 16 keys): binary search into
//     the index narrows the disk read to a 16-key window; a linear scan within
//     that window finishes the lookup.  Avoids loading the full key set while
//     still bounding worst-case scan length.
//   • CRC32 over the data block detects bit-rot or silent corruption.
//   • Bloom filter per file: ~9.6 bits/key at 1% FPR → ~99% of Get() calls
//     for missing keys require 0 disk reads.
// ---------------------------------------------------------------------------

inline constexpr uint32_t kSSTableMagic  = 0x4C534D54u; // "LSMT"
inline constexpr int      kIndexInterval = 16;

// Metadata about an SSTable (used by CompactionEngine and LSMEngine).
struct SSTableMeta {
    std::string  path;
    std::string  smallest_key;
    std::string  largest_key;
    uint64_t     file_size{0};
    int          level{0};
    uint64_t     sequence{0};
};

// ---------------------------------------------------------------------------
// SSTableWriter
// ---------------------------------------------------------------------------

class SSTableWriter {
public:
    explicit SSTableWriter(const std::string& path);

    // Add a key-value pair.  Keys must be added in sorted order.
    void Add(const std::string& key, const std::string& value);

    // Flush to disk and return metadata.  Throws on I/O error.
    SSTableMeta Finish();

private:
    std::string                                  path_;
    std::vector<std::pair<std::string,std::string>> entries_;
};

// ---------------------------------------------------------------------------
// SSTableReader
// ---------------------------------------------------------------------------

class SSTableReader {
public:
    explicit SSTableReader(const std::string& path);
    ~SSTableReader() = default;

    // Point lookup.  Returns nullopt if key is definitely absent (bloom) or
    // not found after scanning the data block.
    std::optional<std::string> Get(const std::string& key);

    // Quick bloom filter check — no disk I/O.
    bool MayContain(const std::string& key) const;

    const SSTableMeta& Meta() const { return meta_; }

    // Full scan — used by compaction.  Returns all (key, value) pairs in order.
    std::vector<std::pair<std::string,std::string>> ReadAll();

private:
    struct IndexEntry {
        std::string key;
        uint64_t    data_offset;
    };

    std::string                    path_;
    SSTableMeta                    meta_;
    std::vector<IndexEntry>        index_;
    std::unique_ptr<BloomFilter>   bloom_;
    uint64_t                       data_block_end_{0}; // byte offset of index block start

    void                       LoadMeta();
    std::optional<std::string> ScanWindow(const std::string& key,
                                           uint64_t start_off,
                                           uint64_t end_off);
};
