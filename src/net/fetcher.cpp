// NEBULA HUB — Fetcher implementation
// Uses PS4 native libSceNet / libSceHttp (same approach as 4PT and FPKGi)

#include "fetcher.h"

#include <orbis/Net.h>
#define sceHttpSetRecvTimeOut sdkSceHttpSetRecvTimeOut
#define sceHttpSetAutoRedirect sdkSceHttpSetAutoRedirect
#include <orbis/Http.h>
#undef sceHttpSetRecvTimeOut
#undef sceHttpSetAutoRedirect
#define sceSslTerm sdkSceSslTerm
#include <orbis/Ssl.h>
#undef sceSslTerm
#include <orbis/Sysmodule.h>

// OpenOrbis 0.5.4 leaves these declarations untyped.
extern "C" int sceHttpSetRecvTimeOut(int id, uint32_t usec);
extern "C" int sceHttpSetAutoRedirect(int id, int enabled);
extern "C" int sceSslTerm(int id);

#include <algorithm>
#include <orbis/libkernel.h>

#include <cstring>
#include <cstdio>
#include <fstream>
#include <sstream>
#include <sys/stat.h>

#define HTTP_BUF   (256 * 1024)   // 256 KB read buffer
#define THUMB_DIR  "/data/NEBULAHUB/cache/thumbs/"

// ─────────────────────────────────────────────
//  Init / teardown
// ─────────────────────────────────────────────

Fetcher::Fetcher() {
    mkdir(DATA_DIR, 0755);
    mkdir(DOWNLOAD_DIR, 0755);
    mkdir(CACHE_DIR, 0755);
    mkdir(THUMB_DIR, 0755);
    initNet();
    worker_ = std::thread([this]{ workerLoop(); });
}

Fetcher::~Fetcher() {
    running_ = false;
    if (worker_.joinable()) worker_.join();
    freeNet();
}

void Fetcher::initNet() {
    if (sceSysmoduleLoadModuleInternal(ORBIS_SYSMODULE_INTERNAL_NET) < 0 ||
        sceSysmoduleLoadModuleInternal(ORBIS_SYSMODULE_INTERNAL_SSL) < 0 ||
        sceSysmoduleLoadModuleInternal(ORBIS_SYSMODULE_INTERNAL_HTTP) < 0 ||
        sceNetInit() < 0) return;
    netInitialized_ = true;
    netPool_ = sceNetPoolCreate("NebulaHub", 1024 * 1024, 0);
    if (netPool_ < 0) return;
    sslCtx_ = sceSslInit(0x40000);
    if (sslCtx_ < 0) return;
    httpCtx_ = sceHttpInit(netPool_, sslCtx_, 0x40000);
    if (httpCtx_ < 0) return;
    httpTemplate_ = sceHttpCreateTemplate(httpCtx_, "NebulaHub/1.0", ORBIS_HTTP_VERSION_1_1, 0);
    if (httpTemplate_ < 0) return;
    sceHttpSetAutoRedirect(httpTemplate_, 1);
    sceHttpSetConnectTimeOut(httpTemplate_, 10 * 1000 * 1000);
    sceHttpSetSendTimeOut(httpTemplate_, 30 * 1000 * 1000);
    sceHttpSetRecvTimeOut(httpTemplate_, 30 * 1000 * 1000);
}

void Fetcher::freeNet() {
    if (httpTemplate_ >= 0) sceHttpDeleteTemplate(httpTemplate_);
    if (httpCtx_ >= 0) sceHttpTerm(httpCtx_);
    if (sslCtx_ >= 0) sceSslTerm(sslCtx_);
    if (netPool_ >= 0) sceNetPoolDestroy(netPool_);
    if (netInitialized_) sceNetTerm();
}

// ─────────────────────────────────────────────
//  HTTP helpers
// ─────────────────────────────────────────────

bool Fetcher::httpGet(const std::string& url, std::string& body, std::string* errOut) {
    if (httpTemplate_ < 0) return false;
    int conn = sceHttpCreateConnectionWithURL(httpTemplate_, url.c_str(), 1);
    if (conn < 0) { if (errOut) *errOut = "conn fail"; return false; }

    int req = sceHttpCreateRequestWithURL(conn, ORBIS_METHOD_GET, url.c_str(), 0);
    if (req < 0) { sceHttpDeleteConnection(conn); return false; }

    int ret = sceHttpSendRequest(req, nullptr, 0);
    if (ret < 0) { sceHttpDeleteRequest(req); sceHttpDeleteConnection(conn); return false; }

    int statusCode = 0;
    sceHttpGetStatusCode(req, &statusCode);
    if (statusCode != 200) {
        sceHttpDeleteRequest(req); sceHttpDeleteConnection(conn);
        if (errOut) *errOut = "HTTP " + std::to_string(statusCode);
        return false;
    }

    char buf[HTTP_BUF];
    body.clear();
    int read = -1;
    while (running_ && (read = sceHttpReadData(req, buf, sizeof(buf))) > 0) {
        if (body.size() + static_cast<size_t>(read) > 16 * 1024 * 1024) break;
        body.append(buf, read);
    }

    sceHttpDeleteRequest(req);
    sceHttpDeleteConnection(conn);
    return read == 0;
}

bool Fetcher::httpDownload(DownloadJob& job) {
    if (httpTemplate_ < 0) { job.errorMsg = "network unavailable"; return false; }
    int conn = sceHttpCreateConnectionWithURL(httpTemplate_, job.url.c_str(), 1);
    if (conn < 0) { job.errorMsg = "connection failed"; return false; }

    int req = sceHttpCreateRequestWithURL(conn, ORBIS_METHOD_GET, job.url.c_str(), 0);
    if (req < 0) { sceHttpDeleteConnection(conn); return false; }

    if (sceHttpSendRequest(req, nullptr, 0) < 0) {
        sceHttpDeleteRequest(req); sceHttpDeleteConnection(conn);
        job.errorMsg = "request failed"; return false;
    }

    int status = 0;
    sceHttpGetStatusCode(req, &status);
    if (status != 200) {
        sceHttpDeleteRequest(req); sceHttpDeleteConnection(conn);
        job.errorMsg = "HTTP " + std::to_string(status); return false;
    }

    size_t contentLen = 0;
    int lengthType = 0;
    if (sceHttpGetResponseContentLength(req, &lengthType, &contentLen) >= 0 && contentLen > 0)
        job.totalBytes = contentLen;
    else
        contentLen = 0;

    const std::string partial = job.destPath + ".part";
    std::ofstream out(partial, std::ios::binary);
    if (!out) {
        sceHttpDeleteRequest(req);
        sceHttpDeleteConnection(conn);
        job.errorMsg = "can't open destination";
        return false;
    }

    char buf[HTTP_BUF];
    int read = -1;
    uint64_t prev  = 0;
    uint64_t t0    = sceKernelGetProcessTime();

    while (running_ && publishProgress(job)) {
        read = sceHttpReadData(req, buf, sizeof(buf));
        if (read < 0) { job.errorMsg = "read error"; break; }
        if (read == 0) break;

        if (!out.write(buf, read)) { job.errorMsg = "write error"; break; }
        job.bytesDown += read;

        uint64_t now  = sceKernelGetProcessTime();
        double   dt   = (double)(now - t0) / 1000000.0;  // us → s
        if (dt > 0.5) {
            job.speedBps = (float)((job.bytesDown - prev) / dt);
            prev = job.bytesDown;
            t0   = now;
        }
    }

    sceHttpDeleteRequest(req);
    sceHttpDeleteConnection(conn);

    out.close();
    const bool ok = read == 0 && out && running_ && publishProgress(job) &&
                    (!contentLen || job.bytesDown == contentLen) &&
                    rename(partial.c_str(), job.destPath.c_str()) == 0;
    if (!ok) {
        remove(partial.c_str());
        if (job.errorMsg.empty()) job.errorMsg = "incomplete";
    }
    return ok;
}

// ─────────────────────────────────────────────
//  Worker loop
// ─────────────────────────────────────────────

void Fetcher::workerLoop() {
    while (running_) {
        std::function<void()> request;
        DownloadJob active;
        {
            std::lock_guard<std::mutex> lock(qMu_);
            if (!requests_.empty()) {
                request = std::move(requests_.front());
                requests_.pop_front();
            } else {
                for (auto& job : queue_) {
                    if (job.state == InstallState::Queued && !job.paused) {
                        job.bytesDown = 0;
                        job.errorMsg.clear();
                        job.state = InstallState::Downloading;
                        active = job;
                        break;
                    }
                }
            }
        }

        if (request) {
            request();
        } else if (!active.id.empty()) {
            const bool ok = httpDownload(active);
            std::lock_guard<std::mutex> lock(qMu_);
            for (auto& job : queue_) {
                if (job.id == active.id && job.url == active.url && job.state == InstallState::Downloading) {
                    job.errorMsg = active.errorMsg;
                    job.speedBps = 0;
                    job.state = ok ? InstallState::Done : InstallState::Error;
                    break;
                }
            }
        } else {
            sceKernelUsleep(50000);
        }
    }
}

bool Fetcher::publishProgress(const DownloadJob& active) {
    std::lock_guard<std::mutex> lock(qMu_);
    for (auto& job : queue_) {
        if (job.id == active.id && job.url == active.url && job.state == InstallState::Downloading) {
            job.totalBytes = active.totalBytes;
            job.bytesDown = active.bytesDown;
            job.speedBps = active.speedBps;
            return !job.paused;
        }
    }
    return false;
}

void Fetcher::fetchManifestAsync(const std::string& url) {
    if (url.empty()) return;
    std::lock_guard<std::mutex> lock(qMu_);
    requests_.push_back([this, url] {
        std::string body;
        if (httpGet(url, body)) {
            std::lock_guard<std::mutex> lock(qMu_);
            manifestCache_ = std::move(body);
        }
    });
}

std::string Fetcher::takeManifest() {
    std::lock_guard<std::mutex> lock(qMu_);
    return std::move(manifestCache_);
}

std::string Fetcher::thumbnail(const std::string& url, const std::string& cacheKey) {
    if (url.empty()) return {};
    std::string path = std::string(THUMB_DIR) + cacheKey + ".jpg";
    struct stat st{};
    if (stat(path.c_str(), &st) == 0 && st.st_size > 0) return path;
    std::lock_guard<std::mutex> lock(qMu_);
    if (!thumbnailRequests_.insert(cacheKey).second) return {};
    requests_.push_back([this, url, path] {
        std::string body;
        if (!httpGet(url, body) || body.empty()) return;
        const std::string partial = path + ".part";
        std::ofstream file(partial, std::ios::binary);
        file.write(body.data(), body.size());
        file.close();
        if (!file || rename(partial.c_str(), path.c_str()) != 0) remove(partial.c_str());
    });
    return {};
}

void Fetcher::enqueue(DownloadJob job) {
    std::lock_guard<std::mutex> lk(qMu_);
    // Avoid duplicate
    for (auto& existing : queue_) {
        if (existing.id == job.id && existing.url == job.url) {
            if (existing.state == InstallState::Error || existing.state == InstallState::Done)
                existing = std::move(job);
            return;
        }
    }
    queue_.push_back(std::move(job));
}

void Fetcher::pause(const std::string& id) {
    std::lock_guard<std::mutex> lk(qMu_);
    for (auto& j : queue_) if (j.id == id) j.paused = true;
}

void Fetcher::resume(const std::string& id) {
    std::lock_guard<std::mutex> lk(qMu_);
    for (auto& j : queue_) {
        if (j.id == id) {
            j.paused = false;
            if (j.state == InstallState::Error) j.state = InstallState::Queued;
        }
    }
}

void Fetcher::cancel(const std::string& id) {
    std::lock_guard<std::mutex> lk(qMu_);
    queue_.erase(std::remove_if(queue_.begin(), queue_.end(),
        [&id](const DownloadJob& j){ return j.id == id; }), queue_.end());
}

void Fetcher::retry(const std::string& id) {
    std::lock_guard<std::mutex> lk(qMu_);
    for (auto& j : queue_) {
        if (j.id == id && j.state == InstallState::Error) {
            j.state = InstallState::Queued;
            j.bytesDown = 0;
            j.errorMsg.clear();
        }
    }
}

std::vector<DownloadJob> Fetcher::queueSnapshot() {
    std::lock_guard<std::mutex> lk(qMu_);
    return queue_;
}

bool Fetcher::isDownloading() const {
    std::lock_guard<std::mutex> lk(qMu_);
    for (auto& j : queue_)
        if (j.state == InstallState::Downloading) return true;
    return false;
}

int Fetcher::activeCount() const {
    std::lock_guard<std::mutex> lk(qMu_);
    int n = 0;
    for (auto& j : queue_)
        if (j.state == InstallState::Downloading || j.state == InstallState::Queued) ++n;
    return n;
}
