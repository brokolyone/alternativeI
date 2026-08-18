#include "BlockDevice.h"

#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <winioctl.h>
#else
#include <fcntl.h>
#include <sys/ioctl.h>
#include <sys/stat.h>
#include <unistd.h>
#include <cerrno>
#include <cstring>
#if defined(__linux__)
#include <linux/fs.h>
#endif
#endif

namespace diskutil {

#if defined(_WIN32)

std::unique_ptr<BlockDevice> BlockDevice::open(const std::string &path, bool writable, std::string *error) {
    auto device = std::unique_ptr<BlockDevice>(new BlockDevice());

    const std::wstring widePath(path.begin(), path.end());
    const DWORD access = writable ? (GENERIC_READ | GENERIC_WRITE) : GENERIC_READ;
    HANDLE handle = CreateFileW(widePath.c_str(), access, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr,
                                 OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (handle == INVALID_HANDLE_VALUE) {
        if (error) *error = "CreateFile failed (error " + std::to_string(GetLastError()) + ")";
        return nullptr;
    }
    device->handle_ = handle;

    device->isSpecialDevice_ =
        path.rfind("\\\\.\\PhysicalDrive", 0) == 0 || path.rfind("\\\\.\\", 0) == 0;

    if (device->isSpecialDevice_) {
        GET_LENGTH_INFORMATION lengthInfo{};
        DWORD bytesReturned = 0;
        if (DeviceIoControl(handle, IOCTL_DISK_GET_LENGTH_INFO, nullptr, 0, &lengthInfo,
                             sizeof(lengthInfo), &bytesReturned, nullptr)) {
            device->sizeBytes_ = static_cast<uint64_t>(lengthInfo.Length.QuadPart);
        }
    } else {
        LARGE_INTEGER size{};
        if (GetFileSizeEx(handle, &size)) {
            device->sizeBytes_ = static_cast<uint64_t>(size.QuadPart);
        }
    }

    return device;
}

BlockDevice::~BlockDevice() {
    if (handle_ != nullptr) {
        CloseHandle(static_cast<HANDLE>(handle_));
    }
}

bool BlockDevice::readAt(uint64_t offset, void *buffer, size_t length) {
    LARGE_INTEGER li;
    li.QuadPart = static_cast<LONGLONG>(offset);
    if (!SetFilePointerEx(static_cast<HANDLE>(handle_), li, nullptr, FILE_BEGIN)) return false;

    DWORD bytesRead = 0;
    if (!ReadFile(static_cast<HANDLE>(handle_), buffer, static_cast<DWORD>(length), &bytesRead, nullptr)) {
        return false;
    }
    return bytesRead == length;
}

bool BlockDevice::writeAt(uint64_t offset, const void *buffer, size_t length) {
    LARGE_INTEGER li;
    li.QuadPart = static_cast<LONGLONG>(offset);
    if (!SetFilePointerEx(static_cast<HANDLE>(handle_), li, nullptr, FILE_BEGIN)) return false;

    DWORD bytesWritten = 0;
    if (!WriteFile(static_cast<HANDLE>(handle_), buffer, static_cast<DWORD>(length), &bytesWritten,
                    nullptr)) {
        return false;
    }
    return bytesWritten == length;
}

#else // POSIX (Linux, and generically anything with pread/pwrite)

std::unique_ptr<BlockDevice> BlockDevice::open(const std::string &path, bool writable, std::string *error) {
    auto device = std::unique_ptr<BlockDevice>(new BlockDevice());

    const int flags = writable ? O_RDWR : O_RDONLY;
    const int fd = ::open(path.c_str(), flags);
    if (fd < 0) {
        if (error) *error = "open() failed: " + std::string(strerror(errno));
        return nullptr;
    }
    device->fd_ = fd;

    struct stat st{};
    if (fstat(fd, &st) == 0) {
        device->isSpecialDevice_ = S_ISBLK(st.st_mode) || S_ISCHR(st.st_mode);
        if (!device->isSpecialDevice_) {
            device->sizeBytes_ = static_cast<uint64_t>(st.st_size);
        }
    }

#if defined(__linux__) && defined(BLKGETSIZE64)
    if (device->isSpecialDevice_) {
        uint64_t sizeBytes = 0;
        if (ioctl(fd, BLKGETSIZE64, &sizeBytes) == 0) {
            device->sizeBytes_ = sizeBytes;
        }
    }
#endif

    return device;
}

BlockDevice::~BlockDevice() {
    if (fd_ >= 0) {
        ::close(fd_);
    }
}

bool BlockDevice::readAt(uint64_t offset, void *buffer, size_t length) {
    const ssize_t n = pread(fd_, buffer, length, static_cast<off_t>(offset));
    return n >= 0 && static_cast<size_t>(n) == length;
}

bool BlockDevice::writeAt(uint64_t offset, const void *buffer, size_t length) {
    const ssize_t n = pwrite(fd_, buffer, length, static_cast<off_t>(offset));
    return n >= 0 && static_cast<size_t>(n) == length;
}

#endif

} // namespace diskutil
