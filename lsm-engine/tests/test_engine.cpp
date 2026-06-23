#include <gtest/gtest.h>
#include <filesystem>
#include <string>
#include <thread>
#include <chrono>
#include "lsm_engine.h"

namespace fs = std::filesystem;

static std::string TestDB(const std::string& suffix) {
    auto p = fs::temp_directory_path() / ("lsm_eng_test_" + suffix);
    fs::remove_all(p);
    fs::create_directories(p);
    return p.string();
}

// ---------------------------------------------------------------------------
// Basic put / get
// ---------------------------------------------------------------------------

TEST(Engine, PutAndGet) {
    LSMConfig cfg;
    cfg.db_dir = TestDB("basic");
    LSMEngine eng(cfg);

    eng.Put("hello", "world");
    EXPECT_EQ(eng.Get("hello"), "world");
    EXPECT_FALSE(eng.Get("missing").has_value());
}

// ---------------------------------------------------------------------------
// Overwrite returns latest value
// ---------------------------------------------------------------------------

TEST(Engine, Overwrite) {
    LSMConfig cfg;
    cfg.db_dir = TestDB("overwrite");
    LSMEngine eng(cfg);

    eng.Put("k", "v1");
    eng.Put("k", "v2");
    EXPECT_EQ(eng.Get("k"), "v2");
}

// ---------------------------------------------------------------------------
// Delete makes key invisible
// ---------------------------------------------------------------------------

TEST(Engine, DeleteHidesKey) {
    LSMConfig cfg;
    cfg.db_dir = TestDB("delete");
    LSMEngine eng(cfg);

    eng.Put("x", "present");
    eng.Delete("x");
    EXPECT_FALSE(eng.Get("x").has_value());
}

// ---------------------------------------------------------------------------
// MemTable flush → SSTable read-back
// ---------------------------------------------------------------------------

TEST(Engine, FlushAndReadBack) {
    LSMConfig cfg;
    cfg.db_dir = TestDB("flush");
    LSMEngine eng(cfg);

    constexpr int N = 1000;
    for (int i = 0; i < N; ++i)
        eng.Put("k" + std::to_string(i), "v" + std::to_string(i));

    eng.Flush();

    for (int i = 0; i < N; ++i)
        ASSERT_EQ(eng.Get("k" + std::to_string(i)),
                  "v" + std::to_string(i)) << "i=" << i;
}

// ---------------------------------------------------------------------------
// WAL replay (crash recovery simulation)
// ---------------------------------------------------------------------------

TEST(Engine, WalReplay) {
    auto dir = TestDB("walreplay");

    constexpr int N = 500;
    {
        LSMConfig cfg;
        cfg.db_dir = dir;
        LSMEngine eng(cfg);
        for (int i = 0; i < N; ++i)
            eng.Put("r" + std::to_string(i), "rv" + std::to_string(i));
        // Close WITHOUT flushing — data remains in WAL.
    }

    // Re-open — should replay WAL.
    {
        LSMConfig cfg;
        cfg.db_dir = dir;
        LSMEngine eng(cfg);
        for (int i = 0; i < N; ++i)
            ASSERT_EQ(eng.Get("r" + std::to_string(i)),
                      "rv" + std::to_string(i)) << "i=" << i;
    }
}

// ---------------------------------------------------------------------------
// Integration: write 1 M keys, read back, verify all
// ---------------------------------------------------------------------------

TEST(Engine, OneMillion) {
    LSMConfig cfg;
    cfg.db_dir            = TestDB("1M");
    cfg.memtable_max_size = 2ULL * 1024 * 1024; // smaller threshold = more flushes
    LSMEngine eng(cfg);

    constexpr int N = 1'000'000;

    // Write
    for (int i = 0; i < N; ++i) {
        std::string k = "key" + std::to_string(i);
        std::string v = "val" + std::to_string(i);
        eng.Put(k, v);
    }
    eng.Flush();

    // Verify
    int failures = 0;
    for (int i = 0; i < N; ++i) {
        std::string k = "key" + std::to_string(i);
        std::string expected = "val" + std::to_string(i);
        auto result = eng.Get(k);
        if (!result || *result != expected) {
            ++failures;
            if (failures <= 5)
                ADD_FAILURE() << "key=" << k << " expected=" << expected
                              << " got=" << (result ? *result : "<nullopt>");
        }
    }
    EXPECT_EQ(failures, 0);
}

// ---------------------------------------------------------------------------
// Concurrent reads + writes (basic race-condition check)
// ---------------------------------------------------------------------------

TEST(Engine, ConcurrentAccess) {
    LSMConfig cfg;
    cfg.db_dir = TestDB("concurrent");
    LSMEngine eng(cfg);

    constexpr int N = 5000;

    // Pre-populate
    for (int i = 0; i < N; ++i)
        eng.Put("p" + std::to_string(i), std::to_string(i));

    std::thread writer([&]{
        for (int i = 0; i < N; ++i)
            eng.Put("w" + std::to_string(i), std::to_string(i));
    });
    std::thread reader([&]{
        for (int i = 0; i < N; ++i)
            (void)eng.Get("p" + std::to_string(i));
    });

    writer.join();
    reader.join();

    // Verify writes landed.
    for (int i = 0; i < N; ++i)
        ASSERT_EQ(eng.Get("w" + std::to_string(i)), std::to_string(i));
}

// ---------------------------------------------------------------------------
// Delete after flush (tombstone must be visible across SSTable boundary)
// ---------------------------------------------------------------------------

TEST(Engine, DeleteAfterFlush) {
    LSMConfig cfg;
    cfg.db_dir = TestDB("delflush");
    LSMEngine eng(cfg);

    eng.Put("key", "value");
    eng.Flush();
    eng.Delete("key");

    EXPECT_FALSE(eng.Get("key").has_value());
}
