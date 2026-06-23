#pragma once
#include <cstddef>
#include <map>
#include <optional>
#include <string>

// ---------------------------------------------------------------------------
// Constants shared by MemTable, SSTableWriter, and CompactionEngine.
// ---------------------------------------------------------------------------
inline constexpr std::size_t kDefaultMemTableMaxSize = 4ULL * 1024 * 1024; // 4 MB
inline constexpr const char* kTombstoneValue         = "__TOMBSTONE__";

// ---------------------------------------------------------------------------
// MemTable — in-memory write buffer backed by an ordered std::map.
//
// Design tradeoffs:
//   • std::map (red-black tree): O(log n) insert/lookup, guaranteed sorted
//     order for SSTable flush. Simple, deterministic.
//   • Alternative: concurrent skip list (e.g. RocksDB's SkipListRep) —
//     allows lock-free concurrent writes but is significantly more complex.
//   • Tombstones are written as kTombstoneValue so Delete() is a Put().
//   • Size is tracked as Σ(key.size() + value.size()) — approximates the
//     actual memory used by the map without an expensive sizeof(node) walk.
// ---------------------------------------------------------------------------
class MemTable {
public:
    explicit MemTable(std::size_t max_size = kDefaultMemTableMaxSize);

    void                     Put(const std::string& key, const std::string& value);
    std::optional<std::string> Get(const std::string& key) const;
    void                     Delete(const std::string& key);

    bool        IsFull()    const { return size_bytes_ >= max_size_; }
    std::size_t SizeBytes() const { return size_bytes_; }
    std::size_t Count()     const { return data_.size(); }
    bool        Empty()     const { return data_.empty(); }

    // Ordered iteration for SSTable flush.
    const std::map<std::string, std::string>& Data() const { return data_; }

    void Clear();

private:
    std::map<std::string, std::string> data_;
    std::size_t                        max_size_;
    std::size_t                        size_bytes_{0};

    // Adjust size_bytes_ by ±(key.size() + value.size()).
    void Account(const std::string& key, const std::string& value, int sign);
};
