#pragma once
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

// ---------------------------------------------------------------------------
// BloomFilter — probabilistic set membership test.
//
// Design:
//   • Bit array of m bits stored as a vector<uint8_t> (byte granularity).
//   • k independent hash probes generated via double-hashing:
//       h_i(key) = ( FNV1a(key) + i * MurmurHash64(key, seed) ) % m
//   • m  = ceil( -n * ln(p) / ln(2)^2 )
//   • k  = max(1, round( m/n * ln(2) ))
//
// At the default 1% FPR: ~9.6 bits/key, ~7 hash functions.
// ---------------------------------------------------------------------------
class BloomFilter {
public:
    // Construct a fresh filter sized for n expected insertions at given FPR.
    BloomFilter(std::size_t n, double fpr = 0.01);

    // Reconstruct from serialised data (loaded from an SSTable).
    BloomFilter(std::vector<uint8_t> bits, std::size_t num_bits,
                std::size_t num_hashes);

    void Add(const std::string& key);
    bool MayContain(const std::string& key) const;

    // Serialisation accessors (used by SSTableWriter / SSTableReader).
    const std::vector<uint8_t>& GetBits()      const { return bits_;       }
    std::size_t                 GetNumBits()   const { return num_bits_;   }
    std::size_t                 GetNumHashes() const { return num_hashes_; }

private:
    std::size_t           num_bits_;
    std::size_t           num_hashes_;
    std::vector<uint8_t>  bits_;

    uint64_t FNV1a(const std::string& key) const;
    uint64_t MurmurHash64(const std::string& key, uint64_t seed) const;
    void     SetBit(std::size_t pos);
    bool     GetBit(std::size_t pos) const;
};
