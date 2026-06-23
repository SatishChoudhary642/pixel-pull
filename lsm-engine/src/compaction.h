#pragma once
#include <atomic>
#include <condition_variable>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include "sstable.h"

// ---------------------------------------------------------------------------
// Level sizing
// ---------------------------------------------------------------------------
inline constexpr int    kL0MaxFiles      = 4;
inline constexpr size_t kL1MaxBytes      = 10ULL * 1024 * 1024;  // 10 MB
inline constexpr int    kMaxLevels       = 7;
inline constexpr int    kLevelMultiplier = 10;

// ---------------------------------------------------------------------------
// CompactionEngine
//
// Strategy: Leveled compaction (identical to LevelDB).
//
// • L0 may hold up to kL0MaxFiles SSTables; overlapping key ranges are OK.
//   New SSTables from MemTable flushes land here.
// • L1 is capped at kL1MaxBytes.  Each subsequent level is 10× larger.
// • When a level exceeds its limit a background thread merges all files from
//   that level with the overlapping files of the next level and writes new
//   SSTables to level+1, then deletes the old files.
// • K-way merge via std::priority_queue (min-heap) over pre-loaded iterators.
// • Tombstones (kTombstoneValue) are dropped only when compacting into the
//   deepest occupied level (safe because no older copy can exist further down).
//
// Tradeoffs vs Size-Tiered (STCS):
//   • Leveled: better read amplification (~1 I/O per level), more predictable
//     space amplification (~1.1×), but higher write amplification.
//   • STCS: much lower write amplification, bad for read-heavy workloads.
// ---------------------------------------------------------------------------
class CompactionEngine {
public:
    // Called on the background thread after compaction changes level state.
    using NotifyFn = std::function<void()>;

    explicit CompactionEngine(const std::string& db_dir,
                              NotifyFn notify = nullptr);
    ~CompactionEngine();

    // Register a new SSTable at a given level (thread-safe).
    void AddSSTable(SSTableMeta meta, int level);

    // Remove an SSTable record by path (thread-safe).
    void RemoveSSTable(const std::string& path, int level);

    // Snapshot of level contents (thread-safe).
    std::vector<SSTableMeta> GetLevel(int level) const;

    // Wake the background compaction thread.
    void TriggerCompaction();

    // Drain + join background thread.
    void Stop();

    // Monotonically increasing file-sequence counter.
    uint64_t NextSequence()    { return sequence_.fetch_add(1); }
    uint64_t CurrentSequence() const { return sequence_.load(); }
    void     SetSequence(uint64_t s) { sequence_.store(s); }

private:
    std::string db_dir_;
    NotifyFn    notify_;

    mutable std::mutex                        mu_;
    std::vector<std::vector<SSTableMeta>>     levels_; // [level][file]

    std::thread             bg_thread_;
    std::condition_variable cv_;
    std::atomic<bool>       running_{true};
    std::atomic<uint64_t>   sequence_{1};

    // Background thread entry point.
    void BackgroundLoop();

    // Returns true if any level exceeds its limit (call with mu_ held).
    bool NeedsCompaction() const;

    // Compact level → level+1.
    void CompactLevel(int from_level);

    // K-way merge of 'inputs'; deduplicates (keep newest by sequence order),
    // drops tombstones when safe, writes to 'to_level'.
    void MergeAndWrite(std::vector<SSTableMeta> inputs,
                       int to_level,
                       bool drop_tombstones);

    static size_t LevelBytes(const std::vector<SSTableMeta>& files);
    static size_t MaxBytesForLevel(int level);
    static constexpr size_t kOutputFileSizeTarget = 2ULL * 1024 * 1024; // 2 MB
};
