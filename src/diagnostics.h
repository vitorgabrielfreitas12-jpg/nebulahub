#pragma once

#include "nebula.h"
#include "storage.h"
#include <cstdint>
#include <cstring>
#include <ctime>
#include <mutex>

enum class ErrorDomain { Event, Posix, Orbis, Http, Application };

inline std::string diagnostic(const char* subsystem, const char* operation,
                              ErrorDomain domain, int code = 0,
                              const char* context = "", bool durable = false) {
    static std::mutex lock;
    std::lock_guard<std::mutex> guard(lock);
    char timestamp[32] = "time-unavailable";
    const auto now = time(nullptr);
    struct tm tm{};
    if (gmtime_r(&now, &tm))
        strftime(timestamp, sizeof(timestamp), "%Y-%m-%dT%H:%M:%SZ", &tm);
    static const char* domains[] = {"event", "errno", "orbis", "http", "app"};
    char line[512];
    snprintf(line, sizeof(line), "%s [%s] %s %s code=0x%08X %s",
             timestamp, subsystem, operation, domains[static_cast<int>(domain)],
             static_cast<unsigned>(code), context);
    if (domain == ErrorDomain::Posix && code) {
        auto used = strlen(line);
        snprintf(line + used, sizeof(line) - used, " (%s)", strerror(code));
    }
    fprintf(stderr, "[Nebula Hub] %s\n", line);
    const char* path = DATA_DIR "diagnostics.log";
    if (FILE* out = fopen(path, "ab")) {
        if (fseek(out, 0, SEEK_END) == 0 && ftell(out) >= 128 * 1024) {
            fclose(out);
            if (rename(path, DATA_DIR "diagnostics.previous.log") == 0) out = fopen(path, "wb");
            else out = nullptr;
        }
        if (out) {
            fprintf(out, "%s\n", line);
            if (durable && fflush(out) == 0) fsync(fileno(out));
            fclose(out);
        }
    }
    std::string message = operation;
    if (code && domain == ErrorDomain::Posix) message += ": " + std::string(strerror(code));
    else if (code) {
        char original[32];
        snprintf(original, sizeof(original), " (0x%08X)", static_cast<unsigned>(code));
        message += original;
    }
    return message;
}

inline bool writableDirectory(const char* path, std::string* error = nullptr) {
    const auto failure = probeDirectory(path);
    if (!failure) return true;
    const auto message = diagnostic("storage", failure.operation, ErrorDomain::Posix,
                                    failure.code, path);
    if (error) *error = std::string(path) + " — " + message;
    return false;
}
