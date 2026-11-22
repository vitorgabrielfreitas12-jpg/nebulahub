#pragma once

#include "service.h"
#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <unistd.h>

enum class ThemePhase { None, FirstRequested, FirstAccepted, SecondRequested, SecondAccepted };
enum class ThemeAction { First, Second, ConfirmFirst, CheckSecond, OtherTheme, Reset };

class ThemeProgress {
public:
    explicit ThemeProgress(const std::string& path) : path_(path) {
        FILE* file = fopen(path_.c_str(), "rb");
        if (!file) {
            if (errno != ENOENT) unreadable_ = true;
            return;
        }
        char record[256]{};
        const bool read = fgets(record, sizeof(record), file);
        const bool atEnd = fgetc(file) == EOF && !ferror(file);
        fclose(file);
        const std::string line(record);
        if (!read || !atEnd || line.size() < 5 || line[0] < '1' || line[0] > '4' ||
            line[1] != ' ' || line.back() != '\n') {
            unreadable_ = true;
            return;
        }
        phase = static_cast<ThemePhase>(line[0] - '0');
        key = line.substr(2, line.size() - 3);
    }

    ThemeAction next(const std::string& theme, bool confirmed) const {
        if (unreadable_) return ThemeAction::Reset;
        if (phase == ThemePhase::None) return ThemeAction::First;
        if (key != theme) return ThemeAction::OtherTheme;
        if (phase == ThemePhase::SecondRequested || phase == ThemePhase::SecondAccepted)
            return ThemeAction::CheckSecond;
        return confirmed ? ThemeAction::Second : ThemeAction::ConfirmFirst;
    }

    int record(ThemePhase next, const std::string& theme) {
        if (theme.empty() || theme.size() > 200 || theme.find_first_of("\r\n") != std::string::npos)
            return EINVAL;
        std::string partial = path_ + ".XXXXXX";
        const int fd = mkstemp(partial.data());
        if (fd < 0) return errno;
        FILE* file = fdopen(fd, "wb");
        if (!file) {
            const int code = errno;
            close(fd);
            unlink(partial.c_str());
            return code;
        }
        int code = 0;
        if (fprintf(file, "%d %s\n", static_cast<int>(next), theme.c_str()) < 0 ||
            fflush(file) != 0 || fsync(fd) != 0) code = errno ? errno : EIO;
        if (fclose(file) != 0 && !code) code = errno;
        if (!code && rename(partial.c_str(), path_.c_str()) != 0) code = errno;
        if (code) {
            unlink(partial.c_str());
            return code;
        }
        phase = next;
        key = theme;
        unreadable_ = false;
        return 0;
    }

    int clear() {
        if (unlink(path_.c_str()) != 0 && errno != ENOENT) return errno;
        phase = ThemePhase::None;
        key.clear();
        unreadable_ = false;
        return 0;
    }

    ThemePhase phase = ThemePhase::None;
    std::string key;

private:
    std::string path_;
    bool unreadable_ = false;
};

// Record a request before calling the system. If the app exits during that call,
// the next session asks the user to check the console before proceeding.
inline InstallResult requestThemePart(ThemeProgress& progress, const std::string& key,
                                      bool partTwo, const std::function<InstallResult(
                                          const std::function<bool()>&)>& send,
                                      const InstallTrace& trace) {
    const auto save = [&](ThemePhase phase) {
        trace("Save theme request state", false, static_cast<int>(phase));
        const auto code = progress.record(phase, key);
        trace("Save theme request state", true, code);
        return code == 0;
    };
    const auto result = send([&] {
        return save(partTwo ? ThemePhase::SecondRequested : ThemePhase::FirstRequested);
    });
    if (result.accepted)
        save(partTwo ? ThemePhase::SecondAccepted : ThemePhase::FirstAccepted);
    return result;
}
