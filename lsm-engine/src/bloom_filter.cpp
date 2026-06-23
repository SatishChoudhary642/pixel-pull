#include "bloom_filter.h"

#include <cassert>
#include <cmath>
#include <stdexcept>

// ---------------------------------------------------------------------------
// Construction helpers
// ---------------------------------------------------------------------------

// Optimal bit-array size: m = ceil( -n * ln(p) / ln(2)^2 )
static std::size_t OptimalBits(std::size_t n, double fpr) {
    if (n == 0) n = 1;
    const double ln2  = std::log(2.0);
    const double bits = -static_cast<double>(n) * std::log(fpr) / (ln2 * ln2);
    // Round up to next multiple of 8 so bits fit exactly in bytes.
    std::size_t m = static_cast<std::size_t>(std::ceil(bits));
    return (m + 7) & ~std::size_t{7};
}

// Optimal number of hash functions: k = round( m/n * ln(2) )
static std::size_t OptimalHashes(std::size_t m, std::size_t n) {
    if (n == 0) n = 1;
    double k = (static_cast<double>(m) / static_cast<double>(n)) * std::log(2.0);
    return std::max(std::size_t{1}, static_cast<std::size_t>(std::round(k)));
}

// ---------------------------------------------------------------------------
// Constructors
// ---------------------------------------------------------------------------

BloomFilter::BloomFilter(std::size_t n, double fpr)
    : num_bits_(OptimalBits(n, fpr)),
      num_hashes_(OptimalHashes(num_bits_, n)),
      bits_(num_bits_ / 8, 0) {}

BloomFilter::BloomFilter(std::vector<uint8_t> bits, std::size_t num_bits,
                         std::size_t num_hashes)
    : num_bits_(num_bits),
      num_hashes_(num_hashes),
      bits_(std::move(bits)) {
    if (bits_.size() * 8 < num_bits_) {
        throw std::invalid_argument("BloomFilter: bit array too small");
    }
}

// ---------------------------------------------------------------------------
// Bit array accessors
// ---------------------------------------------------------------------------

void BloomFilter::SetBit(std::size_t pos) {
    bits_[pos / 8] |= static_cast<uint8_t>(1u << (pos % 8));
}

bool BloomFilter::GetBit(std::size_t pos) const {
    return (bits_[pos / 8] >> (pos % 8)) & 1u;
}

// ---------------------------------------------------------------------------
// FNV-1a 64-bit hash
// ---------------------------------------------------------------------------

uint64_t BloomFilter::FNV1a(const std::string& key) const {
    constexpr uint64_t kBasis = 14695981039346656037ULL;
    constexpr uint64_t kPrime = 1099511628211ULL;
    uint64_t h = kBasis;
    for (unsigned char c : key) {
        h ^= static_cast<uint64_t>(c);
        h *= kPrime;
    }
    return h;
}

// ---------------------------------------------------------------------------
// MurmurHash3-inspired 64-bit hash (Austin Appleby, public domain)
// ---------------------------------------------------------------------------

uint64_t BloomFilter::MurmurHash64(const std::string& key,
                                    uint64_t seed) const {
    const uint8_t* data   = reinterpret_cast<const uint8_t*>(key.data());
    std::size_t    len    = key.size();
    const uint64_t m      = 0xc6a4a7935bd1e995ULL;
    const int      r      = 47;

    uint64_t h = seed ^ (len * m);

    const uint8_t* end = data + (len / 8) * 8;
    while (data != end) {
        uint64_t k;
        __builtin_memcpy(&k, data, 8);   // portable unaligned read
        data += 8;

        k *= m;
        k ^= k >> r;
        k *= m;

        h ^= k;
        h *= m;
    }

    // Remaining bytes
    switch (len & 7) {
        case 7: h ^= static_cast<uint64_t>(data[6]) << 48; [[fallthrough]];
        case 6: h ^= static_cast<uint64_t>(data[5]) << 40; [[fallthrough]];
        case 5: h ^= static_cast<uint64_t>(data[4]) << 32; [[fallthrough]];
        case 4: h ^= static_cast<uint64_t>(data[3]) << 24; [[fallthrough]];
        case 3: h ^= static_cast<uint64_t>(data[2]) << 16; [[fallthrough]];
        case 2: h ^= static_cast<uint64_t>(data[1]) << 8;  [[fallthrough]];
        case 1: h ^= static_cast<uint64_t>(data[0]);
                h *= m;
    }

    h ^= h >> r;
    h *= m;
    h ^= h >> r;
    return h;
}

// ---------------------------------------------------------------------------
// Public interface
// ---------------------------------------------------------------------------

void BloomFilter::Add(const std::string& key) {
    uint64_t h1 = FNV1a(key);
    uint64_t h2 = MurmurHash64(key, 0xdeadbeefcafe1234ULL);
    for (std::size_t i = 0; i < num_hashes_; ++i) {
        std::size_t pos = static_cast<std::size_t>((h1 + i * h2) % num_bits_);
        SetBit(pos);
    }
}

bool BloomFilter::MayContain(const std::string& key) const {
    uint64_t h1 = FNV1a(key);
    uint64_t h2 = MurmurHash64(key, 0xdeadbeefcafe1234ULL);
    for (std::size_t i = 0; i < num_hashes_; ++i) {
        std::size_t pos = static_cast<std::size_t>((h1 + i * h2) % num_bits_);
        if (!GetBit(pos)) return false;
    }
    return true;
}
