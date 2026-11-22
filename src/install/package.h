#pragma once

#include <cstdint>
#include <cstdio>
#include <string>
#include <sys/stat.h>

struct PackageInfo {
    std::string contentId;
    uint32_t type = 0;
    uint32_t iroTag = 0;
};

inline bool isCompletePackage(const std::string& path, uint64_t expected,
                              PackageInfo* infoOut = nullptr) {
    struct stat info{};
    if (stat(path.c_str(), &info) != 0 || !S_ISREG(info.st_mode) || info.st_size < 0x1000)
        return false;
    const auto size = static_cast<uint64_t>(info.st_size);
    if (expected && expected != size) return false;
    FILE* file = fopen(path.c_str(), "rb");
    if (!file) return false;
    unsigned char header[0x438];
    const auto read = fread(header, 1, sizeof(header), file);
    fclose(file);
    if (read != sizeof(header)) return false;
    const auto readBE = [&](unsigned offset, unsigned count) {
        uint64_t value = 0;
        for (unsigned i = 0; i < count; ++i) value = (value << 8) | header[offset + i];
        return value;
    };
    const auto bodyOffset = readBE(0x20, 8);
    const auto bodySize = readBE(0x28, 8);
    const auto pkgSize = readBE(0x430, 8);
    if (readBE(0, 4) != 0x7f434e54 || bodyOffset < 0x1000 || bodyOffset >= size ||
        !bodySize || bodySize > size - bodyOffset) return false;
    // Some retail packages omit the size field. Require the declared body to
    // end at EOF in that case, in addition to the catalog size when supplied.
    if (pkgSize ? pkgSize != size : bodySize != size - bodyOffset) return false;
    if (infoOut) {
        infoOut->contentId.assign(reinterpret_cast<const char*>(header + 0x40), 36);
        infoOut->type = static_cast<uint32_t>(readBE(0x74, 4));
        infoOut->iroTag = static_cast<uint32_t>(readBE(0x98, 4));
    }
    return true;
}
