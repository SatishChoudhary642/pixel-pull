#pragma once
#include <atomic>
#include <memory>
#include <optional>
#include <shared_mutex>
#include <string>
#include <vector>

#include "compaction.h"
#include "memtable.h"
#include "sstable.h"
#include "wal.h"

// ---------------------------------------------------------------------------
// LSMEngine configuration
// ---------------------------------------------------------------------------
struct LSMConfig {
    std::string db_dir;
    size_t      memtable_max_size = 4ULL * 1024 * 1024; // 4 MB
    bool        fsync_wal         = false;
    double      bloom_fpr         = 0.01;               // 1%
};

// ---------------------------------------------------------------------------
// LSMEngine — top-level public API
//
// Thread safety:
//   All public methods (Put / Get / Delete / Flush / Close) are thread-safe.
//   Internally a std::shared_mutex separates readers from writers.
//
// Read path:
//   MemTable → Immutable MemTable → L0 (newest first) → L1 → … → Ln
//   A Bloom filter gates every SSTable lookup to avoid unnecessary disk reads.
//
// Write path:
//   WAL.Append → MemTable.Put → MaybeFlush()
//   MaybeFlush: if MemTable >= threshold, move to imm_, open new MemTable +
//               WAL, FlushImmutable() synchronously.
//
// Crash recovery:
//   On startup, scan db_dir for *.sst files (rebuild level state), then
//   replay current.wal into a fresh MemTable.
// ---------------------------------------------------------------------------
class LSMEngine {
public:
    explicit LSMEngine(const LSMConfig& config);
    ~LSMEngine();

    void                       Put(const std::string& key,
                                   const std::string& value);
    std::optional<std::string> Get(const std::string& key);
    void                       Delete(const std::string& key);

    // Force current MemTable → SSTable (useful in tests / benchmarks).
    void Flush();

    void Close();

private:
    LSMConfig config_;

    // The shared_mutex protects mem_, imm_, wal_, and readers_.
    // Writers take unique_lock; readers take shared_lock.
    mutable std::shared_mutex rw_mu_;

    std::unique_ptr<MemTable>         mem_;
    std::unique_ptr<MemTable>         imm_;  // immutable; non-null during flush
    std::unique_ptr<WAL>              wal_;
    std::unique_ptr<CompactionEngine> compaction_;

    // Readers grouped by level; L0 is sorted newest-first.
    std::vector<std::vector<std::shared_ptr<SSTableReader>>> readers_;

    // Set when the compaction engine changes level state.
    std::atomic<bool> readers_dirty_{false};
    std::atomic<bool> closed_{false};

    // --- Internal helpers (not thread-safe; caller holds appropriate lock) ---

    // Scan db_dir for *.sst + current.wal and rebuild state.
    void Recover();

    // Flush mem_ → SSTable (must hold unique_lock).
    void FlushLocked();

    // Rebuild readers_ from compaction engine (must hold unique_lock).
    void RebuildReadersLocked();

    // Check MemTable size threshold.
    void MaybeFlushLocked();

    std::string WalPath()                       const;
    std::string SSTablePath(int level, uint64_t seq) const;
};
