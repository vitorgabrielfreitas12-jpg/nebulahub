#pragma once
#include "../nebula.h"
#include "../db/database.h"
#include "rejected.h"
#include "../install/package.h"
#include <algorithm>
#include <string>
#include <cstdint>
#include <cstdio>
#include <functional>
#include <condition_variable>
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

enum class DownloadState { Queued, Connecting, Downloading, Validating, Completed, Failed, Cancelled };

inline bool activeDownload(DownloadState state) {
    return state == DownloadState::Connecting || state == DownloadState::Downloading ||
           state == DownloadState::Validating;
}

enum class PreviewState { Missing, Loading, Ready, Illustration, Failed };
struct Preview {
    std::shared_ptr<SDL_Surface> image;
    PreviewState state = PreviewState::Missing;
};

struct DownloadJob {
    uint64_t generation = 0;
    std::string  id;           // content entry id
    std::string  url;
    std::string  destPath;
    uint64_t     totalBytes   = 0;
    uint64_t     catalogBytes = 0;
    uint64_t     bytesDown    = 0;
    float        speedBps     = 0.f;
    DownloadState state = DownloadState::Queued;
    bool rejected = false;
    PackageRole role = PackageRole::None;
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

inline DownloadJob beginNextDownload(std::vector<DownloadJob>& queue) {
    if (std::any_of(queue.begin(), queue.end(), [](const auto& job) { return activeDownload(job.state); }))
        return {};
    for (auto& job : queue) {
        if (job.state != DownloadState::Queued || job.paused) continue;
        job.bytesDown = 0;
        job.totalBytes = job.catalogBytes;
        job.errorMsg.clear();
        job.state = DownloadState::Connecting;
        return job;
    }
    return {};
}

inline void cancelOrDismiss(std::vector<DownloadJob>& queue, const std::string& id) {
    const auto found = std::find_if(queue.begin(), queue.end(), [&](const auto& job) { return job.id == id; });
    if (found == queue.end()) return;
    if (found->state == DownloadState::Queued || activeDownload(found->state)) {
        found->state = DownloadState::Cancelled;
        found->paused = false;
        found->speedBps = 0;
    } else {
        // Removing history does not remove the file.
        queue.erase(found);
    }
}

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
    Preview thumbnail(const std::string& url, const std::string& cacheKey);

    // Download queue
    void enqueue(DownloadJob job);
    void pause(const std::string& id);
    void resume(const std::string& id);
    void cancel(const std::string& id);
    void retry(const std::string& id);
    bool keepingRejected() const { return keepRejected_; }
    void toggleKeepRejected() { keepRejected_ = !keepRejected_; }
    uint64_t rejectedBytes() const { return rejectedBytes_; }
    void requestClearRejected() { clearRejected_ = true; }

    std::vector<DownloadJob> queueSnapshot();

    bool    isDownloading() const;
    int     activeCount()   const;

private:
    std::deque<std::function<void()>> requests_;
    std::deque<std::function<void()>> localPreviews_;
    std::condition_variable previewReady_;
    std::thread previewWorker_;
    std::unordered_map<std::string, Preview> thumbnails_;
    std::deque<std::string> thumbnailOrder_;
    uint64_t generation_ = 0;
    RejectedDownload rejected_{REJECTED_DOWNLOAD};
    std::atomic<bool> keepRejected_{false};
    std::atomic<bool> clearRejected_{false};
    std::atomic<uint64_t> rejectedBytes_{0};
    bool cacheWritable_ = false;
    std::string networkError_;
    std::unique_ptr<Database> manifestCache_;
    std::string manifestStatus_;

    std::vector<DownloadJob> queue_;
    mutable std::mutex       qMu_;
    std::thread              worker_;
    std::atomic<bool>        running_{true};

    void workerLoop();
    void localPreviewLoop();
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
