#include <gtest/gtest.h>
#include <filesystem>
#include "bloom_filter.h"

// ---------------------------------------------------------------------------
// No false negatives (must-have guarantee)
// ---------------------------------------------------------------------------

TEST(BloomFilter, NoFalseNegatives) {
    BloomFilter bf(1000, 0.01);
    std::vector<std::string> keys;
    for (int i = 0; i < 1000; ++i) {
        keys.push_back("key_" + std::to_string(i));
        bf.Add(keys.back());
    }
    for (const auto& k : keys)
        EXPECT_TRUE(bf.MayContain(k)) << "False negative for: " << k;
}

// ---------------------------------------------------------------------------
// False positive rate stays below configured threshold
// ---------------------------------------------------------------------------

TEST(BloomFilter, FalsePositiveRateWithin2x) {
    constexpr int n   = 10'000;   // inserted
    constexpr int m   = 100'000;  // tested non-inserted
    constexpr double fpr = 0.01;

    BloomFilter bf(n, fpr);
    for (int i = 0; i < n; ++i)
        bf.Add("inserted_" + std::to_string(i));

    int fp = 0;
    for (int i = 0; i < m; ++i)
        if (bf.MayContain("notinserted_" + std::to_string(i))) ++fp;

    double measured = static_cast<double>(fp) / m;
    // Allow 2× the configured FPR for statistical fluctuation.
    EXPECT_LT(measured, fpr * 2.0)
        << "Measured FPR " << measured << " exceeds " << fpr * 2.0;
}

// ---------------------------------------------------------------------------
// Empty string key
// ---------------------------------------------------------------------------

TEST(BloomFilter, EmptyKey) {
    BloomFilter bf(10, 0.01);
    bf.Add("");
    EXPECT_TRUE(bf.MayContain(""));
}

// ---------------------------------------------------------------------------
// Serialisation round-trip
// ---------------------------------------------------------------------------

TEST(BloomFilter, SerialiseRoundTrip) {
    BloomFilter original(500, 0.01);
    for (int i = 0; i < 500; ++i)
        original.Add("s" + std::to_string(i));

    // Reconstruct from raw data.
    BloomFilter copy(original.GetBits(),
                     original.GetNumBits(),
                     original.GetNumHashes());

    for (int i = 0; i < 500; ++i) {
        std::string k = "s" + std::to_string(i);
        EXPECT_TRUE(copy.MayContain(k)) << "Missing after round-trip: " << k;
    }
}

// ---------------------------------------------------------------------------
// Optimal sizing sanity checks
// ---------------------------------------------------------------------------

TEST(BloomFilter, BitsPerKey) {
    // At 1% FPR the optimal m/n is ~9.6 bits/key.
    BloomFilter bf(1000, 0.01);
    double bits_per_key = static_cast<double>(bf.GetNumBits()) / 1000.0;
    EXPECT_GT(bits_per_key, 8.0)  << "Too few bits/key";
    EXPECT_LT(bits_per_key, 12.0) << "Too many bits/key";
}

TEST(BloomFilter, NumHashFunctions) {
    BloomFilter bf(1000, 0.01);
    // k = ln(2) * m/n ≈ 6.9 at 1% FPR → round to 7
    EXPECT_GE(bf.GetNumHashes(), 6u);
    EXPECT_LE(bf.GetNumHashes(), 8u);
}

// ---------------------------------------------------------------------------
// Zero-sized filter (edge case)
// ---------------------------------------------------------------------------

TEST(BloomFilter, SingleElement) {
    BloomFilter bf(1, 0.01);
    bf.Add("solo");
    EXPECT_TRUE(bf.MayContain("solo"));
}
