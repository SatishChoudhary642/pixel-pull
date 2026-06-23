#include <gtest/gtest.h>
#include <filesystem>
#include <string>
#include "sstable.h"
#include "memtable.h"  // kTombstoneValue

namespace fs = std::filesystem;

// Helper: returns a temp directory path unique to each test.
static std::string TmpDir(const std::string& suffix) {
    auto p = fs::temp_directory_path() / ("lsm_sst_test_" + suffix);
    fs::create_directories(p);
    return p.string();
}
static std::string TmpFile(const std::string& dir, const std::string& name) {
    return dir + "/" + name;
}

// ---------------------------------------------------------------------------
// Basic write / read round-trip
// ---------------------------------------------------------------------------

TEST(SSTable, WriteReadRoundTrip) {
    auto dir = TmpDir("roundtrip");
    auto path = TmpFile(dir, "0_1.sst");

    {
        SSTableWriter w(path);
        w.Add("apple",  "fruit");
        w.Add("banana", "yellow");
        w.Add("cherry", "red");
        w.Finish();
    }

    SSTableReader r(path);
    EXPECT_EQ(r.Get("apple"),  "fruit");
    EXPECT_EQ(r.Get("banana"), "yellow");
    EXPECT_EQ(r.Get("cherry"), "red");
    EXPECT_FALSE(r.Get("date").has_value());
}

// ---------------------------------------------------------------------------
// Bloom filter gates missing keys (no disk read)
// ---------------------------------------------------------------------------

TEST(SSTable, BloomFilterGatesMissingKeys) {
    auto dir  = TmpDir("bloom");
    auto path = TmpFile(dir, "0_2.sst");

    SSTableWriter w(path);
    for (int i = 0; i < 100; ++i)
        w.Add("present_" + std::to_string(i), "val");
    w.Finish();

    SSTableReader r(path);
    EXPECT_FALSE(r.MayContain("definitely_absent"));
    EXPECT_FALSE(r.Get("definitely_absent").has_value());
}

// ---------------------------------------------------------------------------
// No false negatives from bloom filter
// ---------------------------------------------------------------------------

TEST(SSTable, BloomNoFalseNegatives) {
    auto dir  = TmpDir("bfneg");
    auto path = TmpFile(dir, "0_3.sst");

    constexpr int N = 200;
    SSTableWriter w(path);
    for (int i = 0; i < N; ++i)
        w.Add("k" + std::to_string(i), "v");
    w.Finish();

    SSTableReader r(path);
    for (int i = 0; i < N; ++i)
        EXPECT_TRUE(r.MayContain("k" + std::to_string(i)));
}

// ---------------------------------------------------------------------------
// Metadata: smallest / largest key
// ---------------------------------------------------------------------------

TEST(SSTable, MetadataKeys) {
    auto dir  = TmpDir("meta");
    auto path = TmpFile(dir, "0_4.sst");

    SSTableWriter w(path);
    w.Add("aa", "1");
    w.Add("mm", "2");
    w.Add("zz", "3");
    w.Finish();

    SSTableReader r(path);
    EXPECT_EQ(r.Meta().smallest_key, "aa");
    EXPECT_EQ(r.Meta().largest_key,  "zz");
}

// ---------------------------------------------------------------------------
// ReadAll returns entries in sorted order
// ---------------------------------------------------------------------------

TEST(SSTable, ReadAllSorted) {
    auto dir  = TmpDir("readall");
    auto path = TmpFile(dir, "0_5.sst");

    SSTableWriter w(path);
    for (int i = 99; i >= 0; --i) {
        // Add in reverse order to show writer expects sorted input.
        // (In real usage the MemTable always hands sorted data.)
    }
    for (int i = 0; i < 100; ++i)
        w.Add("key_" + std::to_string(i), std::to_string(i));
    w.Finish();

    SSTableReader r(path);
    auto entries = r.ReadAll();
    ASSERT_EQ(entries.size(), 100u);
    for (std::size_t i = 0; i + 1 < entries.size(); ++i)
        EXPECT_LT(entries[i].first, entries[i + 1].first);
}

// ---------------------------------------------------------------------------
// Sparse index works beyond the first interval
// ---------------------------------------------------------------------------

TEST(SSTable, SparseIndexLargeTable) {
    auto dir  = TmpDir("sparse");
    auto path = TmpFile(dir, "0_6.sst");

    constexpr int N = 500; // spans many index intervals (kIndexInterval = 16)
    SSTableWriter w(path);
    for (int i = 0; i < N; ++i)
        w.Add("key" + std::string(5 - std::to_string(i).size(), '0') +
              std::to_string(i),   // zero-padded so lexicographic = numeric
              std::to_string(i));
    w.Finish();

    SSTableReader r(path);
    for (int i = 0; i < N; i += 37) {  // spot-check
        std::string k = "key" + std::string(5 - std::to_string(i).size(), '0') +
                        std::to_string(i);
        ASSERT_EQ(r.Get(k), std::to_string(i)) << "key=" << k;
    }
}

// ---------------------------------------------------------------------------
// Tombstone values are stored and returned as-is
// ---------------------------------------------------------------------------

TEST(SSTable, TombstoneStoredCorrectly) {
    auto dir  = TmpDir("tomb");
    auto path = TmpFile(dir, "0_7.sst");

    SSTableWriter w(path);
    w.Add("alive",   "yes");
    w.Add("deleted", kTombstoneValue);
    w.Finish();

    SSTableReader r(path);
    EXPECT_EQ(r.Get("deleted"), kTombstoneValue);
    EXPECT_EQ(r.Get("alive"),   "yes");
}

// ---------------------------------------------------------------------------
// Magic number validation
// ---------------------------------------------------------------------------

TEST(SSTable, BadMagicThrows) {
    auto dir  = TmpDir("magic");
    auto path = TmpFile(dir, "bad.sst");

    // Write garbage file.
    { std::ofstream f(path, std::ios::binary); f << "XXXX"; }

    EXPECT_THROW(SSTableReader r(path), std::runtime_error);
}
