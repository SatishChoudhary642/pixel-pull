#include "memtable.h"

// ---------------------------------------------------------------------------
// MemTable implementation
// ---------------------------------------------------------------------------

MemTable::MemTable(std::size_t max_size) : max_size_(max_size) {}

// ---------------------------------------------------------------------------
// Private helper
// ---------------------------------------------------------------------------

void MemTable::Account(const std::string& key, const std::string& value,
                       int sign) {
    if (sign > 0) {
        size_bytes_ += key.size() + value.size();
    } else {
        const std::size_t delta = key.size() + value.size();
        size_bytes_ = (size_bytes_ >= delta) ? size_bytes_ - delta : 0;
    }
}

// ---------------------------------------------------------------------------
// Public API
// ---------------------------------------------------------------------------

void MemTable::Put(const std::string& key, const std::string& value) {
    auto it = data_.find(key);
    if (it != data_.end()) {
        // Remove the old size contribution before overwriting.
        Account(key, it->second, -1);
        it->second = value;
    } else {
        data_[key] = value;
    }
    Account(key, value, +1);
}

std::optional<std::string> MemTable::Get(const std::string& key) const {
    auto it = data_.find(key);
    if (it == data_.end()) return std::nullopt;
    return it->second;
}

void MemTable::Delete(const std::string& key) {
    Put(key, kTombstoneValue);
}

void MemTable::Clear() {
    data_.clear();
    size_bytes_ = 0;
}
