#pragma once

#include "nebula.h"
#include <cerrno>
#include <cstdint>
#include <cstdio>
#include <mutex>
#include <string>
#include <sys/stat.h>
#include <unistd.h>

inline std::string diagnostic(const char* stage, int code) {
    char line[160];
    snprintf(line, sizeof(line), "%s (0x%08X)", stage, static_cast<unsigned>(code));
    static std::mutex lock;
    std::lock_guard<std::mutex> guard(lock);
    fprintf(stderr, "[Nebula Hub] %s\n", line);
    if (FILE* out = fopen(DATA_DIR "diagnostics.log", "ab")) {
        if (fseek(out, 0, SEEK_END) == 0 && ftell(out) >= 128 * 1024) {
            fclose(out);
            out = fopen(DATA_DIR "diagnostics.log", "wb");
        }
        if (out) {
            fprintf(out, "%s\n", line);
            fclose(out);
        }
    }
    return line;
}

inline bool writableDirectory(const char* path) {
    if (mkdir(path, 0755) != 0 && errno != EEXIST) {
        diagnostic(path, errno);
        return false;
    }
    struct stat info{};
    if (stat(path, &info) != 0 || !S_ISDIR(info.st_mode) || access(path, W_OK | X_OK) != 0) {
        diagnostic(path, errno);
        return false;
    }
    return true;
}
