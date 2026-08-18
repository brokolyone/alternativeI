#pragma once

#include <cstdint>
#include <memory>
#include <string>

namespace diskutil {

// Thin wrapper over a raw device or a plain image file: both are read the
// same way (a flat byte sequence), which is exactly what backup/restore
// needs. Opening a real physical device for writing is deliberately not
// hidden behind anything "convenient" - every write call is explicit at
// the CLI layer, gated on --yes, and this class never writes on its own.
class BlockDevice {
public:
    static std::unique_ptr<BlockDevice> open(const std::string &path, bool writable, std::string *error);
    ~BlockDevice();

    BlockDevice(const BlockDevice &) = delete;
    BlockDevice &operator=(const BlockDevice &) = delete;

    uint64_t sizeBytes() const { return sizeBytes_; }
    bool isSpecialDevice() const { return isSpecialDevice_; }

    // Returns false (and leaves *bytesRead/written best-effort) on any
    // short read/write or OS error - callers must treat that as failure,
    // never as "partially succeeded, good enough".
    bool readAt(uint64_t offset, void *buffer, size_t length);
    bool writeAt(uint64_t offset, const void *buffer, size_t length);

private:
    BlockDevice() = default;

#if defined(_WIN32)
    void *handle_ = nullptr; // HANDLE
#else
    int fd_ = -1;
#endif
    uint64_t sizeBytes_ = 0;
    bool isSpecialDevice_ = false;
};

} // namespace diskutil
