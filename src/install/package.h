#pragma once

#include "../file_input.h"
#include <cstdint>
#include <cstdio>
#include <string>

struct PackageInfo {
    std::string contentId;
    uint32_t type = 0;
    uint32_t iroTag = 0;
};

enum class PackageStatus { Valid, Missing, Incomplete, Invalid, IoError };

struct PackageInspection {
    PackageStatus status = PackageStatus::Valid;
    const char* reason = "PKG structure accepted";
    int ioError = 0;
    uint64_t fileSize = 0;
    size_t headerRead = 0;
    uint32_t magic = 0;
    uint64_t declaredSize = 0;
    uint64_t bodyOffset = 0;
    uint64_t bodySize = 0;
    uint64_t pfsOffset = 0;
    uint64_t pfsSize = 0;
    PackageInfo info;

    explicit operator bool() const { return status == PackageStatus::Valid; }
};

inline PackageInspection inspectPackageDetails(const std::string& path, uint64_t expected) {
    PackageInspection result;
    const auto reject = [&](PackageStatus status, const char* reason, int ioError = 0) {
        result.status = status;
        result.reason = reason;
        result.ioError = ioError;
        return result;
    };

    FileInput file(path.c_str());
    if (!file) return reject(file.error == ENOENT ? PackageStatus::Missing : PackageStatus::IoError,
                             "Open PKG file", file.error);
    if (!file.measure(result.fileSize))
        return reject(PackageStatus::IoError, "Seek PKG file", file.error);

    unsigned char header[0x438]{};
    result.headerRead = file.readBytes(header, sizeof(header));
    if (file.error) return reject(PackageStatus::IoError, "Read PKG header", file.error);
    const auto readBE = [&](unsigned offset, unsigned count) {
        uint64_t value = 0;
        for (unsigned i = 0; i < count; ++i) value = (value << 8) | header[offset + i];
        return value;
    };
    if (result.headerRead >= 4) {
        result.magic = static_cast<uint32_t>(readBE(0, 4));
        if (result.magic != 0x7f434e54) return reject(PackageStatus::Invalid, "Invalid PKG magic");
    }
    if (result.headerRead < sizeof(header) || result.fileSize < 0x1000)
        return reject(PackageStatus::Incomplete, "Truncated PKG header");

    const auto bytes = result.fileSize;
    result.declaredSize = readBE(0x430, 8);
    result.bodyOffset = readBE(0x20, 8);
    result.bodySize = readBE(0x28, 8);
    result.pfsOffset = readBE(0x410, 8);
    result.pfsSize = readBE(0x418, 8);
    result.info.contentId.assign(reinterpret_cast<const char*>(header + 0x40), 36);
    result.info.type = static_cast<uint32_t>(readBE(0x74, 4));
    result.info.iroTag = static_cast<uint32_t>(readBE(0x98, 4));
    if ((expected && expected != bytes) || (result.declaredSize && result.declaredSize != bytes))
        return reject(expected > bytes || result.declaredSize > bytes ? PackageStatus::Incomplete :
                      PackageStatus::Invalid, "PKG size mismatch");

    const auto body = result.bodyOffset;
    const auto bodySize = result.bodySize;
    const auto entries = readBE(0x10, 4);
    const auto table = readBE(0x18, 4);
    if (body < 0x1000 || !bodySize || !entries || table < body)
        return reject(PackageStatus::Invalid, "Invalid PKG offsets");
    if (body >= bytes || bodySize > bytes - body)
        return reject(PackageStatus::Incomplete, "PKG body extends beyond file");
    const auto bodyEnd = body + bodySize;
    if (table > bodyEnd || entries > (bodyEnd - table) / 32)
        return reject(PackageStatus::Invalid, "Invalid PKG entry table");

    // These are separate ranges in real CNT files. App PKGs can end their
    // metadata body before the PFS image. Retail files with package_size=0
    // must still account for EOF through a declared, bounded range.
    const auto pfs = result.pfsOffset;
    const auto pfsSize = result.pfsSize;
    if (pfsSize && (pfs < bodyEnd || pfs >= bytes || pfsSize > bytes - pfs))
        return reject(PackageStatus::Invalid, "Invalid PKG PFS range");
    if (!result.declaredSize && (pfsSize ? pfs + pfsSize : bodyEnd) != bytes)
        return reject(PackageStatus::Invalid, "PKG size cannot be established");
    return result;
}

inline PackageStatus inspectPackage(const std::string& path, uint64_t expected,
                                    PackageInfo* infoOut = nullptr, int* ioCode = nullptr) {
    const auto result = inspectPackageDetails(path, expected);
    if (result && infoOut) *infoOut = result.info;
    if (ioCode) *ioCode = result.ioError;
    return result.status;
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

inline std::string describePackage(const PackageInspection& package, uint64_t expected) {
    char details[256];
    snprintf(details, sizeof(details),
             "expected=%llu disk=%llu header_read=%zu magic=%08X declared=%llu body=%llu+%llu pfs=%llu+%llu type=0x%X iro=0x%X",
             static_cast<unsigned long long>(expected), static_cast<unsigned long long>(package.fileSize),
             package.headerRead, package.magic, static_cast<unsigned long long>(package.declaredSize),
             static_cast<unsigned long long>(package.bodyOffset), static_cast<unsigned long long>(package.bodySize),
             static_cast<unsigned long long>(package.pfsOffset), static_cast<unsigned long long>(package.pfsSize),
             package.info.type, package.info.iroTag);
    return details;
}


enum class PackageRole { None, AvatarPack, ThemeFirst, ThemePayload };

inline bool matchesPackageRole(const PackageInfo& package, PackageRole role) {
    switch (role) {
        case PackageRole::AvatarPack: return package.type == 0x1a;
        case PackageRole::ThemeFirst: return package.type == 0x1c || isThemePayload(package);
        case PackageRole::ThemePayload: return isThemePayload(package);
        default: return true;
    }
}
