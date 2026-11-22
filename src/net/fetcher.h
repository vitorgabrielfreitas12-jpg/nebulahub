#pragma once
#include "../nebula.h"
#include <string>
#include <functional>
#include <vector>
#include <atomic>
#include <thread>
#include <mutex>

// ─────────────────────────────────────────────
//  Download job
// ─────────────────────────────────────────────

struct DownloadJob {
    std::string  id;           // content entry id
    std::string  url;
    std::string  destPath;
    uint64_t     totalBytes   = 0;
    uint64_t     bytesDown    = 0;
    float        speedBps     = 0.f;
    InstallState state        = InstallState::Queued;
    std::string  errorMsg;
    bool         paused       = false;

    float progress() const {
        if (totalBytes == 0) return 0.f;
        return (float)bytesDown / (float)totalBytes;
    }

    std::string speedStr() const {
        if (speedBps < 1024.f) return std::to_string((int)speedBps) + " B/s";
        if (speedBps < 1024.f*1024.f) return std::to_string((int)(speedBps/1024.f)) + " KB/s";
        char buf[32];
        snprintf(buf, sizeof(buf), "%.1f MB/s", speedBps/(1024.f*1024.f));
        return buf;
    }

    std::string etaStr() const {
        if (speedBps <= 0 || totalBytes == 0) return "--:--";
        uint64_t rem  = totalBytes - bytesDown;
        uint64_t secs = (uint64_t)(rem / speedBps);
        char buf[16];
        snprintf(buf, sizeof(buf), "%02llu:%02llu",
                 static_cast<unsigned long long>(secs / 60),
                 static_cast<unsigned long long>(secs % 60));
        return buf;
    }
};

// ─────────────────────────────────────────────
//  Fetcher
// ─────────────────────────────────────────────

class Fetcher {
public:
    Fetcher();
    ~Fetcher();

    void setManifestUrl(const std::string& url);

    // Async manifest fetch
    void fetchManifestAsync(std::function<void(bool)> cb);
    std::string getManifest() const;

    // Thumbnail fetch (cached to disk)
    // Returns SDL_Surface* as void* to avoid SDL include here
    void fetchThumbAsync(const std::string& url,
                         const std::string& cacheKey,
                         std::function<void(std::string)> onPathReady);

    // Download queue
    void enqueue(DownloadJob job);
    void pause(const std::string& id);
    void resume(const std::string& id);
    void cancel(const std::string& id);
    void retry(const std::string& id);

    std::vector<DownloadJob> queueSnapshot();

    bool    isDownloading() const;
    int     activeCount()   const;

private:
    std::string  manifestUrl_;
    std::string  manifestCache_;

    std::vector<DownloadJob> queue_;
    mutable std::mutex       qMu_;
    std::thread              worker_;
    std::atomic<bool>        running_{true};

    void workerLoop();
    bool httpGet(const std::string& url, std::string& body, std::string* errOut = nullptr);
    bool httpDownload(DownloadJob& job);

    // PS4 native HTTP helpers
    int netHandle_ = -1;
    int httpCtx_   = -1;
    void initNet();
    void freeNet();
};
