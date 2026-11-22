#pragma once

#include <cstdint>
#include <cerrno>
#include <cstdio>
#include <string>
#include <sys/stat.h>

struct PackageInfo {
    std::string contentId;
    uint32_t type = 0;
    uint32_t iroTag = 0;
};

enum class PackageStatus { Valid, Missing, Incomplete, Invalid, IoError };

inline PackageStatus inspectPackage(const std::string& path, uint64_t expected,
                                    PackageInfo* infoOut = nullptr, int* ioCode = nullptr) {
    const auto ioFailure = [&](int code) {
        if (ioCode) *ioCode = code;
        return code == ENOENT ? PackageStatus::Missing : PackageStatus::IoError;
    };
    struct stat info{};
    if (stat(path.c_str(), &info) != 0) return ioFailure(errno);
    if (!S_ISREG(info.st_mode)) return PackageStatus::Invalid;
    if (info.st_size < 0x1000) return PackageStatus::Incomplete;
    const auto size = static_cast<uint64_t>(info.st_size);
    if (expected && expected != size)
        return size < expected ? PackageStatus::Incomplete : PackageStatus::Invalid;
    FILE* file = fopen(path.c_str(), "rb");
    if (!file) return ioFailure(errno);
    unsigned char header[0x438];
    const auto read = fread(header, 1, sizeof(header), file);
    const int readError = ferror(file) ? (errno ? errno : EIO) : 0;
    fclose(file);
    if (readError) return ioFailure(readError);
    if (read != sizeof(header)) return PackageStatus::Incomplete;
    const auto readBE = [&](unsigned offset, unsigned count) {
        uint64_t value = 0;
        for (unsigned i = 0; i < count; ++i) value = (value << 8) | header[offset + i];
        return value;
    };
    const auto bodyOffset = readBE(0x20, 8);
    const auto bodySize = readBE(0x28, 8);
    const auto pkgSize = readBE(0x430, 8);
    if (readBE(0, 4) != 0x7f434e54 || bodyOffset < 0x1000 || !bodySize)
        return PackageStatus::Invalid;
    if (bodyOffset >= size || bodySize > size - bodyOffset || pkgSize > size)
        return PackageStatus::Incomplete;
    // Some retail packages omit package_size (0x430). In that case the
    // declared body must end exactly at EOF.
    if (pkgSize ? pkgSize != size : bodySize != size - bodyOffset)
        return PackageStatus::Invalid;
    if (infoOut) {
        infoOut->contentId.assign(reinterpret_cast<const char*>(header + 0x40), 36);
        infoOut->type = static_cast<uint32_t>(readBE(0x74, 4));
        infoOut->iroTag = static_cast<uint32_t>(readBE(0x98, 4));
    }
    return PackageStatus::Valid;
}


inline bool isCompletePackage(const std::string& path, uint64_t expected,
                              PackageInfo* infoOut = nullptr) {
    return inspectPackage(path, expected, infoOut) == PackageStatus::Valid;
}


inline bool isThemePayload(const PackageInfo& package) {
    return package.type == 0x1b && package.iroTag == 2;
}

inline bool isThemePair(const PackageInfo& first, const PackageInfo& second) {
    return first.contentId == second.contentId && !first.contentId.empty() &&
           (first.type == 0x1c || isThemePayload(first)) && isThemePayload(second);
}
