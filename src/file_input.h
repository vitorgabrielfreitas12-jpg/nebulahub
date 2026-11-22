#pragma once

#include <cerrno>
#include <cstdint>
#include <fcntl.h>
#include <unistd.h>

// Keep the length and the bytes on the same descriptor. Neither operation
// needs the target libc's layout for struct stat.
class FileInput {
public:
    explicit FileInput(const char* path)
        : fd_(open(path, O_RDONLY | O_NONBLOCK | O_NOFOLLOW)) {
        if (fd_ < 0) error = errno;
    }
    ~FileInput() { if (fd_ >= 0) close(fd_); }
    FileInput(const FileInput&) = delete;
    FileInput& operator=(const FileInput&) = delete;
    explicit operator bool() const { return fd_ >= 0; }

    bool measure(uint64_t& size) {
        static_assert(sizeof(off_t) >= sizeof(int64_t), "PKG offsets require 64-bit off_t");
        const auto end = lseek(fd_, 0, SEEK_END);
        if (end < 0 || lseek(fd_, 0, SEEK_SET) < 0) {
            error = errno;
            return false;
        }
        size = static_cast<uint64_t>(end);
        return true;
    }

    size_t readBytes(void* data, size_t size) {
        auto* next = static_cast<unsigned char*>(data);
        size_t used = 0;
        while (used < size) {
            const auto readCount = read(fd_, next + used, size - used);
            if (readCount < 0) {
                if (errno == EINTR) continue;
                error = errno;
                break;
            }
            if (!readCount) break;
            used += static_cast<size_t>(readCount);
        }
        return used;
    }

    int error = 0;

private:
    int fd_;
};
