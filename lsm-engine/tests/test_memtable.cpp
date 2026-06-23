#include <gtest/gtest.h>
#include "memtable.h"

// ---------------------------------------------------------------------------
// Basic operations
// ---------------------------------------------------------------------------

TEST(MemTable, PutAndGet) {
    MemTable mt(1024);
    mt.Put("alpha", "1");
    mt.Put("beta",  "2");
    EXPECT_EQ(mt.Get("alpha"), "1");
    EXPECT_EQ(mt.Get("beta"),  "2");
    EXPECT_FALSE(mt.Get("gamma").has_value());
}

TEST(MemTable, OverwriteReturnsLatest) {
    MemTable mt(1024);
    mt.Put("key", "v1");
    mt.Put("key", "v2");
    EXPECT_EQ(mt.Get("key"), "v2");
}

TEST(MemTable, DeleteWritesTombstone) {
    MemTable mt(1024);
    mt.Put("x", "hello");
    mt.Delete("x");
    EXPECT_EQ(mt.Get("x"), kTombstoneValue);
}

// ---------------------------------------------------------------------------
// Size tracking
// ---------------------------------------------------------------------------

TEST(MemTable, SizeTracking) {
    MemTable mt(1024);
    EXPECT_EQ(mt.SizeBytes(), 0u);

    mt.Put("abc", "xyz");   // 3 + 3 = 6
    EXPECT_EQ(mt.SizeBytes(), 6u);

    mt.Put("abc", "longer"); // old 3+3=6 removed, new 3+6=9 added
    EXPECT_EQ(mt.SizeBytes(), 9u);
}

TEST(MemTable, IsFullTriggersOnThreshold) {
    MemTable mt(10);
    EXPECT_FALSE(mt.IsFull());
    mt.Put("aaaaa", "bbbbb"); // exactly 10 bytes
    EXPECT_TRUE(mt.IsFull());
}

// ---------------------------------------------------------------------------
// Iteration order
// ---------------------------------------------------------------------------

TEST(MemTable, IterationIsSorted) {
    MemTable mt(4096);
    mt.Put("cherry", "c");
    mt.Put("apple",  "a");
    mt.Put("banana", "b");

    std::vector<std::string> keys;
    for (const auto& [k, v] : mt.Data()) keys.push_back(k);

    EXPECT_EQ(keys, (std::vector<std::string>{"apple", "banana", "cherry"}));
}

// ---------------------------------------------------------------------------
// Clear
// ---------------------------------------------------------------------------

TEST(MemTable, ClearResetsState) {
    MemTable mt(1024);
    mt.Put("a", "1");
    mt.Put("b", "2");
    mt.Clear();
    EXPECT_EQ(mt.Count(),     0u);
    EXPECT_EQ(mt.SizeBytes(), 0u);
    EXPECT_FALSE(mt.Get("a").has_value());
}

// ---------------------------------------------------------------------------
// Large insert count
// ---------------------------------------------------------------------------

TEST(MemTable, LargeInsert) {
    MemTable mt(64ULL * 1024 * 1024); // 64 MB
    constexpr int N = 50'000;
    for (int i = 0; i < N; ++i) {
        mt.Put(std::to_string(i), std::to_string(i * 2));
    }
    EXPECT_EQ(mt.Count(), static_cast<std::size_t>(N));
    for (int i = 0; i < N; ++i) {
        ASSERT_EQ(mt.Get(std::to_string(i)), std::to_string(i * 2));
    }
}
