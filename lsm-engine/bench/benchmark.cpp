// benchmark.cpp — LSM-Tree vs std::map throughput and latency benchmark
//
// Measures:
//   1. Sequential write throughput  (ops/sec)
//   2. Random write throughput      (ops/sec)
//   3. Sequential read latency      (p50 / p99 / p99.9 µs)
//   4. Random read latency          (p50 / p99 / p99.9 µs)
//   5. Read amplification for missing keys (bloom filter effectiveness)
//   6. Comparison against std::map  (B-tree baseline)

#include <algorithm>
#include <chrono>
#include <cinttypes>
#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <iomanip>
#include <iostream>
#include <map>
#include <numeric>
#include <optional>
#include <random>
#include <string>
#include <vector>

#include "lsm_engine.h"

namespace fs = std::filesystem;
using Clock  = std::chrono::steady_clock;
using us_t   = std::chrono::microseconds;

// ---------------------------------------------------------------------------
// Helper: percentile from a sorted latency vector (microseconds)
// ---------------------------------------------------------------------------
static double Percentile(const std::vector<double>& sorted_us, double pct) {
    if (sorted_us.empty()) return 0.0;
    std::size_t idx =
        static_cast<std::size_t>(pct / 100.0 * sorted_us.size());
    idx = std::min(idx, sorted_us.size() - 1);
    return sorted_us[idx];
}

static void PrintPercentiles(const std::vector<double>& lat_us) {
    auto s = lat_us;
    std::sort(s.begin(), s.end());
    std::cout << "    p50  = " << std::fixed << std::setprecision(2)
              << Percentile(s, 50.0) << " µs\n";
    std::cout << "    p99  = " << Percentile(s, 99.0) << " µs\n";
    std::cout << "    p99.9= " << Percentile(s, 99.9) << " µs\n";
}

// ---------------------------------------------------------------------------
// Helpers: key/value generation
// ---------------------------------------------------------------------------
static std::string MakeKey(uint64_t i) {
    // Zero-padded 12-char key so lexicographic = numeric for sequential keys.
    char buf[20];
    std::snprintf(buf, sizeof(buf), "key%012" PRIu64, i);
    return buf;
}

static std::string MakeValue(uint64_t i) {
    // ~100-byte value
    return "value_" + std::string(94, 'x') + std::to_string(i);
}

// ---------------------------------------------------------------------------
// LSM benchmark
// ---------------------------------------------------------------------------
static void BenchLSM(uint64_t N_write, uint64_t N_read) {
    auto db_dir = (fs::temp_directory_path() / "lsm_bench_db").string();
    fs::remove_all(db_dir);

    LSMConfig cfg;
    cfg.db_dir            = db_dir;
    cfg.memtable_max_size = 4ULL * 1024 * 1024;
    cfg.fsync_wal         = false;

    std::cout << "=== LSM-Tree Benchmark ===\n";
    std::cout << "Write N=" << N_write << "  Read N=" << N_read << "\n\n";

    // ---- 1. Sequential writes ----
    {
        LSMEngine eng(cfg);
        auto t0 = Clock::now();
        for (uint64_t i = 0; i < N_write; ++i)
            eng.Put(MakeKey(i), MakeValue(i));
        eng.Flush();
        auto dt = std::chrono::duration_cast<us_t>(Clock::now() - t0).count();
        double ops = static_cast<double>(N_write) / (dt / 1e6);
        std::cout << "[Sequential Write]\n";
        std::cout << "  " << N_write << " ops in " << dt / 1000 << " ms\n";
        std::cout << "  Throughput: " << static_cast<uint64_t>(ops) << " ops/sec\n\n";
    }

    // ---- 2. Random writes ----
    {
        fs::remove_all(db_dir);
        LSMEngine eng(cfg);
        std::mt19937_64 rng(42);
        auto t0 = Clock::now();
        for (uint64_t i = 0; i < N_write; ++i)
            eng.Put(MakeKey(rng() % N_write), MakeValue(i));
        eng.Flush();
        auto dt = std::chrono::duration_cast<us_t>(Clock::now() - t0).count();
        double ops = static_cast<double>(N_write) / (dt / 1e6);
        std::cout << "[Random Write]\n";
        std::cout << "  " << N_write << " ops in " << dt / 1000 << " ms\n";
        std::cout << "  Throughput: " << static_cast<uint64_t>(ops) << " ops/sec\n\n";
    }

    // ---- 3. Sequential reads (after fresh sequential write) ----
    {
        fs::remove_all(db_dir);
        LSMEngine eng(cfg);
        for (uint64_t i = 0; i < N_write; ++i)
            eng.Put(MakeKey(i), MakeValue(i));
        eng.Flush();

        std::vector<double> lat;
        lat.reserve(N_read);
        for (uint64_t i = 0; i < N_read; ++i) {
            auto t0 = Clock::now();
            (void)eng.Get(MakeKey(i % N_write));
            auto dt = std::chrono::duration_cast<us_t>(Clock::now() - t0).count();
            lat.push_back(static_cast<double>(dt));
        }
        std::cout << "[Sequential Read]\n";
        PrintPercentiles(lat);
        std::cout << "\n";
    }

    // ---- 4. Random reads ----
    {
        fs::remove_all(db_dir);
        LSMEngine eng(cfg);
        for (uint64_t i = 0; i < N_write; ++i)
            eng.Put(MakeKey(i), MakeValue(i));
        eng.Flush();

        std::mt19937_64 rng(123);
        std::vector<double> lat;
        lat.reserve(N_read);
        for (uint64_t i = 0; i < N_read; ++i) {
            std::string k = MakeKey(rng() % N_write);
            auto t0 = Clock::now();
            (void)eng.Get(k);
            auto dt = std::chrono::duration_cast<us_t>(Clock::now() - t0).count();
            lat.push_back(static_cast<double>(dt));
        }
        std::cout << "[Random Read]\n";
        PrintPercentiles(lat);
        std::cout << "\n";
    }

    // ---- 5. Missing key reads (bloom filter effectiveness) ----
    {
        fs::remove_all(db_dir);
        LSMEngine eng(cfg);
        // Write keys 0..N_write-1 then read keys N_write..2*N_write-1 (all missing)
        for (uint64_t i = 0; i < N_write; ++i)
            eng.Put(MakeKey(i), MakeValue(i));
        eng.Flush();

        std::vector<double> lat;
        lat.reserve(N_read);
        for (uint64_t i = 0; i < N_read; ++i) {
            std::string k = MakeKey(N_write + i);
            auto t0 = Clock::now();
            (void)eng.Get(k);
            auto dt = std::chrono::duration_cast<us_t>(Clock::now() - t0).count();
            lat.push_back(static_cast<double>(dt));
        }
        std::cout << "[Missing Key Read (Bloom Filter)]\n";
        PrintPercentiles(lat);
        std::cout << "\n";
    }

    fs::remove_all(db_dir);
}

// ---------------------------------------------------------------------------
// std::map (B-tree) baseline
// ---------------------------------------------------------------------------
static void BenchStdMap(uint64_t N_write, uint64_t N_read) {
    std::cout << "=== std::map (B-tree) Baseline ===\n";
    std::cout << "Write N=" << N_write << "  Read N=" << N_read << "\n\n";

    std::map<std::string, std::string> m;

    // Sequential writes
    {
        auto t0 = Clock::now();
        for (uint64_t i = 0; i < N_write; ++i)
            m[MakeKey(i)] = MakeValue(i);
        auto dt = std::chrono::duration_cast<us_t>(Clock::now() - t0).count();
        double ops = static_cast<double>(N_write) / (dt / 1e6);
        std::cout << "[Sequential Write]\n";
        std::cout << "  Throughput: " << static_cast<uint64_t>(ops) << " ops/sec\n\n";
    }

    // Random reads
    {
        std::mt19937_64 rng(42);
        std::vector<double> lat;
        lat.reserve(N_read);
        for (uint64_t i = 0; i < N_read; ++i) {
            std::string k = MakeKey(rng() % N_write);
            auto t0 = Clock::now();
            (void)m.count(k);
            auto dt = std::chrono::duration_cast<us_t>(Clock::now() - t0).count();
            lat.push_back(static_cast<double>(dt));
        }
        std::cout << "[Random Read]\n";
        PrintPercentiles(lat);
        std::cout << "\n";
    }
}

// ---------------------------------------------------------------------------
// main
// ---------------------------------------------------------------------------
int main(int argc, char* argv[]) {
    // Default: 10 M writes, 1 M reads (pass --small for quick run)
    uint64_t N_write = 10'000'000;
    uint64_t N_read  =  1'000'000;

    for (int i = 1; i < argc; ++i) {
        if (std::string(argv[i]) == "--small") {
            N_write = 100'000;
            N_read  =  10'000;
        }
    }

    BenchLSM(N_write, N_read);
    std::cout << "-------------------------------------------\n\n";
    BenchStdMap(N_write, N_read);

    return 0;
}
