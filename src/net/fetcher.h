#pragma once
#include "../nebula.h"
#include "../db/database.h"
#include <string>
#include <cstdint>
#include <cstdio>
#include <functional>
#include <vector>
#include <atomic>
#include <thread>
#include <mutex>
#include <deque>
#include <unordered_map>
#include <memory>
#include <SDL2/SDL_surface.h>

// ─────────────────────────────────────────────
//  Download job
// ─────────────────────────────────────────────

struct DownloadJob {
    uint64_t generation = 0;
    std::string  id;           // content entry id
    std::string  url;
    std::string  destPath;
    uint64_t     totalBytes   = 0;
    uint64_t     catalogBytes = 0;
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
        uint64_t rem = totalBytes > bytesDown ? totalBytes - bytesDown : 0;
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

    void fetchManifestAsync(const std::string& url);
    std::unique_ptr<Database> takeManifest();
    std::string manifestStatus() const;
    std::shared_ptr<SDL_Surface> thumbnail(const std::string& url, const std::string& cacheKey);

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
    std::deque<std::function<void()>> requests_;
    std::unordered_map<std::string, std::shared_ptr<SDL_Surface>> thumbnails_;
    std::deque<std::string> thumbnailOrder_;
    uint64_t generation_ = 0;
    bool cacheWritable_ = false;
    std::string networkError_;
    std::unique_ptr<Database> manifestCache_;
    std::string manifestStatus_;

    std::vector<DownloadJob> queue_;
    mutable std::mutex       qMu_;
    std::thread              worker_;
    std::atomic<bool>        running_{true};

    void workerLoop();
    bool publishProgress(const DownloadJob& active);
    bool httpGet(const std::string& url, std::string& body, size_t limit, const char* item,
                 std::string& error);
    bool httpDownload(DownloadJob& job, std::string& partial);

    // PS4 native HTTP helpers
    bool netInitialized_ = false;
    int netPool_ = -1;
    int sslCtx_ = -1;
    int httpCtx_ = -1;
    int httpTemplate_ = -1;
    void initNet();
    void freeNet();
};
