# LSM-Tree Key-Value Storage Engine

A production-quality **Log-Structured Merge-Tree (LSM-Tree)** key-value storage engine written in **C++17**. This is the core data structure powering LevelDB, RocksDB, Apache Cassandra, and HBase — built from scratch with every layer documented and tradeoffs explained.

---

## Architecture

```
┌─────────────────────────────────────────────────────────────┐
│                        Write Path                           │
│  Put(k,v) ──► WAL.Append ──► MemTable.Put                  │
│                                    │ (size ≥ 4 MB)          │
│                               FlushLocked()                  │
│                                    │                         │
│                              SSTableWriter (L0)              │
│                                    │ (L0 ≥ 4 files)          │
│                         CompactionEngine (background)        │
│                           └──► L0 → L1 → L2 → …            │
└─────────────────────────────────────────────────────────────┘

┌─────────────────────────────────────────────────────────────┐
│                         Read Path                           │
│  Get(k) ──► MemTable ──► ImmutableMem ──► L0 (newest→old) │
│               └──► L1 ──► … ──► Ln                         │
│          Each SSTable: BloomFilter check → SparseIndex      │
│                         → DataBlock scan                    │
└─────────────────────────────────────────────────────────────┘
```

---

## Components

### 1. MemTable (`src/memtable.h/cpp`)
In-memory sorted write buffer backed by `std::map<string,string>`.

| Design Decision | Choice | Rationale |
|---|---|---|
| Data structure | `std::map` (red-black tree) | O(log n) writes, guaranteed sorted iteration for flush |
| Size tracking | Σ(key.size() + val.size()) | No per-node sizeof() walk needed |
| Tombstone | `"__TOMBSTONE__"` sentinel | Allows Delete() to be a Put() — uniform write path |
| Threshold | 4 MB (configurable) | Matches LevelDB default; tunable via `LSMConfig` |

**Alternative**: Skip list (RocksDB uses `InlineSkipList`) offers lock-free concurrent writes but is ~5× more complex to implement correctly.

---

### 2. Write-Ahead Log (`src/wal.h/cpp`)
Binary append-only log for crash recovery.

**Record format:**
```
┌──────────┬─────────┬─────────┬──────────┬───────────┐
│ CRC32 4B │ klen 4B │ vlen 4B │ key klen │ val vlen  │
└──────────┴─────────┴─────────┴──────────┴───────────┘
```

| Design Decision | Choice | Rationale |
|---|---|---|
| CRC algorithm | IEEE 802.3 (table-based) | Fast, hardware-friendly, detects partial writes |
| fsync mode | Configurable per write | fsync=true: ~15× slower but power-loss safe |
| WAL rotation | On every MemTable flush | Old WAL deleted after SSTable persisted → bounded WAL size |
| Replay strategy | Stop at first corrupt record | Partial writes are safe; don't replay corrupt data |

**Tradeoff:** Group commit (batch fsync across multiple writers) would improve write throughput ~10× with negligible durability loss — natural next step.

---

### 3. SSTable (`src/sstable.h/cpp`)
Immutable on-disk sorted string table.

**File layout:**
```
[Magic 4B "LSMT"] [Data Block] [Index Block] [Bloom Block] [Footer 44B]
```

**Footer:**
```
index_offset(8B) | index_size(8B) | bloom_offset(8B) | bloom_size(8B) | crc32(4B) | magic(4B)
```

| Design Decision | Choice | Rationale |
|---|---|---|
| Index density | 1 entry per 16 keys | Binary search → 16-key window → linear scan; O(log n/16) disk seeks |
| Block format | Raw binary (host-endian) | Fastest I/O; no serialization overhead |
| Bloom filter | Per-file, persisted | ~99% of missing-key reads skip the disk entirely |
| CRC32 | Over data block at write time | Detects bit-rot; verified on open |

---

### 4. Bloom Filter (`src/bloom_filter.h/cpp`)
Probabilistic set membership with no false negatives.

**Mathematics:**
- Optimal bit count: `m = ⌈ -n·ln(p) / ln(2)² ⌉`
- Optimal hash functions: `k = round( m/n · ln(2) )`
- At 1% FPR: **~9.6 bits/key, ~7 hash functions**

**Double-hashing scheme:**
```
h_i(key) = ( FNV-1a(key) + i · MurmurHash64(key) ) mod m
```

Uses only 2 hash computations regardless of k — O(1) per probe.

| Design Decision | Choice | Rationale |
|---|---|---|
| Hash functions | FNV-1a + MurmurHash64 | Non-cryptographic, fast, good distribution |
| Combining | Double hashing | Proven near-optimal with only 2 hash computations |
| Storage | `vector<uint8_t>` | Compact; persisted verbatim into SSTable bloom block |

---

### 5. Compaction Engine (`src/compaction.h/cpp`)
Background thread that maintains level health.

**Strategy: Leveled Compaction** (same as LevelDB)

| Level | Limit |
|---|---|
| L0 | 4 files (count-based; overlapping ranges OK) |
| L1 | 10 MB |
| L2 | 100 MB |
| L3 | 1 GB |
| Ln | 10× Ln-1 |

**K-way merge algorithm:**
1. Load all entries from all input SSTables into per-file vectors
2. Seed a `std::priority_queue` min-heap with the first entry of each file
3. Pop minimum; advance that file's cursor; re-seed heap
4. Skip duplicate keys (keep newest by sequence number)
5. Drop tombstones only when compacting into the deepest occupied level

| Design Decision | Choice | Rationale |
|---|---|---|
| Strategy | Leveled | Better read amplification vs tiered; predictable space |
| K-way merge | Min-heap | O(k log k) per pop; no full sort of merged set |
| Tombstone removal | Only at deepest level | Safe: no older data exists below |
| Concurrency | Background `std::thread` + `condition_variable` | Non-blocking; doesn't stall write path |
| Output size | 2 MB per output file | Limits individual file size for better range query performance |

**Leveled vs Size-Tiered (STCS):**
- Leveled: read amplification = O(levels) ≈ 7, space amplification ≈ 1.1×, higher write amp
- STCS: lower write amp, but read amp can be O(total files), space amp ≈ 2×

---

### 6. LSM Engine (`src/lsm_engine.h/cpp`)
Top-level coordinator providing the public API.

```cpp
class LSMEngine {
    void Put(const string& key, const string& value);
    optional<string> Get(const string& key);
    void Delete(const string& key);   // writes tombstone
    void Flush();                      // force MemTable → SSTable
    void Close();
};
```

**Thread safety:** `std::shared_mutex` — multiple concurrent readers, exclusive writers.

**Crash recovery sequence:**
1. Scan `db_dir` for `*.sst` files → rebuild `CompactionEngine` level state
2. Replay `current.wal` → re-populate MemTable
3. Open new WAL for subsequent writes

---

## Project Structure

```
lsm-engine/
├── src/
│   ├── bloom_filter.h/cpp   — Bit-array bloom filter
│   ├── memtable.h/cpp       — In-memory write buffer
│   ├── wal.h/cpp            — Write-Ahead Log
│   ├── sstable.h/cpp        — SSTable reader/writer
│   ├── compaction.h/cpp     — Leveled compaction engine
│   └── lsm_engine.h/cpp     — Public API
├── tests/
│   ├── test_memtable.cpp    — MemTable unit tests
│   ├── test_bloom_filter.cpp— Bloom filter unit tests
│   ├── test_sstable.cpp     — SSTable round-trip tests
│   ├── test_compaction.cpp  — Compaction correctness tests
│   └── test_engine.cpp      — Integration: 1M key write/read/verify
├── bench/
│   └── benchmark.cpp        — Throughput + latency benchmark vs std::map
└── CMakeLists.txt
```

---

## Build Instructions

### Prerequisites
- CMake ≥ 3.16
- C++17 compiler: GCC 9+, Clang 10+, or MSVC 19.14+ (VS 2017+)
- Internet access (CMake fetches GoogleTest automatically)

### Build

```bash
# Configure
cmake -B build -DCMAKE_BUILD_TYPE=Release

# Build (parallel)
cmake --build build --parallel

# Run all tests
cd build && ctest --output-on-failure

# Run benchmark (quick mode)
./build/lsm_benchmark --small

# Run full 10M benchmark
./build/lsm_benchmark
```

### Windows (PowerShell)

```powershell
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --config Release --parallel
cd build
ctest -C Release --output-on-failure
.\Release\lsm_benchmark.exe --small
```

---

## Expected Benchmark Results

Approximate results on a modern NVMe SSD (Release build):

| Operation | LSM-Tree | std::map |
|---|---|---|
| Sequential Write | ~800K–1.5M ops/sec | ~1.2M ops/sec |
| Random Write | ~600K–1M ops/sec | ~900K ops/sec |
| Sequential Read p50 | 2–5 µs | <1 µs |
| Random Read p50 | 5–20 µs | <1 µs |
| Missing Key Read p50 | <5 µs (bloom) | <1 µs |

> **Why is LSM-Tree slower on reads?** The std::map lives entirely in RAM; LSM-Tree reads may touch disk. LSM-Tree's advantage is **write throughput at scale** — once data exceeds RAM it avoids random writes (B-tree's worst case) by converting them to sequential I/O.

---

## Key Tradeoffs Summary

| Concern | LSM-Tree | B-Tree (std::map) |
|---|---|---|
| Write amplification | 10–30× (compaction) | 1–2× |
| Read amplification | O(levels) ≈ 7 | O(log n) |
| Space amplification | ~1.1× | ~1.3× |
| Write throughput | **Excellent** (sequential I/O) | Good (in-memory) |
| Read throughput | Good (bloom + index) | **Excellent** (in-memory) |
| Crash safety | WAL + replay | fsync per write |
| Range scans | Good (sorted SSTables) | **Excellent** |

---

## Possible Extensions

- [ ] Snappy/LZ4 block compression (SSTable data block)
- [ ] Concurrent skip list MemTable (lock-free writes)
- [ ] Block cache (LRU cache for hot SSTable data blocks)
- [ ] Iterator API for range scans
- [ ] Column families (separate MemTable/WAL per family)
- [ ] Tiered compaction strategy option
- [ ] mmap-based SSTable reader (avoids syscall overhead)
- [ ] Statistics: read/write amplification counters, compaction metrics
