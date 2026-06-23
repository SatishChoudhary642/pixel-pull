#include "sstable.h"

#include <array>
#include <cassert>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <stdexcept>

// ---------------------------------------------------------------------------
// Portable CRC32 (IEEE 802.3)  — duplicated here to keep sstable.cpp
//   self-contained; avoids exposing WAL internals.
// ---------------------------------------------------------------------------

namespace {

const std::array<uint32_t, 256>& Crc32Table() {
    static const std::array<uint32_t, 256> tbl = []() {
        std::array<uint32_t, 256> t{};
        for (uint32_t i = 0; i < 256; ++i) {
            uint32_t c = i;
            for (int j = 0; j < 8; ++j)
                c = (c & 1) ? (0xEDB88320u ^ (c >> 1)) : (c >> 1);
            t[i] = c;
        }
        return t;
    }();
    return tbl;
}

uint32_t Crc32(const void* data, std::size_t len) {
    const auto& tbl = Crc32Table();
    uint32_t crc = 0xFFFFFFFFu;
    const auto* p = static_cast<const uint8_t*>(data);
    for (std::size_t i = 0; i < len; ++i)
        crc = tbl[(crc ^ p[i]) & 0xFF] ^ (crc >> 8);
    return crc ^ 0xFFFFFFFFu;
}

// ---------------------------------------------------------------------------
// Typed read/write helpers  (little-endian — x86/arm default)
// ---------------------------------------------------------------------------

template<typename T>
void WriteLE(std::ofstream& f, T val) {
    f.write(reinterpret_cast<const char*>(&val), sizeof(T));
}

template<typename T>
T ReadLE(std::ifstream& f) {
    T val{};
    f.read(reinterpret_cast<char*>(&val), sizeof(T));
    return val;
}

void WriteBytes(std::ofstream& f, const std::string& s) {
    f.write(s.data(), static_cast<std::streamsize>(s.size()));
}

std::string ReadBytes(std::ifstream& f, std::size_t n) {
    std::string s(n, '\0');
    f.read(s.data(), static_cast<std::streamsize>(n));
    return s;
}

} // anonymous namespace

// ===========================================================================
// SSTableWriter
// ===========================================================================

SSTableWriter::SSTableWriter(const std::string& path) : path_(path) {}

void SSTableWriter::Add(const std::string& key, const std::string& value) {
    entries_.emplace_back(key, value);
}

SSTableMeta SSTableWriter::Finish() {
    if (entries_.empty())
        throw std::runtime_error("SSTableWriter::Finish called with no entries");

    std::ofstream f(path_, std::ios::binary | std::ios::trunc);
    if (!f) throw std::runtime_error("SSTableWriter: cannot open " + path_);

    // ------------------------------------------------------------------
    // 1. Magic
    // ------------------------------------------------------------------
    WriteLE<uint32_t>(f, kSSTableMagic);

    // ------------------------------------------------------------------
    // 2. Data block  +  build index + bloom in parallel
    // ------------------------------------------------------------------
    struct IndexEntry { std::string key; uint64_t offset; };
    std::vector<IndexEntry> index;
    BloomFilter bloom(entries_.size(), 0.01);

    uint32_t data_crc = 0xFFFFFFFFu;
    const auto& tbl   = Crc32Table();

    auto crc_update = [&](const void* buf, std::size_t n) {
        const auto* p = static_cast<const uint8_t*>(buf);
        for (std::size_t i = 0; i < n; ++i)
            data_crc = tbl[(data_crc ^ p[i]) & 0xFF] ^ (data_crc >> 8);
    };

    int entry_idx = 0;
    for (const auto& [k, v] : entries_) {
        // Record offset BEFORE writing this entry.
        uint64_t offset = static_cast<uint64_t>(f.tellp());

        // Sparse index: record every kIndexInterval-th entry.
        if (entry_idx % kIndexInterval == 0)
            index.push_back({k, offset});

        // Bloom filter.
        bloom.Add(k);

        // Write key.
        uint32_t klen = static_cast<uint32_t>(k.size());
        uint32_t vlen = static_cast<uint32_t>(v.size());

        WriteLE<uint32_t>(f, klen);  crc_update(&klen, 4);
        WriteBytes(f, k);            crc_update(k.data(), k.size());
        WriteLE<uint32_t>(f, vlen);  crc_update(&vlen, 4);
        WriteBytes(f, v);            crc_update(v.data(), v.size());

        ++entry_idx;
    }
    data_crc ^= 0xFFFFFFFFu;

    // ------------------------------------------------------------------
    // 3. Index block
    // ------------------------------------------------------------------
    uint64_t index_block_offset = static_cast<uint64_t>(f.tellp());
    for (const auto& ie : index) {
        uint32_t klen = static_cast<uint32_t>(ie.key.size());
        WriteLE<uint32_t>(f, klen);
        WriteBytes(f, ie.key);
        WriteLE<uint64_t>(f, ie.offset);
    }
    uint64_t index_block_size =
        static_cast<uint64_t>(f.tellp()) - index_block_offset;

    // ------------------------------------------------------------------
    // 4. Bloom filter block
    // ------------------------------------------------------------------
    uint64_t bloom_block_offset = static_cast<uint64_t>(f.tellp());
    uint64_t num_bits   = static_cast<uint64_t>(bloom.GetNumBits());
    uint64_t num_hashes = static_cast<uint64_t>(bloom.GetNumHashes());
    uint64_t byte_count = static_cast<uint64_t>(bloom.GetBits().size());

    WriteLE<uint64_t>(f, num_bits);
    WriteLE<uint64_t>(f, num_hashes);
    WriteLE<uint64_t>(f, byte_count);
    f.write(reinterpret_cast<const char*>(bloom.GetBits().data()),
            static_cast<std::streamsize>(byte_count));

    uint64_t bloom_block_size =
        static_cast<uint64_t>(f.tellp()) - bloom_block_offset;

    // ------------------------------------------------------------------
    // 5. Footer (44 bytes fixed)
    // ------------------------------------------------------------------
    WriteLE<uint64_t>(f, index_block_offset);
    WriteLE<uint64_t>(f, index_block_size);
    WriteLE<uint64_t>(f, bloom_block_offset);
    WriteLE<uint64_t>(f, bloom_block_size);
    WriteLE<uint32_t>(f, data_crc);
    WriteLE<uint32_t>(f, kSSTableMagic);

    f.flush();
    if (!f) throw std::runtime_error("SSTableWriter: I/O error writing " + path_);

    SSTableMeta meta;
    meta.path         = path_;
    meta.smallest_key = entries_.front().first;
    meta.largest_key  = entries_.back().first;
    meta.file_size    = static_cast<uint64_t>(f.tellp());
    // level and sequence filled in by the caller.
    return meta;
}

// ===========================================================================
// SSTableReader
// ===========================================================================

SSTableReader::SSTableReader(const std::string& path) : path_(path) {
    LoadMeta();
}

// ---------------------------------------------------------------------------
// LoadMeta  — read footer, index, bloom filter, smallest/largest keys
// ---------------------------------------------------------------------------

void SSTableReader::LoadMeta() {
    std::ifstream f(path_, std::ios::binary);
    if (!f) throw std::runtime_error("SSTableReader: cannot open " + path_);

    // --- Check magic at start ---
    uint32_t magic = ReadLE<uint32_t>(f);
    if (magic != kSSTableMagic)
        throw std::runtime_error("SSTableReader: bad magic in " + path_);

    // --- Read footer (last 44 bytes) ---
    constexpr std::streamoff kFooterSize = 44;
    f.seekg(-kFooterSize, std::ios::end);
    uint64_t idx_off   = ReadLE<uint64_t>(f);
    uint64_t idx_size  = ReadLE<uint64_t>(f);
    uint64_t blm_off   = ReadLE<uint64_t>(f);
    uint64_t blm_size  = ReadLE<uint64_t>(f);
    /*data_crc32 =*/     ReadLE<uint32_t>(f);   // TODO: validate on open
    uint32_t ftr_magic = ReadLE<uint32_t>(f);
    if (ftr_magic != kSSTableMagic)
        throw std::runtime_error("SSTableReader: bad footer magic in " + path_);

    data_block_end_ = idx_off;
    (void)idx_size;
    (void)blm_size;

    // --- Read index block ---
    f.seekg(static_cast<std::streamoff>(idx_off), std::ios::beg);
    while (static_cast<uint64_t>(f.tellg()) < blm_off) {
        uint32_t klen = ReadLE<uint32_t>(f);
        if (!f) break;
        std::string key = ReadBytes(f, klen);
        uint64_t    off = ReadLE<uint64_t>(f);
        index_.push_back({std::move(key), off});
    }

    // --- Read bloom filter block ---
    f.seekg(static_cast<std::streamoff>(blm_off), std::ios::beg);
    uint64_t num_bits   = ReadLE<uint64_t>(f);
    uint64_t num_hashes = ReadLE<uint64_t>(f);
    uint64_t byte_count = ReadLE<uint64_t>(f);
    std::vector<uint8_t> bits(byte_count);
    f.read(reinterpret_cast<char*>(bits.data()),
           static_cast<std::streamsize>(byte_count));
    bloom_ = std::make_unique<BloomFilter>(
        std::move(bits),
        static_cast<std::size_t>(num_bits),
        static_cast<std::size_t>(num_hashes));

    // --- Metadata: smallest key from first index entry ---
    if (!index_.empty()) {
        meta_.smallest_key = index_.front().key;

        // Largest key: scan from last index entry to data_block_end_.
        // We only scan the last window (≤ kIndexInterval entries).
        std::string last_key;
        f.seekg(static_cast<std::streamoff>(index_.back().data_offset),
                std::ios::beg);
        while (static_cast<uint64_t>(f.tellg()) < data_block_end_) {
            uint32_t klen = ReadLE<uint32_t>(f);
            if (!f) break;
            std::string k = ReadBytes(f, klen);
            uint32_t vlen = ReadLE<uint32_t>(f);
            f.seekg(vlen, std::ios::cur);
            last_key = std::move(k);
        }
        meta_.largest_key = last_key.empty() ? meta_.smallest_key : last_key;
    }

    meta_.path      = path_;
    meta_.file_size = static_cast<uint64_t>(
        std::filesystem::file_size(path_));
}

// ---------------------------------------------------------------------------
// MayContain — bloom filter only, no disk I/O
// ---------------------------------------------------------------------------

bool SSTableReader::MayContain(const std::string& key) const {
    return bloom_ && bloom_->MayContain(key);
}

// ---------------------------------------------------------------------------
// Get — binary-search index → scan data window
// ---------------------------------------------------------------------------

std::optional<std::string> SSTableReader::Get(const std::string& key) {
    if (!MayContain(key)) return std::nullopt;

    if (index_.empty()) {
        return ScanWindow(key, 4 /*after magic*/, data_block_end_);
    }

    // Find the last index entry with key ≤ target key.
    // std::upper_bound gives first entry > target; step back one.
    auto it = std::upper_bound(
        index_.begin(), index_.end(), key,
        [](const std::string& k, const IndexEntry& e) { return k < e.key; });

    uint64_t start_off;
    uint64_t end_off;

    if (it == index_.begin()) {
        // Target key is before all indexed keys → scan from beginning.
        start_off = 4; // skip magic
        end_off   = (index_.size() > 1) ? index_[1].data_offset : data_block_end_;
    } else {
        --it;
        start_off = it->data_offset;
        auto next = std::next(it);
        end_off   = (next != index_.end()) ? next->data_offset : data_block_end_;
    }

    return ScanWindow(key, start_off, end_off);
}

// ---------------------------------------------------------------------------
// ScanWindow — linear scan of a data-block window
// ---------------------------------------------------------------------------

std::optional<std::string> SSTableReader::ScanWindow(
    const std::string& key, uint64_t start_off, uint64_t end_off)
{
    std::ifstream f(path_, std::ios::binary);
    if (!f) return std::nullopt;
    f.seekg(static_cast<std::streamoff>(start_off), std::ios::beg);

    while (static_cast<uint64_t>(f.tellg()) < end_off) {
        uint32_t klen = ReadLE<uint32_t>(f);
        if (!f) break;
        std::string k = ReadBytes(f, klen);
        uint32_t vlen = ReadLE<uint32_t>(f);
        std::string v = ReadBytes(f, vlen);

        if (k == key) return v;
        if (k > key)  break; // sorted — no need to continue
    }
    return std::nullopt;
}

// ---------------------------------------------------------------------------
// ReadAll — full scan for compaction
// ---------------------------------------------------------------------------

std::vector<std::pair<std::string,std::string>> SSTableReader::ReadAll() {
    std::vector<std::pair<std::string,std::string>> result;

    std::ifstream f(path_, std::ios::binary);
    if (!f) return result;

    f.seekg(4, std::ios::beg); // skip magic
    while (static_cast<uint64_t>(f.tellg()) < data_block_end_) {
        uint32_t klen = ReadLE<uint32_t>(f);
        if (!f) break;
        std::string k = ReadBytes(f, klen);
        uint32_t vlen = ReadLE<uint32_t>(f);
        std::string v = ReadBytes(f, vlen);
        result.emplace_back(std::move(k), std::move(v));
    }
    return result;
}
