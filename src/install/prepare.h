#pragma once

#include "../file_input.h"
#include "../storage.h"
#include <memory>
#include <functional>

using StageTrace = std::function<void(const char*, bool, int)>;

inline StorageFailure stagePackage(const std::string& source, const std::string& destination,
                                   const StageTrace& trace) {
    trace("Open source", false, 0);
    FileInput input(source.c_str());
    trace("Open source", true, input.error);
    if (!input) return {"open source", input.error};

    trace("Allocate copy buffer", false, 0);
    auto bytes = std::make_unique<unsigned char[]>(64 * 1024);
    trace("Allocate copy buffer", true, 0);

    struct Partial {
        std::string path;
        int fd = -1;
        bool exists = false;
        ~Partial() {
            if (fd >= 0) close(fd);
            if (exists) unlink(path.c_str());
        }
    } partial{destination + ".XXXXXX"};
    trace("Create staging file", false, 0);
    partial.fd = mkstemp(partial.path.data());
    const int createError = partial.fd < 0 ? errno : 0;
    partial.exists = partial.fd >= 0;
    trace("Create staging file", true, createError);
    if (!partial.exists) return {"create staging file", createError};

    trace("Open staging stream", false, 0);
    const int fd = partial.fd;
    FILE* stream = fdopen(fd, "wb");
    const int openError = stream ? 0 : errno;
    const auto closeStream = [](FILE* file) { fclose(file); };
    std::unique_ptr<FILE, decltype(closeStream)> output(stream, closeStream);
    if (stream) partial.fd = -1;
    trace("Open staging stream", true, openError);
    if (!stream) return {"open staging stream", openError};

    trace("Copy package bytes", false, 0);
    StorageFailure failure;
    for (;;) {
        const auto count = input.readBytes(bytes.get(), 64 * 1024);
        if (input.error) {
            failure = {"read source", input.error};
            break;
        }
        if (!count) break;
        failure = writeStorage(stream, bytes.get(), count);
        if (failure) break;
    }
    trace("Copy package bytes", true, failure.code);
    if (!failure) {
        // Preserve the read permissions previously used for files handed to the PS4 service.
        trace("Set staged file permissions", false, 0);
        if (fchmod(fd, 0644) != 0) failure = {"set staged file permissions", errno};
        trace("Set staged file permissions", true, failure.code);
    }
    trace("Close staging stream", false, 0);
    const int closeError = fclose(output.release()) == 0 ? 0 : errno;
    trace("Close staging stream", true, closeError);
    if (closeError && !failure) failure = {"close staging file", closeError};

    if (!failure) {
        trace("Publish staged package", false, 0);
        if (rename(partial.path.c_str(), destination.c_str()) != 0)
            failure = {"publish staged package", errno};
        else partial.exists = false;
        trace("Publish staged package", true, failure.code);
    }
    return failure;
}
