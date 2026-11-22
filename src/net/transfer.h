#pragma once

#include <cstdint>

inline bool transferComplete(bool eof, bool cancelled, uint64_t received,
                             uint64_t expected, uint64_t contentLength) {
    return eof && !cancelled && received > 0 &&
           (!expected || received == expected) &&
           (!contentLength || received == contentLength);
}
