#pragma once

#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <cstdint>
#include <string>
#include <sys/stat.h>
#include <unistd.h>

struct StorageFailure {
    const char* operation = nullptr;
    int code = 0;
    explicit operator bool() const { return operation != nullptr; }
};

// Probe the same operations that real transfers use. Existing directories can
// be writable even when mkdir or access reports a policy error on the console.
inline StorageFailure probeDirectory(std::string path) {
    while (path.size() > 1 && path.back() == '/') path.pop_back();
    if (path.empty() || path.front() != '/') return {"path", EINVAL};
    struct stat info{};
    if (stat(path.c_str(), &info) != 0) {
        if (errno != ENOENT) return {"stat-directory", errno};
        if (mkdir(path.c_str(), 0755) != 0 && errno != EEXIST) {
            const int code = errno;
            if (stat(path.c_str(), &info) != 0) return {"mkdir", code};
        } else if (stat(path.c_str(), &info) != 0) {
            return {"stat-created-directory", errno};
        }
    }
    if (!S_ISDIR(info.st_mode)) return {"validate-directory", ENOTDIR};

    std::string probe = path + "/.nebula-write-XXXXXX";
    const int fd = mkstemp(probe.data());
    if (fd < 0) return {"create-probe", errno};
    StorageFailure failure;
    const auto written = write(fd, "N", 1);
    if (written != 1) failure = {"write-probe", written < 0 ? errno : EIO};
    if (close(fd) != 0 && !failure) failure = {"close-probe", errno};
    if (unlink(probe.c_str()) != 0 && !failure) failure = {"remove-probe", errno};
    return failure;
}


inline bool fitsStorage(uint64_t bytes, uint64_t blockSize, uint64_t availableBlocks) {
    return !blockSize || availableBlocks >= bytes / blockSize + (bytes % blockSize != 0);
}

inline StorageFailure writeStorage(FILE* file, const void* data, size_t bytes) {
    if (fwrite(data, 1, bytes, file) == bytes) return {};
    return {"write", errno ? errno : EIO};
}
