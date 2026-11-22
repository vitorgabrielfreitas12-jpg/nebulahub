// NEBULA HUB — Fetcher implementation
// Uses PS4 native libSceNet / libSceHttp (same approach as 4PT and FPKGi)

#include "fetcher.h"
#include "../diagnostics.h"
#include "../install/package.h"

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
#include <SDL2/SDL_image.h>

#include <cstring>
#include <cstdio>
#include <fstream>
#include <sstream>
#include <sys/stat.h>

#define HTTP_BUF (16 * 1024)
#define THUMB_DIR CACHE_DIR "thumbs/"

// ─────────────────────────────────────────────
//  Init / teardown
// ─────────────────────────────────────────────

Fetcher::Fetcher() {
    writableDirectory(DATA_DIR);
    downloadsWritable_ = writableDirectory(DOWNLOAD_DIR);
    cacheWritable_ = writableDirectory(CACHE_DIR) && writableDirectory(THUMB_DIR);
    try {
        worker_ = std::thread([this] {
            initNet();
            workerLoop();
            freeNet();
        });
    } catch (const std::system_error& error) {
        diagnostic("Start network worker", error.code().value());
        running_ = false;
    }
}

Fetcher::~Fetcher() {
    running_ = false;
    if (worker_.joinable()) worker_.join();
}

void Fetcher::initNet() {
    diagnostic("Initialize HTTP services", 0);
    const auto check = [this](const char* stage, int status) {
        if (status < 0) networkError_ = diagnostic(stage, status);
        return status >= 0;
    };
    if (!check("load Net", sceSysmoduleLoadModuleInternal(ORBIS_SYSMODULE_INTERNAL_NET)) ||
        !check("load SSL", sceSysmoduleLoadModuleInternal(ORBIS_SYSMODULE_INTERNAL_SSL)) ||
        !check("load HTTP", sceSysmoduleLoadModuleInternal(ORBIS_SYSMODULE_INTERNAL_HTTP)) ||
        !check("sceNetInit", sceNetInit())) return;
    netInitialized_ = true;
    netPool_ = sceNetPoolCreate("NebulaHub", 1024 * 1024, 0);
    if (!check("sceNetPoolCreate", netPool_)) return;
    sslCtx_ = sceSslInit(0x40000);
    if (!check("sceSslInit", sslCtx_)) return;
    httpCtx_ = sceHttpInit(netPool_, sslCtx_, 0x40000);
    if (!check("sceHttpInit", httpCtx_)) return;
    httpTemplate_ = sceHttpCreateTemplate(httpCtx_, "NebulaHub/1.0", ORBIS_HTTP_VERSION_1_1, 0);
    if (!check("sceHttpCreateTemplate", httpTemplate_)) return;
    check("sceHttpSetAutoRedirect", sceHttpSetAutoRedirect(httpTemplate_, 1));
    check("sceHttpSetConnectTimeOut", sceHttpSetConnectTimeOut(httpTemplate_, 5000000));
    check("sceHttpSetSendTimeOut", sceHttpSetSendTimeOut(httpTemplate_, 5000000));
    check("sceHttpSetRecvTimeOut", sceHttpSetRecvTimeOut(httpTemplate_, 8000000));
    networkError_.clear();
    diagnostic("HTTP initialized", 0);
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

namespace {
struct HttpRequest {
    int connection = -1;
    int request = -1;
    ~HttpRequest() {
        if (request >= 0) sceHttpDeleteRequest(request);
        if (connection >= 0) sceHttpDeleteConnection(connection);
    }

    bool open(int httpTemplate, const std::string& url, std::string& error) {
        const auto hostEnd = url.find_first_of("/?#", 8);
        if (url.compare(0, 8, "https://") != 0 || url.size() <= 8 || hostEnd == 8 ||
            url.find_first_of(" \r\n\t") != std::string::npos) {
            error = diagnostic("Invalid HTTPS URL", EINVAL);
            return false;
        }
        connection = sceHttpCreateConnectionWithURL(httpTemplate, url.c_str(), false);
        if (connection < 0) {
            error = diagnostic("HTTP create connection", connection);
            return false;
        }
        request = sceHttpCreateRequestWithURL(connection, ORBIS_METHOD_GET, url.c_str(), 0);
        if (request < 0) {
            error = diagnostic("HTTP create request", request);
            return false;
        }
        diagnostic("HTTP send request", 0);
        int status = sceHttpSendRequest(request, nullptr, 0);
        if (status < 0) {
            error = diagnostic("HTTP send / TLS", status);
            return false;
        }
        int response = 0;
        status = sceHttpGetStatusCode(request, &response);
        if (status < 0) {
            error = diagnostic("HTTP read status", status);
            return false;
        }
        if (response != 200) {
            error = "HTTP " + std::to_string(response);
            diagnostic("HTTP response", response);
            return false;
        }
        return true;
    }
};
}

bool Fetcher::httpGet(const std::string& url, std::string& body, size_t limit) {
    if (httpTemplate_ < 0) return false;
    HttpRequest request;
    std::string error;
    if (!request.open(httpTemplate_, url, error)) return false;
    char buffer[HTTP_BUF];
    body.clear();
    while (running_) {
        const int received = sceHttpReadData(request.request, buffer, sizeof(buffer));
        if (received == 0) return !body.empty();
        if (received < 0 || body.size() + static_cast<size_t>(received) > limit) {
            diagnostic("HTTP read response", received < 0 ? received : EFBIG);
            return false;
        }
        body.append(buffer, received);
    }
    return false;
}

bool Fetcher::httpDownload(DownloadJob& job) {
    diagnostic(("Download start " + job.id).c_str(), 0);
    if (!downloadsWritable_) {
        job.errorMsg = "Download directory is not writable";
        return false;
    }
    if (httpTemplate_ < 0) {
        job.errorMsg = networkError_.empty() ? "Network unavailable" : networkError_;
        return false;
    }
    HttpRequest request;
    if (!request.open(httpTemplate_, job.url, job.errorMsg)) return false;
    const int req = request.request;
    const auto expected = job.totalBytes;
    size_t contentLen = 0;
    int lengthType = 0;
    if (sceHttpGetResponseContentLength(req, &lengthType, &contentLen) >= 0 && contentLen > 0)
        job.totalBytes = contentLen;
    else
        contentLen = 0;

    if (expected && contentLen && expected != contentLen) {
        job.errorMsg = diagnostic("Download length differs from catalog", EINVAL);
        return false;
    }
    const std::string partial = job.destPath + ".part";
    std::ofstream out(partial, std::ios::binary);
    if (!out) {
        job.errorMsg = diagnostic("Open download temporary file", errno);
        return false;
    }

    char buf[HTTP_BUF];
    int read = -1;
    uint64_t prev  = 0;
    uint64_t t0    = sceKernelGetProcessTime();

    while (running_ && publishProgress(job)) {
        read = sceHttpReadData(req, buf, sizeof(buf));
        if (read < 0) { job.errorMsg = diagnostic("HTTP read download", read); break; }
        if (read == 0) break;

        const uint64_t limit = expected ? expected : contentLen;
        if (limit && job.bytesDown + static_cast<uint64_t>(read) > limit) {
            job.errorMsg = diagnostic("Download exceeded expected size", EFBIG);
            break;
        }
        if (!out.write(buf, read)) { job.errorMsg = diagnostic("Write download temporary file", errno); break; }
        job.bytesDown += read;

        uint64_t now  = sceKernelGetProcessTime();
        double   dt   = (double)(now - t0) / 1000000.0;  // us → s
        if (dt > 0.5) {
            job.speedBps = (float)((job.bytesDown - prev) / dt);
            prev = job.bytesDown;
            t0   = now;
        }
    }

    out.close();
    const bool complete = read == 0 && out && running_ && job.bytesDown > 0 &&
        (!contentLen || job.bytesDown == contentLen) && (!expected || job.bytesDown == expected);
    const bool package = job.destPath.size() > 4 &&
        job.destPath.compare(job.destPath.size() - 4, 4, ".pkg") == 0;
    if (complete && (!package || isCompletePackage(partial, job.bytesDown))) return true;
    if (job.errorMsg.empty()) job.errorMsg = diagnostic("Incomplete or invalid download", EINVAL);
    remove(partial.c_str());
    return false;
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
            for (auto& job : queue_) {
                if (job.state == InstallState::Queued && !job.paused) {
                    job.bytesDown = 0;
                    job.errorMsg.clear();
                    job.state = InstallState::Downloading;
                    active = job;
                    break;
                }
            }
            if (active.id.empty() && !requests_.empty()) {
                request = std::move(requests_.front());
                requests_.pop_front();
            }
        }

        if (request) {
            try {
                request();
            } catch (const std::exception&) {
                diagnostic("Network request worker failed", EIO);
            }
        } else if (!active.id.empty()) {
            bool ok = false;
            try {
                ok = httpDownload(active);
            } catch (const std::exception&) {
                active.errorMsg = diagnostic("Download worker failed", EIO);
            }
            const auto partial = active.destPath + ".part";
            bool published = false;
            {
                std::lock_guard<std::mutex> lock(qMu_);
                for (auto& job : queue_) {
                    if (job.generation == active.generation && job.state == InstallState::Downloading) {
                        if (ok && !job.paused && running_) {
                            published = rename(partial.c_str(), job.destPath.c_str()) == 0;
                            if (!published) active.errorMsg = "Cannot publish download";
                        }
                        job.errorMsg = job.paused ? "Paused. Resume restarts the transfer." : active.errorMsg;
                        job.speedBps = 0;
                        job.state = published ? InstallState::Done : InstallState::Error;
                        break;
                    }
                }
            }
            if (!published) remove(partial.c_str());
            else diagnostic(("Download complete " + active.id).c_str(), 0);
        } else {
            sceKernelUsleep(50000);
        }
    }
}

bool Fetcher::publishProgress(const DownloadJob& active) {
    std::lock_guard<std::mutex> lock(qMu_);
    for (auto& job : queue_) {
        if (job.generation == active.generation && job.state == InstallState::Downloading) {
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
        if (httpGet(url, body, 16 * 1024 * 1024)) {
            std::lock_guard<std::mutex> lock(qMu_);
            manifestCache_ = std::move(body);
        }
    });
}

std::string Fetcher::takeManifest() {
    std::lock_guard<std::mutex> lock(qMu_);
    std::string result;
    result.swap(manifestCache_);
    return result;
}

std::shared_ptr<SDL_Surface> Fetcher::thumbnail(const std::string& url, const std::string& cacheKey) {
    if (url.empty() || !running_) return {};
    const std::string bundled = "/app0/assets/images/" + cacheKey + ".png";
    const bool local = url == bundled;
    if (!local && !cacheWritable_) return {};
    const auto key = cacheKey + "\n" + url;
    std::lock_guard<std::mutex> lock(qMu_);
    const auto found = thumbnails_.find(key);
    if (found != thumbnails_.end()) return found->second;
    if (requests_.size() >= 24) return {};
    if (thumbnailOrder_.size() >= 48) {
        thumbnails_.erase(thumbnailOrder_.front());
        thumbnailOrder_.pop_front();
    }
    thumbnailOrder_.push_back(key);
    thumbnails_.emplace(key, nullptr);
    requests_.push_back([this, url, cacheKey, key, local] {
        std::shared_ptr<SDL_Surface> image;
        if (local) {
            image.reset(IMG_Load(url.c_str()), SDL_FreeSurface);
        } else {
            // The URL suffix differentiates a refreshed catalog image from older cache entries.
            uint32_t hash = 2166136261u;
            for (unsigned char c : url) hash = (hash ^ c) * 16777619u;
            const auto path = std::string(THUMB_DIR) + cacheKey + "-" + std::to_string(hash) + ".img";
            struct stat info{};
            if (stat(path.c_str(), &info) == 0 && info.st_size > 0 && info.st_size <= 2 * 1024 * 1024)
                image.reset(IMG_Load(path.c_str()), SDL_FreeSurface);
            if (!image) {
                std::string body;
                if (!httpGet(url, body, 2 * 1024 * 1024)) return;
                const bool jpeg = body.compare(0, 3, "\xff\xd8\xff") == 0;
                const bool png = body.compare(0, 8, "\x89PNG\r\n\x1a\n") == 0;
                if (!jpeg && !png) {
                    diagnostic("Thumbnail is not JPEG or PNG", EINVAL);
                    return;
                }
                SDL_RWops* source = SDL_RWFromConstMem(body.data(), static_cast<int>(body.size()));
                if (source) image.reset(IMG_Load_RW(source, 1), SDL_FreeSurface);
                if (image) {
                    const auto partial = path + ".part";
                    std::ofstream file(partial, std::ios::binary);
                    file.write(body.data(), body.size());
                    file.close();
                    if (!file || rename(partial.c_str(), path.c_str()) != 0) {
                        diagnostic("Write thumbnail cache", errno);
                        remove(partial.c_str());
                    }
                }
            }
        }
        if (!image || image->w <= 0 || image->h <= 0) {
            diagnostic(IMG_GetError(), EINVAL);
            return;
        }
        const auto longest = std::max(image->w, image->h);
        if (longest > 384) {
            const int width = std::max(1, int(int64_t(image->w) * 384 / longest));
            const int height = std::max(1, int(int64_t(image->h) * 384 / longest));
            std::shared_ptr<SDL_Surface> resized(
                SDL_CreateRGBSurfaceWithFormat(0, width, height, 32, SDL_PIXELFORMAT_RGBA32), SDL_FreeSurface);
            SDL_SetSurfaceBlendMode(image.get(), SDL_BLENDMODE_NONE);
            if (!resized || SDL_BlitScaled(image.get(), nullptr, resized.get(), nullptr) < 0) {
                diagnostic(SDL_GetError(), EINVAL);
                return;
            }
            image = std::move(resized);
        }
        std::lock_guard<std::mutex> guard(qMu_);
        const auto slot = thumbnails_.find(key);
        if (slot != thumbnails_.end()) slot->second = std::move(image);
    });
    return {};
}

void Fetcher::enqueue(DownloadJob job) {
    std::lock_guard<std::mutex> lk(qMu_);
    job.generation = ++generation_;
    if (!running_) {
        job.state = InstallState::Error;
        job.errorMsg = "Network worker unavailable";
    }
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
