#pragma once
#include "../nebula.h"
#include "../db/database.h"
#include "../net/fetcher.h"
#include "../input/controller.h"
#include "../install/installer.h"
#include "renderer.h"
#include <vector>
#include <memory>
#include <string>
#include <functional>

// ─────────────────────────────────────────────
//  Base screen
// ─────────────────────────────────────────────

struct ScreenCtx {
    Renderer&   gfx;
    Database&   db;
    Fetcher&    net;
    Controller& ctrl;
    Installer&  installer;
    std::function<void(ScreenID, void*)> navigate;
    bool remoteCatalog;
    std::string catalogConfigError;
};

class Screen {
public:
    virtual ~Screen() = default;
    virtual void update(ScreenCtx& ctx) = 0;
    virtual void render(ScreenCtx& ctx) = 0;
};

// ─────────────────────────────────────────────
//  Screen manager
// ─────────────────────────────────────────────

class ScreenManager {
public:
    ScreenManager(Renderer& gfx, Database& db, Fetcher& net,
                  Controller& ctrl, Installer& installer, bool remoteCatalog, std::string catalogConfigError);

    void push(ScreenID id, void* param = nullptr);
    void pop();
    size_t depth() const { return stack_.size(); }

    void update();
    void render();

private:
    ScreenCtx ctx_;
    std::vector<std::unique_ptr<Screen>> stack_;

    std::unique_ptr<Screen> makeScreen(ScreenID id, void* param);
};

// ─────────────────────────────────────────────
//  Individual screens — declarations
// ─────────────────────────────────────────────

class HomeScreen    : public Screen {
public:
    HomeScreen();
    void update(ScreenCtx& ctx) override;
    void render(ScreenCtx& ctx) override;
private:
    int   selectedIdx_ = 0;
    float bgPulse_     = 0.f;
    // Nav tiles: label, screen to push
    struct Tile { std::string label; std::string sub; ScreenID target; Color accent; };
    std::vector<Tile> tiles_;
};

class LibraryScreen : public Screen {
public:
    explicit LibraryScreen(ContentType type);
    void update(ScreenCtx& ctx) override;
    void render(ScreenCtx& ctx) override;
private:
    ContentType type_;
    int   colIdx_   = 0;    // carousel column
    int   rowIdx_   = 0;
    float scrollX_  = 0.f;
    float scrollY_  = 0.f;
    std::string selCategory_;
    std::vector<ContentEntry*> entries_;
    std::vector<std::string> categories_;
    bool loaded_ = false;

    void refreshEntries(ScreenCtx& ctx);
    void drawCard(ScreenCtx& ctx, ContentEntry* e, int x, int y, bool focused);
};

class SearchScreen : public Screen {
public:
    SearchScreen();
    void update(ScreenCtx& ctx) override;
    void render(ScreenCtx& ctx) override;
private:
    size_t searchChar_ = 0;
    std::string query_;
    int         filterMode_ = 0;  // 0=all 1=avatars 2=themes 3=animated
    std::vector<ContentEntry*> results_;
    int   selIdx_ = 0;
    float scroll_ = 0.f;

    void doSearch(ScreenCtx& ctx);
    void drawResultCard(ScreenCtx& ctx, ContentEntry* e, int y, bool focused);
};

class PreviewScreen : public Screen {
public:
    explicit PreviewScreen(ContentEntry* entry);
    void update(ScreenCtx& ctx) override;
    void render(ScreenCtx& ctx) override;
private:
    ContentEntry*  entry_;
    int            btnIdx_    = 0;  // 0=download, 1=install, 2=favorite
    std::string    statusMsg_;
    bool           downloading_ = false;
    bool           clearInstallProgress_ = false;
};

class DownloadsScreen : public Screen {
public:
    void update(ScreenCtx& ctx) override;
    void render(ScreenCtx& ctx) override;
private:
    int   selIdx_ = 0;
};

class FavoritesScreen : public Screen {
public:
    void update(ScreenCtx& ctx) override;
    void render(ScreenCtx& ctx) override;
private:
    int   selIdx_ = 0;
    std::vector<ContentEntry*> favs_;
    uint64_t revision_ = UINT64_MAX;
};

class SettingsScreen : public Screen {
public:
    void update(ScreenCtx& ctx) override;
    void render(ScreenCtx& ctx) override;
private:
    int selIdx_ = 0;
    struct Setting { std::string label; std::string value; };
    std::vector<Setting> settings_;
};
