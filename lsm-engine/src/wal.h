#pragma once
#include <cstdint>
#include <functional>
#include <string>

// ---------------------------------------------------------------------------
// Write-Ahead Log (WAL) — crash-recovery guarantee.
//
// File format (one record per key-value pair):
//   [CRC32: 4B] [key_len: 4B] [val_len: 4B] [key bytes] [value bytes]
//
// Design decisions:
//   • CRC32 (IEEE 802.3) computed over the key+value payload so a partial
//     write (power failure mid-record) is detected during replay and stops
//     replay at the first corrupt record rather than silently replaying garbage.
//   • fsync_on_write: when true, every Append() calls fsync()/FlushFileBuffers()
//     before returning. This guarantees durability at the cost of ~15x write
//     throughput reduction. When false (default) the OS write cache is relied
//     upon — data safe across crashes of the process but NOT power loss.
//   • WAL is rotated (closed + deleted) after the corresponding MemTable has
//     been durably written to an SSTable, so WAL files are short-lived.
// ---------------------------------------------------------------------------
class WAL {
public:
    // Open (or create) the WAL at the given path.
    explicit WAL(const std::string& path, bool fsync_on_write = false);
    ~WAL();

    // Append a key-value record.  Thread-unsafe — caller must serialise.
    void Append(const std::string& key, const std::string& value);

    // fsync the underlying file descriptor.
    void Sync();

    void Close();
    bool IsOpen() const;
    const std::string& Path() const { return path_; }

    // Replay all valid records from path; calls cb(key, value) for each.
    // Stops at the first corrupt or truncated record (partial writes are safe).
    static void Replay(const std::string& path,
                       std::function<void(const std::string&,
                                          const std::string&)> cb);

    // Remove the WAL file from disk.
    static void DeleteFile(const std::string& path);

private:
    std::string path_;
    bool        fsync_on_write_;

#ifdef _WIN32
    void* handle_;           // HANDLE (Windows)
#else
    int   fd_;               // file descriptor (POSIX)
#endif

    // IEEE 802.3 CRC32 via lookup table.
    static uint32_t Crc32(const void* data, std::size_t len);

    // Low-level helpers.
    void WriteAll(const void* buf, std::size_t n);
    static bool ReadAll(
#ifdef _WIN32
        void* handle,
#else
        int fd,
#endif
        void* buf, std::size_t n);
};
