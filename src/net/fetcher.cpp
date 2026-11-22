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
#include "../ui/image.h"
#include "url.h"
#include "transfer.h"
#include <sys/statvfs.h>
#include <sys/stat.h>

#include <cstring>
#include <cctype>
#include <string_view>
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
    cacheWritable_ = writableDirectory(CACHE_DIR) && writableDirectory(THUMB_DIR);
    try {
        previewWorker_ = std::thread([this] { localPreviewLoop(); });
    } catch (const std::system_error& error) {
        diagnostic("preview", "Start local preview worker", ErrorDomain::Posix, error.code().value());
    }
    try {
        worker_ = std::thread([this] {
            initNet();
            workerLoop();
            freeNet();
        });
    } catch (const std::system_error& error) {
        diagnostic("network", "Start worker", ErrorDomain::Posix, error.code().value());
        running_ = false;
    }
}

Fetcher::~Fetcher() {
    {
        std::lock_guard<std::mutex> lock(qMu_);
        running_ = false;
    }
    previewReady_.notify_one();
    if (previewWorker_.joinable()) previewWorker_.join();
    if (worker_.joinable()) worker_.join();
}

void Fetcher::initNet() {
    diagnostic("network", "Initialize HTTP services", ErrorDomain::Event);
    const auto check = [this](const char* stage, int status) {
        if (status < 0) networkError_ = diagnostic("network", stage, ErrorDomain::Orbis, status);
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
    if (!check("sceHttpSetAutoRedirect", sceHttpSetAutoRedirect(httpTemplate_, 0))) {
        sceHttpDeleteTemplate(httpTemplate_);
        httpTemplate_ = -1;
        return;
    }
    check("sceHttpSetResolveTimeOut", sceHttpSetResolveTimeOut(httpTemplate_, 5000000));
    check("sceHttpSetConnectTimeOut", sceHttpSetConnectTimeOut(httpTemplate_, 5000000));
    check("sceHttpSetSendTimeOut", sceHttpSetSendTimeOut(httpTemplate_, 5000000));
    check("sceHttpSetRecvTimeOut", sceHttpSetRecvTimeOut(httpTemplate_, 8000000));
    networkError_.clear();
    diagnostic("network", "HTTP initialized", ErrorDomain::Event);
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
    int response = 0;
    ~HttpRequest() { reset(); }

    void reset() {
        if (request >= 0) sceHttpDeleteRequest(request);
        if (connection >= 0) sceHttpDeleteConnection(connection);
        request = connection = -1;
    }

    std::string responseHeader(const char* requested) const {
        char* headers = nullptr;
        size_t size = 0;
        if (sceHttpGetAllResponseHeaders(request, &headers, &size) < 0 ||
            !headers || size > 64 * 1024) return {};
        std::string_view lines(headers, size);
        while (!lines.empty()) {
            const auto end = lines.find('\n');
            auto line = lines.substr(0, end);
            if (end == std::string_view::npos) lines = {};
            else lines.remove_prefix(end + 1);
            const auto colon = line.find(':');
            if (colon == std::string_view::npos || colon > 64) continue;
            std::string name(line.substr(0, colon));
            for (char& c : name) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
            if (name != requested) continue;
            line.remove_prefix(colon + 1);
            while (!line.empty() && (line.front() == ' ' || line.front() == '\t')) line.remove_prefix(1);
            while (!line.empty() && (line.back() == ' ' || line.back() == '\r' ||
                                      line.back() == '\t' || line.back() == '\0')) line.remove_suffix(1);
            return std::string(line);
        }
        return {};
    }

    bool open(int httpTemplate, std::string url, std::string& error, const char* item) {
        if (!validHttpsUrl(url)) {
            error = diagnostic("network", "Invalid HTTPS URL", ErrorDomain::Application, 0, item);
            return false;
        }
        for (int redirect = 0; redirect <= 5; ++redirect) {
            response = 0;
            connection = sceHttpCreateConnectionWithURL(httpTemplate, url.c_str(), false);
            if (connection < 0) {
                error = diagnostic("network", "HTTP create connection", ErrorDomain::Orbis, connection, item);
                return false;
            }
            request = sceHttpCreateRequestWithURL(connection, ORBIS_METHOD_GET, url.c_str(), 0);
            if (request < 0) {
                error = diagnostic("network", "HTTP create request", ErrorDomain::Orbis, request, item);
                return false;
            }
            diagnostic("network", "HTTP send request", ErrorDomain::Event, 0, item);
            int status = sceHttpSendRequest(request, nullptr, 0);
            if (status < 0) {
                error = diagnostic("network", "HTTP send / TLS", ErrorDomain::Orbis, status, item);
                return false;
            }
            status = sceHttpGetStatusCode(request, &response);
            if (status < 0) {
                error = diagnostic("network", "HTTP read status", ErrorDomain::Orbis, status, item);
                return false;
            }
            if (response == 200) return true;
            if (response != 301 && response != 302 && response != 303 && response != 307 && response != 308) {
                error = "HTTP " + std::to_string(response);
                diagnostic("network", "HTTP response", ErrorDomain::Http, response, item);
                return false;
            }
            url = httpsRedirect(url, responseHeader("location"));
            diagnostic("network", "HTTP redirect", ErrorDomain::Http, response, item);
            if (url.empty()) {
                error = diagnostic("network", "Invalid HTTPS redirect", ErrorDomain::Application, 0, item);
                return false;
            }
            reset();
        }
        error = diagnostic("network", "HTTP redirect limit reached", ErrorDomain::Application, 0, item);
        return false;
    }
};
}

bool Fetcher::httpGet(const std::string& url, std::string& body, size_t limit,
                      const char* item, std::string& error) {
    if (httpTemplate_ < 0) {
        error = networkError_.empty() ? "Network unavailable" : networkError_;
        return false;
    }
    HttpRequest request;
    if (!request.open(httpTemplate_, url, error, item)) return false;
    char buffer[HTTP_BUF];
    body.clear();
    while (running_) {
        const int received = sceHttpReadData(request.request, buffer, sizeof(buffer));
        if (received == 0) {
            if (!body.empty()) return true;
            error = diagnostic("network", "Empty HTTP response", ErrorDomain::Application, 0, item);
            return false;
        }
        if (received < 0 || body.size() + static_cast<size_t>(received) > limit) {
            error = diagnostic("network", "Read response", received < 0 ? ErrorDomain::Orbis : ErrorDomain::Application,
                               received < 0 ? received : EFBIG, item);
            return false;
        }
        body.append(buffer, received);
    }
    return false;
}

bool Fetcher::httpDownload(DownloadJob& job, std::string& partial) {
    const auto context = job.id + " path=" + job.destPath;
    const auto source = diagnosticOrigin(job.url) + "/[path redacted]";
    if (!validHttpsUrl(job.url)) {
        job.errorMsg = "Download unavailable in this catalog";
        return false;
    }
    if (!writableDirectory(DOWNLOAD_DIR, &job.errorMsg)) {
        diagnostic("download", "Destination unavailable", ErrorDomain::Application, 0, job.id.c_str());
        return false;
    }
    std::string temporary = job.destPath + ".part-XXXXXX";
    const int fd = mkstemp(temporary.data());
    if (fd < 0) {
        job.errorMsg = diagnostic("download", "Create temporary file", ErrorDomain::Posix, errno, context.c_str());
        return false;
    }
    partial = std::move(temporary);
    FILE* file = fdopen(fd, "wb");
    if (!file) {
        const int code = errno;
        close(fd);
        job.errorMsg = diagnostic("download", "Open temporary stream", ErrorDomain::Posix, code, context.c_str());
        return false;
    }
    std::unique_ptr<FILE, decltype(&fclose)> out(file, fclose);
    diagnostic("download", "Destination ready", ErrorDomain::Event, 0, context.c_str());
    if (httpTemplate_ < 0) {
        job.errorMsg = networkError_.empty() ? "Network unavailable" : networkError_;
        return false;
    }
    HttpRequest request;
    if (!request.open(httpTemplate_, job.url, job.errorMsg, job.id.c_str())) {
        if (request.response) diagnostic("download", "Unexpected HTTP response", ErrorDomain::Http,
                                         request.response, (context + " source=" + source).c_str());
        return false;
    }
    const int req = request.request;
    const auto expected = job.catalogBytes;
    size_t contentLen = 0;
    int lengthType = ORBIS_HTTP_CONTENTLEN_NOT_FOUND;
    if (sceHttpGetResponseContentLength(req, &lengthType, &contentLen) >= 0 &&
        lengthType == ORBIS_HTTP_CONTENTLEN_EXIST && contentLen > 0)
        job.totalBytes = contentLen;
    else
        contentLen = 0;

    std::string contentType = request.responseHeader("content-type");
    contentType = contentType.substr(0, contentType.find_first_of("; \t"));
    for (char& c : contentType) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    if (contentType.size() > 64 || contentType.find_first_not_of(
        "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789!#$&^_.+-/") != std::string::npos)
        contentType.clear();
    const auto httpContext = job.id + " source=" + source + " http=" + std::to_string(request.response) +
        " content_type=" + (contentType.empty() ? "unknown" : contentType) +
        " content_length=" + (contentLen ? std::to_string(contentLen) : "unknown") +
        " expected=" + std::to_string(expected);
    diagnostic("download", "Response headers", ErrorDomain::Event, 0, httpContext.c_str());
    const bool package = job.destPath.size() > 4 &&
        job.destPath.compare(job.destPath.size() - 4, 4, ".pkg") == 0;
    if (package && (contentType == "text/html" || contentType == "application/json")) {
        job.errorMsg = diagnostic("download", "Unexpected content type", ErrorDomain::Event, 0,
                                  httpContext.c_str());
        return false;
    }
    if (expected && contentLen && expected != contentLen) {
        job.errorMsg = diagnostic("download", "HTTP length differs from catalog", ErrorDomain::Event, 0, httpContext.c_str());
        return false;
    }
    const uint64_t limit = expected ? expected : contentLen;
    struct statvfs space{};
    if (limit && statvfs(DOWNLOAD_DIR, &space) == 0 &&
        !fitsStorage(limit, space.f_frsize, space.f_bavail)) {
        job.errorMsg = diagnostic("download", "Insufficient free space", ErrorDomain::Posix, ENOSPC, context.c_str());
        return false;
    }

    job.state = DownloadState::Downloading;
    char buf[HTTP_BUF];
    int read = -1;
    uint64_t prev  = 0;
    uint64_t t0    = sceKernelGetProcessTime();

    while (running_ && publishProgress(job)) {
        read = sceHttpReadData(req, buf, sizeof(buf));
        if (read < 0) { job.errorMsg = diagnostic("download", "Read HTTP body", ErrorDomain::Orbis, read, context.c_str()); break; }
        if (read == 0) break;

        if (limit && job.bytesDown + static_cast<uint64_t>(read) > limit) {
            job.errorMsg = diagnostic("download", "Response exceeds expected size", ErrorDomain::Application, EFBIG, context.c_str());
            break;
        }
        if (const auto failure = writeStorage(out.get(), buf, read)) {
            job.errorMsg = diagnostic("download", "Write temporary file", ErrorDomain::Posix, failure.code, context.c_str());
            break;
        }
        job.bytesDown += read;

        uint64_t now  = sceKernelGetProcessTime();
        double   dt   = (double)(now - t0) / 1000000.0;  // us → s
        if (dt > 0.5) {
            job.speedBps = (float)((job.bytesDown - prev) / dt);
            prev = job.bytesDown;
            t0   = now;
        }
    }

    if (fclose(out.release()) != 0 && job.errorMsg.empty())
        job.errorMsg = diagnostic("download", "Close temporary file", ErrorDomain::Posix, errno, context.c_str());
    const bool complete = job.errorMsg.empty() && transferComplete(
        read == 0, !running_ || !publishProgress(job), job.bytesDown, expected, contentLen);
    if (complete) {
        job.state = DownloadState::Validating;
        if (!publishProgress(job)) return false;
        if (!package) return true;
        const auto result = inspectPackageDetails(partial, job.bytesDown);
        const bool supported = result && matchesPackageRole(result.info, job.role);
        if (supported) return true;
        job.rejected = true;
        const auto details = context + " received=" + std::to_string(job.bytesDown) +
            " " + describePackage(result, job.bytesDown);
        job.errorMsg = diagnostic("download", result ? "Unsupported package type" : result.reason,
            result.ioError ? ErrorDomain::Posix : ErrorDomain::Event, result.ioError, details.c_str());
    } else if (job.errorMsg.empty() && running_ && publishProgress(job)) {
        job.errorMsg = diagnostic("download", "Incomplete transfer", ErrorDomain::Event, 0,
            (context + " received=" + std::to_string(job.bytesDown) + " expected=" +
             std::to_string(expected) + " http_length=" + std::to_string(contentLen)).c_str());
    }
    return false;
}

// ─────────────────────────────────────────────
//  Worker loop
// ─────────────────────────────────────────────

void Fetcher::workerLoop() {
    const auto removed = cleanupInterruptedDownloads(DOWNLOAD_DIR);
    if (removed) diagnostic("download", "Cleaned interrupted transfers", ErrorDomain::Event, 0,
                            ("count=" + std::to_string(removed)).c_str());
    rejectedBytes_ = rejected_.size();
    while (running_) {
        if (clearRejected_.exchange(false)) {
            if (!rejected_.clear())
                diagnostic("download", "Delete rejected sample", ErrorDomain::Posix, errno, REJECTED_DOWNLOAD);
            rejectedBytes_ = rejected_.size();
        }
        std::function<void()> request;
        DownloadJob active;
        {
            std::lock_guard<std::mutex> lock(qMu_);
            active = beginNextDownload(queue_);
            if (active.id.empty() && !requests_.empty()) {
                request = std::move(requests_.front());
                requests_.pop_front();
            }
        }

        if (request) {
            try {
                request();
            } catch (const std::exception&) {
                diagnostic("network", "Request worker failed", ErrorDomain::Application, EIO);
            }
        } else if (!active.id.empty()) {
            bool ok = false;
            std::string partial;
            try {
                ok = httpDownload(active, partial);
            } catch (const std::exception&) {
                active.errorMsg = diagnostic("download", "Worker failed", ErrorDomain::Application, EIO, active.id.c_str());
            }
            int publishError = 0;
            bool published = false;
            bool retainRejected = false;
            {
                std::lock_guard<std::mutex> lock(qMu_);
                for (auto& job : queue_) {
                    if (job.generation == active.generation && activeDownload(job.state)) {
                        if (ok && !job.paused && running_) {
                            published = rename(partial.c_str(), job.destPath.c_str()) == 0;
                            if (!published) {
                                publishError = errno;
                                active.errorMsg = "Cannot publish download: " + std::string(strerror(publishError));
                            }
                        }
                        job.errorMsg = job.paused ? "Paused. Resume restarts the transfer." : active.errorMsg;
                        job.totalBytes = published && !active.totalBytes ? active.bytesDown : active.totalBytes;
                        job.bytesDown = active.bytesDown;
                        job.speedBps = 0;
                        job.state = published ? DownloadState::Completed : DownloadState::Failed;
                        retainRejected = active.rejected && !job.paused && running_ && keepRejected_;
                        break;
                    }
                }
            }
            if (!published && !partial.empty()) {
                if (retainRejected && rejected_.keep(partial)) {
                    rejectedBytes_ = rejected_.size();
                    diagnostic("download", "Preserved rejected sample", ErrorDomain::Event, 0, REJECTED_DOWNLOAD);
                } else {
                    remove(partial.c_str());
                }
            }
            if (publishError) diagnostic("download", "Publish package", ErrorDomain::Posix,
                                         publishError, active.destPath.c_str());
            if (published) diagnostic("download", "Download complete", ErrorDomain::Event, 0, active.id.c_str());
        } else {
            sceKernelUsleep(50000);
        }
    }
}

bool Fetcher::publishProgress(const DownloadJob& active) {
    std::lock_guard<std::mutex> lock(qMu_);
    for (auto& job : queue_) {
        if (job.generation == active.generation && activeDownload(job.state)) {
            job.state = active.state;
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
    if (!running_) {
        manifestStatus_ = "Network worker unavailable. Using local catalog.";
        return;
    }
    manifestStatus_ = "Checking remote catalog";
    requests_.push_back([this, url] {
        std::string body, error;
        std::unique_ptr<Database> catalog;
        if (httpGet(url, body, 16 * 1024 * 1024, "catalog", error)) {
            catalog = std::make_unique<Database>();
            if (!catalog->importManifest(body)) {
                error = diagnostic("catalog", "Invalid remote manifest", ErrorDomain::Application);
                catalog.reset();
            } else if (writableDirectory(DATA_DIR)) {
                const char* temporary = DATA_DIR "manifest-cache.json.tmp";
                std::ofstream cache(temporary, std::ios::binary);
                cache.write(body.data(), body.size());
                cache.close();
                if (!cache || rename(temporary, DATA_DIR "manifest-cache.json") != 0) {
                    const int code = errno;
                    remove(temporary);
                    diagnostic("catalog", "Publish validated manifest cache", ErrorDomain::Posix, code);
                }
            }
        }
        std::lock_guard<std::mutex> lock(qMu_);
        manifestStatus_ = catalog ? "Remote catalog validated" : error + ". Using local catalog.";
        manifestCache_ = std::move(catalog);
    });
}

std::unique_ptr<Database> Fetcher::takeManifest() {
    std::lock_guard<std::mutex> lock(qMu_);
    return std::move(manifestCache_);
}

std::string Fetcher::manifestStatus() const {
    std::lock_guard<std::mutex> lock(qMu_);
    return manifestStatus_;
}

void Fetcher::localPreviewLoop() {
    while (running_) {
        std::function<void()> load;
        {
            std::unique_lock<std::mutex> lock(qMu_);
            previewReady_.wait(lock, [this] { return !running_ || !localPreviews_.empty(); });
            if (!running_) break;
            load = std::move(localPreviews_.front());
            localPreviews_.pop_front();
        }
        try {
            load();
        } catch (const std::exception&) {
            diagnostic("preview", "Local preview worker failed", ErrorDomain::Event);
        }
    }
}

namespace {
size_t surfaceBytes(const Preview& preview) {
    return preview.image ? size_t(preview.image->pitch) * preview.image->h : 0;
}
}

bool Fetcher::evictThumbnail() {
    for (auto oldest = thumbnailOrder_.begin(); oldest != thumbnailOrder_.end(); ++oldest) {
        const auto found = thumbnails_.find(*oldest);
        if (found->second.state == PreviewState::Loading) continue;
        thumbnailBytes_ -= surfaceBytes(found->second);
        thumbnails_.erase(found);
        thumbnailOrder_.erase(oldest);
        return true;
    }
    return false;
}

Preview Fetcher::thumbnail(const std::string& url, const std::string& cacheKey) {
    if (url.empty()) return {};
    const std::string bundled = "/app0/assets/images/" + cacheKey + ".png";
    const bool local = url == bundled;
    if (!running_ || (!local && !validHttpsUrl(url))) return {{}, PreviewState::Failed};
    const auto key = cacheKey + "\n" + url;
    std::lock_guard<std::mutex> lock(qMu_);
    const auto found = thumbnails_.find(key);
    if (found != thumbnails_.end()) return found->second;
    if (local && !previewWorker_.joinable()) return {{}, PreviewState::Failed};
    auto& pending = local ? localPreviews_ : requests_;
    if (pending.size() >= 24) return {{}, PreviewState::Loading};
    if (thumbnailOrder_.size() >= 48 && !evictThumbnail()) return {{}, PreviewState::Loading};
    thumbnailOrder_.push_back(key);
    thumbnails_.emplace(key, Preview{{}, PreviewState::Loading});
    pending.push_back([this, url, cacheKey, key, local] {
        const Preview failed{{}, PreviewState::Failed};
        const auto load = [&]() -> Preview {
            const auto logError = [&](const PreviewError& error, const std::string& path) {
                const auto context = cacheKey + " path=" + path + " " + error.detail;
                diagnostic("preview", error.stage ? error.stage : "Unknown image failure",
                           error.ioError ? ErrorDomain::Posix : ErrorDomain::Event,
                           error.ioError, context.c_str());
            };
            PreviewError imageError;
            std::shared_ptr<SDL_Surface> image;
            const char* source = local ? "bundled" : "network";
            if (local) {
                diagnostic("preview", "Open packaged illustration", ErrorDomain::Event, 0, cacheKey.c_str());
                image.reset(loadPreviewFile(url, &imageError), SDL_FreeSurface);
                if (!image) logError(imageError, url);
            } else {
                uint32_t hash = 2166136261u;
                for (unsigned char c : url) hash = (hash ^ c) * 16777619u;
                const auto path = std::string(THUMB_DIR) + cacheKey + "-" + std::to_string(hash) + ".img";
                if (cacheWritable_) {
                    image.reset(loadPreviewFile(path, &imageError), SDL_FreeSurface);
                    if (image) source = "disk-cache";
                    else if (imageError.ioError != ENOENT) logError(imageError, path);
                }
                if (!image) {
                    std::string body, error;
                    if (!httpGet(url, body, 2 * 1024 * 1024, cacheKey.c_str(), error)) {
                        diagnostic("preview", "Fetch remote image", ErrorDomain::Event, 0,
                            (cacheKey + " source=" + diagnosticOrigin(url) + " " + error).c_str());
                        return failed;
                    }
                    image.reset(decodePreview(body, &imageError), SDL_FreeSurface);
                    if (!image) logError(imageError, diagnosticOrigin(url));
                    if (image && cacheWritable_) {
                        const auto partial = path + ".part";
                        std::ofstream file(partial, std::ios::binary);
                        file.write(body.data(), body.size());
                        file.close();
                        if (!file || rename(partial.c_str(), path.c_str()) != 0) {
                            const int code = errno;
                            remove(partial.c_str());
                            diagnostic("preview", "Write cache", ErrorDomain::Posix, code, path.c_str());
                        }
                    }
                }
            }
            if (image) {
                const auto dimensions = [](const SDL_Surface& surface) {
                    return std::to_string(surface.w) + "x" + std::to_string(surface.h);
                };
                const auto decoded = cacheKey + " source=" + source + " original=" + dimensions(*image);
                diagnostic("preview", "Image decoded to RGBA surface", ErrorDomain::Event, 0, decoded.c_str());
                if (std::max(image->w, image->h) > previewMaxEdge) {
                    image.reset(downsamplePreview(*image, &imageError), SDL_FreeSurface);
                    if (!image) {
                        logError(imageError, source);
                        return failed;
                    }
                }
                const auto prepared = decoded + " cached=" + dimensions(*image);
                diagnostic("preview", "Preview surface available", ErrorDomain::Event, 0, prepared.c_str());
                return {std::move(image), local ? PreviewState::Illustration : PreviewState::Ready};
            }
            return failed;
        };
        auto result = failed;
        try {
            result = load();
        } catch (const std::exception& error) {
            diagnostic("preview", "Prepare preview failed", ErrorDomain::Application, 0, error.what());
        }
        std::lock_guard<std::mutex> guard(qMu_);
        constexpr size_t maxSurfaceBytes = 24 * 1024 * 1024;
        const auto bytes = surfaceBytes(result);
        while (thumbnailBytes_ + bytes > maxSurfaceBytes && evictThumbnail()) {}
        thumbnailBytes_ += bytes;
        thumbnails_.at(key) = std::move(result);
    });
    if (local) previewReady_.notify_one();
    return {{}, PreviewState::Loading};
}

void Fetcher::enqueue(DownloadJob job) {
    std::lock_guard<std::mutex> lk(qMu_);
    job.generation = ++generation_;
    job.catalogBytes = job.totalBytes;
    if (!running_) {
        job.state = DownloadState::Failed;
        job.errorMsg = "Network worker unavailable";
    }
    // Avoid duplicate
    for (auto& existing : queue_) {
        if (existing.id == job.id && existing.url == job.url) {
            if (existing.state == DownloadState::Failed || existing.state == DownloadState::Completed ||
                existing.state == DownloadState::Cancelled)
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
            if (j.state == DownloadState::Failed) j.state = DownloadState::Queued;
        }
    }
}

void Fetcher::cancel(const std::string& id) {
    std::lock_guard<std::mutex> lock(qMu_);
    cancelOrDismiss(queue_, id);
}

void Fetcher::retry(const std::string& id) {
    std::lock_guard<std::mutex> lk(qMu_);
    for (auto& j : queue_) {
        if (j.id == id && (j.state == DownloadState::Failed || j.state == DownloadState::Cancelled)) {
            j.state = DownloadState::Queued;
            j.paused = false;
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
        if (activeDownload(j.state)) return true;
    return false;
}

int Fetcher::activeCount() const {
    std::lock_guard<std::mutex> lk(qMu_);
    int n = 0;
    for (auto& j : queue_)
        if (activeDownload(j.state) || j.state == DownloadState::Queued) ++n;
    return n;
}
