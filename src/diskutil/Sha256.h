#pragma once

#include <cstddef>
#include <cstdint>
#include <string>

namespace diskutil {

// Minimal self-contained SHA-256 (FIPS 180-4), no external dependency.
// Used only for integrity verification of backup/restore images - not a
// general-purpose crypto library.
class Sha256 {
public:
    Sha256();

    void update(const void *data, size_t length);
    // Finalizes and returns the 64-char lowercase hex digest. Only call
    // once; the object shouldn't be reused after this.
    std::string hexDigest();

private:
    void processBlock(const uint8_t block[64]);

    uint32_t state_[8];
    uint8_t buffer_[64];
    size_t bufferLength_ = 0;
    uint64_t totalLength_ = 0;
};

} // namespace diskutil
