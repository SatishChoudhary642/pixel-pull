#include "wal.h"

#include <array>
#include <cstring>
#include <filesystem>
#include <functional>
#include <stdexcept>
#include <vector>

#ifdef _WIN32
#  define WIN32_LEAN_AND_MEAN
#  include <windows.h>
#else
#  include <fcntl.h>
#  include <sys/types.h>
#  include <unistd.h>
#endif

// ---------------------------------------------------------------------------
// CRC32 — IEEE 802.3 polynomial 0xEDB88320 (reflected)
// ---------------------------------------------------------------------------

static const std::array<uint32_t, 256>& Crc32Table() {
    static std::array<uint32_t, 256> tbl = []() {
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

uint32_t WAL::Crc32(const void* data, std::size_t len) {
    const auto& tbl = Crc32Table();
    uint32_t crc = 0xFFFFFFFFu;
    const auto* p = static_cast<const uint8_t*>(data);
    for (std::size_t i = 0; i < len; ++i)
        crc = tbl[(crc ^ p[i]) & 0xFF] ^ (crc >> 8);
    return crc ^ 0xFFFFFFFFu;
}

// ---------------------------------------------------------------------------
// Open / Close
// ---------------------------------------------------------------------------

WAL::WAL(const std::string& path, bool fsync_on_write)
    : path_(path), fsync_on_write_(fsync_on_write)
{
#ifdef _WIN32
    handle_ = CreateFileA(
        path.c_str(),
        GENERIC_WRITE,
        FILE_SHARE_READ,
        nullptr,
        OPEN_ALWAYS,           // create or open existing
        FILE_ATTRIBUTE_NORMAL,
        nullptr);
    if (handle_ == INVALID_HANDLE_VALUE)
        throw std::runtime_error("WAL: failed to open " + path);
    // Seek to end so we always append.
    SetFilePointer(handle_, 0, nullptr, FILE_END);
#else
    fd_ = open(path.c_str(), O_WRONLY | O_CREAT | O_APPEND, 0644);
    if (fd_ < 0)
        throw std::runtime_error("WAL: failed to open " + path);
#endif
}

WAL::~WAL() { Close(); }

void WAL::Close() {
#ifdef _WIN32
    if (handle_ && handle_ != INVALID_HANDLE_VALUE) {
        FlushFileBuffers(handle_);
        CloseHandle(handle_);
        handle_ = INVALID_HANDLE_VALUE;
    }
#else
    if (fd_ >= 0) {
        fsync(fd_);
        close(fd_);
        fd_ = -1;
    }
#endif
}

bool WAL::IsOpen() const {
#ifdef _WIN32
    return handle_ != nullptr && handle_ != INVALID_HANDLE_VALUE;
#else
    return fd_ >= 0;
#endif
}

// ---------------------------------------------------------------------------
// Write helpers
// ---------------------------------------------------------------------------

void WAL::WriteAll(const void* buf, std::size_t n) {
    const auto* p = static_cast<const uint8_t*>(buf);
    std::size_t written = 0;
    while (written < n) {
#ifdef _WIN32
        DWORD dw = 0;
        if (!WriteFile(handle_, p + written,
                       static_cast<DWORD>(n - written), &dw, nullptr) || dw == 0)
            throw std::runtime_error("WAL: write error");
        written += dw;
#else
        ssize_t r = write(fd_, p + written, n - written);
        if (r <= 0) throw std::runtime_error("WAL: write error");
        written += static_cast<std::size_t>(r);
#endif
    }
}

void WAL::Sync() {
#ifdef _WIN32
    if (handle_ && handle_ != INVALID_HANDLE_VALUE)
        FlushFileBuffers(handle_);
#else
    if (fd_ >= 0) fsync(fd_);
#endif
}

// ---------------------------------------------------------------------------
// Append — record layout: CRC32(4B) | key_len(4B) | val_len(4B) | key | val
// ---------------------------------------------------------------------------

void WAL::Append(const std::string& key, const std::string& value) {
    // Build the payload (everything after CRC) in a contiguous buffer so we
    // can CRC it in one pass.
    uint32_t klen = static_cast<uint32_t>(key.size());
    uint32_t vlen = static_cast<uint32_t>(value.size());

    std::size_t payload_size = 4 + 4 + key.size() + value.size();
    std::vector<uint8_t> payload(payload_size);
    uint8_t* p = payload.data();

    std::memcpy(p, &klen, 4); p += 4;
    std::memcpy(p, &vlen, 4); p += 4;
    std::memcpy(p, key.data(), key.size());   p += key.size();
    std::memcpy(p, value.data(), value.size());

    uint32_t crc = Crc32(payload.data(), payload.size());

    WriteAll(&crc, 4);
    WriteAll(payload.data(), payload.size());

    if (fsync_on_write_) Sync();
}

// ---------------------------------------------------------------------------
// Replay — reads records until EOF or first corrupt record
// ---------------------------------------------------------------------------

bool WAL::ReadAll(
#ifdef _WIN32
    void* handle,
#else
    int fd,
#endif
    void* buf, std::size_t n)
{
    auto* p = static_cast<uint8_t*>(buf);
    std::size_t total = 0;
    while (total < n) {
#ifdef _WIN32
        DWORD dw = 0;
        if (!ReadFile(handle, p + total,
                      static_cast<DWORD>(n - total), &dw, nullptr) || dw == 0)
            return false;
        total += dw;
#else
        ssize_t r = read(fd, p + total, n - total);
        if (r <= 0) return false;
        total += static_cast<std::size_t>(r);
#endif
    }
    return true;
}

void WAL::Replay(const std::string& path,
                 std::function<void(const std::string&,
                                    const std::string&)> cb) {
    if (!std::filesystem::exists(path)) return;

#ifdef _WIN32
    void* handle = CreateFileA(path.c_str(), GENERIC_READ, FILE_SHARE_READ,
                               nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL,
                               nullptr);
    if (handle == INVALID_HANDLE_VALUE) return;
    auto guard = [&]{ CloseHandle(handle); };
#else
    int fd = open(path.c_str(), O_RDONLY);
    if (fd < 0) return;
    auto guard = [&]{ close(fd); };
#endif

    // Suppress "unused" warning — guard is called at scope exit via RAII trick.
    struct Guard { std::function<void()> fn; ~Guard(){ fn(); } } g{guard};

    for (;;) {
        uint32_t crc_stored = 0;
#ifdef _WIN32
        if (!ReadAll(handle, &crc_stored, 4)) break;
#else
        if (!ReadAll(fd, &crc_stored, 4)) break;
#endif

        uint32_t klen = 0, vlen = 0;
#ifdef _WIN32
        if (!ReadAll(handle, &klen, 4) || !ReadAll(handle, &vlen, 4)) break;
#else
        if (!ReadAll(fd, &klen, 4) || !ReadAll(fd, &vlen, 4)) break;
#endif

        std::string key(klen, '\0');
        std::string val(vlen, '\0');

#ifdef _WIN32
        if (!ReadAll(handle, key.data(), klen)) break;
        if (!ReadAll(handle, val.data(), vlen)) break;
#else
        if (!ReadAll(fd, key.data(), klen)) break;
        if (!ReadAll(fd, val.data(), vlen)) break;
#endif

        // Verify CRC over payload = klen(4B) | vlen(4B) | key | val
        std::vector<uint8_t> payload(4 + 4 + klen + vlen);
        uint8_t* p = payload.data();
        std::memcpy(p, &klen, 4); p += 4;
        std::memcpy(p, &vlen, 4); p += 4;
        std::memcpy(p, key.data(), klen);  p += klen;
        std::memcpy(p, val.data(), vlen);

        uint32_t crc_computed = Crc32(payload.data(), payload.size());
        if (crc_computed != crc_stored) break; // corrupt; stop here

        cb(key, val);
    }
}

// ---------------------------------------------------------------------------
// DeleteFile
// ---------------------------------------------------------------------------

void WAL::DeleteFile(const std::string& path) {
    std::error_code ec;
    std::filesystem::remove(path, ec);
    // Ignore errors — file may already be gone.
}
