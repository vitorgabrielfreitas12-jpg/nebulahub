#pragma once

#include "../db/database.h"
#include <atomic>
#include <mutex>
#include <string>
#include <thread>

enum class InstallCommand { Request, ConfirmFirst, ClearProgress };

class Installer {
public:
    ~Installer();
    bool submit(ContentEntry entry, InstallCommand command = InstallCommand::Request);
    bool busy() const { return busy_; }
    std::string takeResult(const std::string& id);

private:
    std::string installAvatar(const ContentEntry& entry);
    std::string installTheme(const ContentEntry& entry, InstallCommand command);

    std::thread worker_;
    std::atomic<bool> busy_{false};
    std::mutex mutex_;
    std::string resultId_;
    std::string result_;
};
