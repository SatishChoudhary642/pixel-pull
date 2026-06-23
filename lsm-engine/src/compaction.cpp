#include "compaction.h"
#include "memtable.h"   // kTombstoneValue

#include <algorithm>
#include <cassert>
#include <filesystem>
#include <functional>
#include <queue>
#include <stdexcept>

// ===========================================================================
// Helpers
// ===========================================================================

size_t CompactionEngine::LevelBytes(const std::vector<SSTableMeta>& files) {
    size_t total = 0;
    for (const auto& m : files) total += m.file_size;
    return total;
}

size_t CompactionEngine::MaxBytesForLevel(int level) {
    if (level <= 0) return 0; // L0 is count-based
    size_t sz = kL1MaxBytes;
    for (int i = 1; i < level; ++i) sz *= kLevelMultiplier;
    return sz;
}

// ===========================================================================
// Constructor / Destructor
// ===========================================================================

CompactionEngine::CompactionEngine(const std::string& db_dir, NotifyFn notify)
    : db_dir_(db_dir), notify_(std::move(notify)),
      levels_(kMaxLevels)
{
    bg_thread_ = std::thread(&CompactionEngine::BackgroundLoop, this);
}

CompactionEngine::~CompactionEngine() {
    Stop();
}

// ===========================================================================
// Public API
// ===========================================================================

void CompactionEngine::AddSSTable(SSTableMeta meta, int level) {
    assert(level >= 0 && level < kMaxLevels);
    {
        std::lock_guard<std::mutex> lk(mu_);
        levels_[level].push_back(std::move(meta));
    }
    cv_.notify_all();
}

void CompactionEngine::RemoveSSTable(const std::string& path, int level) {
    assert(level >= 0 && level < kMaxLevels);
    std::lock_guard<std::mutex> lk(mu_);
    auto& lvl = levels_[level];
    lvl.erase(std::remove_if(lvl.begin(), lvl.end(),
                              [&](const SSTableMeta& m){ return m.path == path; }),
              lvl.end());
}

std::vector<SSTableMeta> CompactionEngine::GetLevel(int level) const {
    assert(level >= 0 && level < kMaxLevels);
    std::lock_guard<std::mutex> lk(mu_);
    return levels_[level];
}

void CompactionEngine::TriggerCompaction() {
    cv_.notify_all();
}

void CompactionEngine::Stop() {
    running_.store(false);
    cv_.notify_all();
    if (bg_thread_.joinable()) bg_thread_.join();
}

// ===========================================================================
// Background thread
// ===========================================================================

void CompactionEngine::BackgroundLoop() {
    while (running_.load()) {
        std::unique_lock<std::mutex> lk(mu_);
        cv_.wait(lk, [this]{ return !running_.load() || NeedsCompaction(); });
        if (!running_.load()) break;
        lk.unlock();

        // Find and compact the first level that needs it.
        for (int lvl = 0; lvl < kMaxLevels - 1; ++lvl) {
            bool needs = false;
            {
                std::lock_guard<std::mutex> g(mu_);
                if (lvl == 0)
                    needs = (static_cast<int>(levels_[0].size()) >= kL0MaxFiles);
                else
                    needs = (LevelBytes(levels_[lvl]) > MaxBytesForLevel(lvl));
            }
            if (needs) {
                CompactLevel(lvl);
                break;
            }
        }
    }
}

bool CompactionEngine::NeedsCompaction() const {
    // mu_ must be held by caller
    if (static_cast<int>(levels_[0].size()) >= kL0MaxFiles) return true;
    for (int lvl = 1; lvl < kMaxLevels - 1; ++lvl) {
        if (LevelBytes(levels_[lvl]) > MaxBytesForLevel(lvl)) return true;
    }
    return false;
}

// ===========================================================================
// CompactLevel  (from_level → from_level+1)
// ===========================================================================

void CompactionEngine::CompactLevel(int from_level) {
    int to_level = from_level + 1;
    assert(to_level < kMaxLevels);

    // -----------------------------------------------------------------------
    // Collect all inputs under the lock, then release before doing I/O.
    // -----------------------------------------------------------------------
    std::vector<SSTableMeta> inputs;
    {
        std::lock_guard<std::mutex> lk(mu_);
        // All files in from_level are input.
        inputs = levels_[from_level];
        if (inputs.empty()) return;

        if (from_level > 0) {
            // For L1+: also include overlapping files from to_level.
            std::string lo = inputs.front().smallest_key;
            std::string hi = inputs.back().largest_key;
            for (const auto& m : levels_[to_level]) {
                if (m.smallest_key <= hi && m.largest_key >= lo)
                    inputs.push_back(m);
            }
        } else {
            // L0: include ALL L1 files (L0 may have overlapping ranges).
            for (const auto& m : levels_[to_level])
                inputs.push_back(m);
        }
    }

    // Decide whether it is safe to drop tombstones.
    // Drop iff there are no files in any level below to_level.
    bool drop_tombstones = true;
    {
        std::lock_guard<std::mutex> lk(mu_);
        for (int lvl = to_level + 1; lvl < kMaxLevels; ++lvl) {
            if (!levels_[lvl].empty()) { drop_tombstones = false; break; }
        }
    }

    MergeAndWrite(std::move(inputs), to_level, drop_tombstones);

    if (notify_) notify_();
}

// ===========================================================================
// MergeAndWrite  — k-way merge via min-heap
// ===========================================================================

void CompactionEngine::MergeAndWrite(std::vector<SSTableMeta> inputs,
                                      int to_level,
                                      bool drop_tombstones) {
    if (inputs.empty()) return;

    // -----------------------------------------------------------------------
    // Load all entries from each input file.
    // We assign a "file index" that encodes recency: lower index = newer.
    // For L0 compaction, newer files were appended last (higher sequence).
    // -----------------------------------------------------------------------
    std::sort(inputs.begin(), inputs.end(),
              [](const SSTableMeta& a, const SSTableMeta& b){
                  return a.sequence > b.sequence; // newest first = index 0
              });

    using Entry = std::tuple<std::string, std::string, int /*file_idx*/>;

    // Each "stream" is a sorted vector of entries with a cursor.
    struct Stream {
        std::vector<std::pair<std::string,std::string>> entries;
        std::size_t cursor{0};
        int         file_idx;
    };
    std::vector<Stream> streams;
    streams.reserve(inputs.size());

    for (int i = 0; i < static_cast<int>(inputs.size()); ++i) {
        SSTableReader rdr(inputs[i].path);
        Stream s;
        s.entries   = rdr.ReadAll();
        s.cursor    = 0;
        s.file_idx  = i;
        if (!s.entries.empty()) streams.push_back(std::move(s));
    }

    // Min-heap: (key, value, file_idx, stream_idx)
    // Priority: smallest key first; for equal keys smallest file_idx wins
    //           (newest data).
    using HeapEl = std::tuple<std::string, std::string, int, std::size_t>;
    auto cmp = [](const HeapEl& a, const HeapEl& b) {
        if (std::get<0>(a) != std::get<0>(b))
            return std::get<0>(a) > std::get<0>(b); // min-heap on key
        return std::get<2>(a) > std::get<2>(b);     // newer (lower idx) wins
    };
    std::priority_queue<HeapEl, std::vector<HeapEl>, decltype(cmp)> heap(cmp);

    // Seed heap with first element of each stream.
    for (std::size_t si = 0; si < streams.size(); ++si) {
        if (!streams[si].entries.empty()) {
            const auto& e = streams[si].entries[0];
            heap.push({e.first, e.second,
                       streams[si].file_idx, si});
        }
    }

    // -----------------------------------------------------------------------
    // Merge loop — emit deduplicated, tombstone-filtered output.
    // -----------------------------------------------------------------------
    std::string last_key_written;
    std::vector<std::pair<std::string,std::string>> output;

    while (!heap.empty()) {
        auto [key, val, file_idx, si] = heap.top();
        heap.pop();

        // Advance cursor in the stream we just popped.
        streams[si].cursor++;
        if (streams[si].cursor < streams[si].entries.size()) {
            const auto& ne = streams[si].entries[streams[si].cursor];
            heap.push({ne.first, ne.second, streams[si].file_idx, si});
        }

        // Skip stale duplicates (same key from an older file).
        if (key == last_key_written) continue;

        // Skip tombstones if we're at the bottom level.
        if (drop_tombstones && val == kTombstoneValue) {
            last_key_written = key;
            continue;
        }

        output.emplace_back(key, val);
        last_key_written = key;
    }

    // -----------------------------------------------------------------------
    // Write output to new SSTable(s) at to_level.
    // Split when output exceeds kOutputFileSizeTarget.
    // -----------------------------------------------------------------------
    std::vector<SSTableMeta> new_metas;
    std::size_t i = 0;
    while (i < output.size()) {
        uint64_t seq  = NextSequence();
        std::string p = db_dir_ + "/" +
                        std::to_string(to_level) + "_" +
                        std::to_string(seq) + ".sst";
        SSTableWriter wtr(p);
        std::size_t est_size = 0;
        while (i < output.size()) {
            wtr.Add(output[i].first, output[i].second);
            est_size += output[i].first.size() + output[i].second.size() + 8;
            ++i;
            if (est_size >= kOutputFileSizeTarget && i < output.size()) break;
        }
        SSTableMeta m = wtr.Finish();
        m.level    = to_level;
        m.sequence = seq;
        new_metas.push_back(m);
    }

    // -----------------------------------------------------------------------
    // Atomically update level state + delete old files.
    // -----------------------------------------------------------------------
    {
        std::lock_guard<std::mutex> lk(mu_);

        // Determine which file paths were involved (inputs).
        std::vector<std::string> input_paths;
        for (const auto& m : inputs) input_paths.push_back(m.path);

        // Remove input files from from_level and to_level.
        for (int lvl = 0; lvl < kMaxLevels; ++lvl) {
            levels_[lvl].erase(
                std::remove_if(levels_[lvl].begin(), levels_[lvl].end(),
                               [&](const SSTableMeta& m) {
                                   return std::find(input_paths.begin(),
                                                    input_paths.end(),
                                                    m.path)
                                          != input_paths.end();
                               }),
                levels_[lvl].end());
        }

        // Register new output files.
        for (auto& m : new_metas)
            levels_[to_level].push_back(m);
    }

    // Delete old files from disk (outside the lock).
    for (const auto& m : inputs) {
        std::error_code ec;
        std::filesystem::remove(m.path, ec);
    }
}
