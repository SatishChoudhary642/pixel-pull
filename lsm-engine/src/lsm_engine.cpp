#include "lsm_engine.h"

#include <algorithm>
#include <cassert>
#include <filesystem>
#include <stdexcept>
#include <string>

// ===========================================================================
// Path helpers
// ===========================================================================

std::string LSMEngine::WalPath() const {
    return config_.db_dir + "/current.wal";
}

std::string LSMEngine::SSTablePath(int level, uint64_t seq) const {
    return config_.db_dir + "/" +
           std::to_string(level) + "_" +
           std::to_string(seq) + ".sst";
}

// ===========================================================================
// Constructor / Destructor
// ===========================================================================

LSMEngine::LSMEngine(const LSMConfig& config) : config_(config) {
    std::filesystem::create_directories(config_.db_dir);

    // Compaction engine notifies us when it finishes a compaction.
    compaction_ = std::make_unique<CompactionEngine>(
        config_.db_dir,
        [this]{ readers_dirty_.store(true); });

    readers_.resize(kMaxLevels);

    Recover();

    if (!mem_) mem_ = std::make_unique<MemTable>(config_.memtable_max_size);
    if (!wal_) wal_ = std::make_unique<WAL>(WalPath(), config_.fsync_wal);
}

LSMEngine::~LSMEngine() {
    if (!closed_.load()) Close();
}

// ===========================================================================
// Crash recovery
// ===========================================================================

void LSMEngine::Recover() {
    // --- 1. Scan for existing SSTable files ---
    uint64_t max_seq = 0;
    for (const auto& entry :
         std::filesystem::directory_iterator(config_.db_dir)) {
        if (entry.path().extension() != ".sst") continue;
        std::string stem = entry.path().stem().string();
        // Filename pattern: <level>_<seq>.sst
        auto sep = stem.find('_');
        if (sep == std::string::npos) continue;
        int      level = std::stoi(stem.substr(0, sep));
        uint64_t seq   = std::stoull(stem.substr(sep + 1));
        if (level < 0 || level >= kMaxLevels) continue;

        try {
            SSTableReader rdr(entry.path().string());
            SSTableMeta   meta = rdr.Meta();
            meta.level    = level;
            meta.sequence = seq;
            meta.path     = entry.path().string();
            compaction_->AddSSTable(meta, level);
            max_seq = std::max(max_seq, seq);
        } catch (...) {
            // Corrupted file — skip; it will not be served.
        }
    }
    if (max_seq > 0) compaction_->SetSequence(max_seq + 1);

    RebuildReadersLocked();

    // --- 2. Replay WAL ---
    mem_ = std::make_unique<MemTable>(config_.memtable_max_size);
    WAL::Replay(WalPath(),
                [this](const std::string& k, const std::string& v) {
                    mem_->Put(k, v);
                });
}

// ===========================================================================
// Rebuild SSTable readers
// ===========================================================================

void LSMEngine::RebuildReadersLocked() {
    readers_.assign(kMaxLevels, {});

    for (int lvl = 0; lvl < kMaxLevels; ++lvl) {
        auto metas = compaction_->GetLevel(lvl);

        if (lvl == 0) {
            // L0: newest (highest sequence) first so reads prefer recent data.
            std::sort(metas.begin(), metas.end(),
                      [](const SSTableMeta& a, const SSTableMeta& b){
                          return a.sequence > b.sequence;
                      });
        } else {
            // L1+: sorted by smallest key (non-overlapping).
            std::sort(metas.begin(), metas.end(),
                      [](const SSTableMeta& a, const SSTableMeta& b){
                          return a.smallest_key < b.smallest_key;
                      });
        }

        for (const auto& m : metas) {
            try {
                readers_[lvl].push_back(
                    std::make_shared<SSTableReader>(m.path));
            } catch (...) { /* skip corrupt files */ }
        }
    }
    readers_dirty_.store(false);
}

// ===========================================================================
// Write path: Put / Delete
// ===========================================================================

void LSMEngine::Put(const std::string& key, const std::string& value) {
    std::unique_lock<std::shared_mutex> lk(rw_mu_);

    // Lazily rebuild readers if compaction changed level state.
    if (readers_dirty_.load()) RebuildReadersLocked();

    wal_->Append(key, value);
    mem_->Put(key, value);
    MaybeFlushLocked();
}

void LSMEngine::Delete(const std::string& key) {
    Put(key, kTombstoneValue);
}

// ===========================================================================
// MaybeFlush / Flush
// ===========================================================================

void LSMEngine::MaybeFlushLocked() {
    if (mem_->IsFull()) FlushLocked();
}

void LSMEngine::Flush() {
    std::unique_lock<std::shared_mutex> lk(rw_mu_);
    if (!mem_->Empty()) FlushLocked();
}

void LSMEngine::FlushLocked() {
    // Rotate: current MemTable → immutable.
    imm_ = std::move(mem_);
    mem_ = std::make_unique<MemTable>(config_.memtable_max_size);

    // Write immutable MemTable to a new L0 SSTable.
    uint64_t seq = compaction_->NextSequence();
    std::string sst_path = SSTablePath(0, seq);

    SSTableWriter wtr(sst_path);
    for (const auto& [k, v] : imm_->Data())
        wtr.Add(k, v);

    SSTableMeta meta = wtr.Finish();
    meta.level    = 0;
    meta.sequence = seq;

    compaction_->AddSSTable(meta, 0);
    compaction_->TriggerCompaction();

    imm_.reset();

    // Rotate WAL: close old, delete it, open fresh.
    wal_->Close();
    WAL::DeleteFile(WalPath());
    wal_ = std::make_unique<WAL>(WalPath(), config_.fsync_wal);

    RebuildReadersLocked();
}

// ===========================================================================
// Read path: Get
// ===========================================================================

std::optional<std::string> LSMEngine::Get(const std::string& key) {
    // First try with a shared (read) lock.
    {
        std::shared_lock<std::shared_mutex> lk(rw_mu_);

        if (!readers_dirty_.load()) {
            // --- MemTable ---
            if (auto v = mem_->Get(key)) {
                if (*v == kTombstoneValue) return std::nullopt;
                return v;
            }
            // --- Immutable MemTable ---
            if (imm_) {
                if (auto v = imm_->Get(key)) {
                    if (*v == kTombstoneValue) return std::nullopt;
                    return v;
                }
            }
            // --- SSTables level by level ---
            for (int lvl = 0; lvl < kMaxLevels; ++lvl) {
                for (auto& rdr : readers_[lvl]) {
                    if (!rdr->MayContain(key)) continue;
                    if (auto v = rdr->Get(key)) {
                        if (*v == kTombstoneValue) return std::nullopt;
                        return v;
                    }
                }
            }
            return std::nullopt;
        }
    }

    // Compaction changed level state — upgrade to write lock to rebuild.
    std::unique_lock<std::shared_mutex> lk(rw_mu_);
    if (readers_dirty_.load()) RebuildReadersLocked();

    // --- MemTable ---
    if (auto v = mem_->Get(key)) {
        if (*v == kTombstoneValue) return std::nullopt;
        return v;
    }
    // --- Immutable MemTable ---
    if (imm_) {
        if (auto v = imm_->Get(key)) {
            if (*v == kTombstoneValue) return std::nullopt;
            return v;
        }
    }
    // --- SSTables ---
    for (int lvl = 0; lvl < kMaxLevels; ++lvl) {
        for (auto& rdr : readers_[lvl]) {
            if (!rdr->MayContain(key)) continue;
            if (auto v = rdr->Get(key)) {
                if (*v == kTombstoneValue) return std::nullopt;
                return v;
            }
        }
    }
    return std::nullopt;
}

// ===========================================================================
// Close
// ===========================================================================

void LSMEngine::Close() {
    if (closed_.exchange(true)) return;

    {
        std::unique_lock<std::shared_mutex> lk(rw_mu_);
        if (mem_ && !mem_->Empty()) FlushLocked();
    }

    compaction_->Stop();

    if (wal_) wal_->Close();
}
