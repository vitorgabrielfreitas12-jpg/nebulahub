// NEBULA HUB — Screen implementations

#include "screens.h"
#include <cmath>
#include <cstdio>
#include <algorithm>
#include <sys/stat.h>
#include "../net/url.h"

namespace {
void drawPreview(ScreenCtx& ctx, const ContentEntry& entry, int x, int y, int w, int h) {
    const auto preview = ctx.net.thumbnail(entry.previewUrl, entry.id);
    auto& gfx = ctx.gfx;
    auto* texture = gfx.loadTexture(entry.id + entry.previewUrl, preview.image.get());
    const char* caption = nullptr;
    if (texture) {
        gfx.drawTexture(texture, x, y, w, h);
        if (preview.state == PreviewState::Illustration) caption = "Ilustração de categoria";
    } else {
        const bool avatar = entry.type == ContentType::Avatar;
        const auto accent = fromHex(avatar ? 0xBCA1EBFF : 0xA9C8EFFF);
        gfx.fillRoundRect(x, y, w, h, 10, fromHex(avatar ? 0x281849FF : 0x172A4BFF));
        const int icon = std::min(w, h) / 3;
        const int cx = x + w / 2;
        const int cy = y + h / 2 - 6;
        if (avatar) {
            gfx.fillRoundRect(cx - icon / 5, cy - icon / 2, icon * 2 / 5, icon * 2 / 5, icon / 5, accent);
            gfx.fillRoundRect(cx - icon / 2, cy, icon, icon / 3, icon / 6, accent);
        } else {
            gfx.drawRect(cx - icon / 2, cy - icon / 2, icon, icon * 3 / 4, accent, 2);
            gfx.drawLine(cx - icon / 2, cy + icon / 8, cx - icon / 6, cy - icon / 6, accent);
            gfx.drawLine(cx - icon / 6, cy - icon / 6, cx + icon / 5, cy + icon / 8, accent);
        }
        caption = preview.state == PreviewState::Missing ? "Sem prévia cadastrada" :
                  preview.state == PreviewState::Loading ? "Carregando prévia" : "Falha na prévia";
    }
    if (caption) {
        gfx.fillRect(x, y + h - 32, w, 32, fromHex(0x11101CDD));
        gfx.textCentered(caption, x, y + h - 32, w, 32, fromHex(COL_TEXT), 16);
    }
}
}

// ─────────────────────────────────────────────
//  ScreenManager
// ─────────────────────────────────────────────

ScreenManager::ScreenManager(Renderer& gfx, Database& db, Fetcher& net,
                              Controller& ctrl, Installer& installer, bool remoteCatalog,
                              std::string catalogConfigError)
    : ctx_{gfx, db, net, ctrl, installer, [this](ScreenID id, void* param) { push(id, param); },
           remoteCatalog, std::move(catalogConfigError)} {}

void ScreenManager::push(ScreenID id, void* param) {
    stack_.push_back(makeScreen(id, param));
}

void ScreenManager::pop() {
    if (!stack_.empty()) stack_.pop_back();
}

void ScreenManager::update() {
    if (!stack_.empty()) stack_.back()->update(ctx_);
}

void ScreenManager::render() {
    if (!stack_.empty()) stack_.back()->render(ctx_);
}

std::unique_ptr<Screen> ScreenManager::makeScreen(ScreenID id, void* param) {
    switch (id) {
        case ScreenID::Home:         return std::make_unique<HomeScreen>();
        case ScreenID::AvatarLibrary:return std::make_unique<LibraryScreen>(ContentType::Avatar);
        case ScreenID::ThemeLibrary: return std::make_unique<LibraryScreen>(ContentType::Theme);
        case ScreenID::Search:       return std::make_unique<SearchScreen>();
        case ScreenID::ItemPreview:  return std::make_unique<PreviewScreen>((ContentEntry*)param);
        case ScreenID::Downloads:    return std::make_unique<DownloadsScreen>();
        case ScreenID::Favorites:    return std::make_unique<FavoritesScreen>();
        case ScreenID::Settings:     return std::make_unique<SettingsScreen>();
        default:                     return std::make_unique<HomeScreen>();
    }
}

// ─────────────────────────────────────────────
//  HomeScreen
// ─────────────────────────────────────────────

HomeScreen::HomeScreen() {
    tiles_ = {
        { "AVATARS",    "Avatar packs",          ScreenID::AvatarLibrary, fromHex(0x7B2FFFFF) },
        { "THEMES",     "Static & animated themes",       ScreenID::ThemeLibrary,  fromHex(0xFF5DB1FF) },
        { "SEARCH",     "Find anything instantly",        ScreenID::Search,        fromHex(0x2BFFB1FF) },
        { "DOWNLOADS",  "Track downloads",                ScreenID::Downloads,     fromHex(0xFFAA00FF) },
        { "FAVORITES",  "Your saved collection",          ScreenID::Favorites,     fromHex(0x00D4FFFF) },
        { "SETTINGS",   "Configure NEBULA HUB",           ScreenID::Settings,      fromHex(0xAAAAAFFF) },
    };
}

void HomeScreen::update(ScreenCtx& ctx) {
    bgPulse_ += 0.015f;
    if (ctx.ctrl.pressed(ORBIS_PAD_BUTTON_RIGHT)) {
        selectedIdx_ = (selectedIdx_ + 1) % (int)tiles_.size();
    }
    if (ctx.ctrl.pressed(ORBIS_PAD_BUTTON_LEFT)) {
        selectedIdx_ = ((selectedIdx_ - 1) + (int)tiles_.size()) % (int)tiles_.size();
    }
    if (ctx.ctrl.pressed(ORBIS_PAD_BUTTON_DOWN)) {
        int next = selectedIdx_ + 3;
        if (next < (int)tiles_.size()) selectedIdx_ = next;
    }
    if (ctx.ctrl.pressed(ORBIS_PAD_BUTTON_UP)) {
        int prev = selectedIdx_ - 3;
        if (prev >= 0) selectedIdx_ = prev;
    }
    if (ctx.ctrl.pressed(ORBIS_PAD_BUTTON_CROSS)) {
        ctx.navigate(tiles_[selectedIdx_].target, nullptr);
    }
}

void HomeScreen::render(ScreenCtx& ctx) {
    auto& gfx = ctx.gfx;

    // ── Background gradient ──
    float pulse = 0.5f + 0.5f * std::sin(bgPulse_);
    uint8_t bgr = (uint8_t)(10 + pulse * 8);
    uint8_t bgg = (uint8_t)(10 + pulse * 4);
    uint8_t bgb = (uint8_t)(20 + pulse * 20);
    gfx.fillRect(0, 0, SCREEN_W, SCREEN_H, {bgr, bgg, bgb, 255});

    // ── Nebula particle overlay (decorative bands) ──
    for (int i = 0; i < 3; ++i) {
        float t = bgPulse_ + i * 2.09f;
        int bx = (int)(SCREEN_W * 0.1f + std::sin(t * 0.3f) * SCREEN_W * 0.6f);
        gfx.fillRect(bx, 0, 300, SCREEN_H,
                     {(uint8_t)(80 + i*20), 10, (uint8_t)(120 + i*30), 20}, 0.08f);
    }

    // ── Logo / title ──
    gfx.text("✦ NEBULA HUB", 80, 60, fromHex(COL_HIGHLIGHT), 54, true, true);
    gfx.text("YOUR ULTIMATE PS4 CUSTOMIZATION LIBRARY",
             80, 128, fromHex(COL_SUBTEXT), 20, false);

    // Stats
    char stats[128];
    snprintf(stats, sizeof(stats), "%zu AVATAR PACKS  •  %zu THEMES",
             ctx.db.totalAvatars(), ctx.db.totalThemes());
    auto sdim = gfx.measureText(stats, 18);
    gfx.text(stats, SCREEN_W - sdim.x - 80, 80, fromHex(COL_SUBTEXT), 18);

    // ── Navigation tiles (2 rows of 3) ──
    int tileW = 500, tileH = 160, gapX = 50, gapY = 30;
    int startX = (SCREEN_W - (3*tileW + 2*gapX)) / 2;
    int startY = 220;

    for (int i = 0; i < (int)tiles_.size(); ++i) {
        int row = i / 3;
        int col = i % 3;
        int tx = startX + col * (tileW + gapX);
        int ty = startY + row * (tileH + gapY);
        bool focused = (i == selectedIdx_);

        // Panel
        float alpha = focused ? 1.0f : 0.55f;
        Color panel = fromHex(COL_PANEL_ALT);
        gfx.fillRoundRect(tx, ty, tileW, tileH, 16, panel, alpha);

        // Accent bar on left
        gfx.fillRoundRect(tx, ty + 20, 5, tileH - 40, 2, tiles_[i].accent);

        // Focus ring
        if (focused) {
            gfx.drawRect(tx - 2, ty - 2, tileW + 4, tileH + 4,
                        tiles_[i].accent, 2);
            // Animated glow top bar
            gfx.gradientH(tx, ty, tileW, 3,
                          {tiles_[i].accent.r, tiles_[i].accent.g,
                           tiles_[i].accent.b, 255},
                          fromHex(0x00000000));
        }

        // Label
        gfx.text(tiles_[i].label, tx + 32, ty + 36, fromHex(COL_HIGHLIGHT), 28, true);
        gfx.text(tiles_[i].sub,   tx + 32, ty + 90, fromHex(COL_SUBTEXT),   18);
    }

    // ── Bottom hint bar ──
    int hy = SCREEN_H - 60;
    gfx.fillRect(0, hy, SCREEN_W, 60, fromHex(0x0A0A14E0), 0.9f);
    gfx.text("✕  Open    ○  Back    △  Favorites    □  Search",
             80, hy + 18, fromHex(COL_SUBTEXT), 20);
    // Download indicator
    if (ctx.net.activeCount() > 0) {
        char dl[64];
        snprintf(dl, sizeof(dl), "↓ %d active", ctx.net.activeCount());
        auto ddim = gfx.measureText(dl, 20);
        gfx.text(dl, SCREEN_W - ddim.x - 80, hy + 18,
                 fromHex(COL_PROGRESS), 20);
    }
}

// ─────────────────────────────────────────────
//  LibraryScreen
// ─────────────────────────────────────────────

LibraryScreen::LibraryScreen(ContentType type) : type_(type) {}

void LibraryScreen::refreshEntries(ScreenCtx& ctx) {
    if (selCategory_.empty())
        entries_ = ctx.db.byType(type_);
    else
        entries_ = ctx.db.byCategory(type_, selCategory_);
}

void LibraryScreen::drawCard(ScreenCtx& ctx, ContentEntry* e, int x, int y, bool focused) {
    auto& gfx = ctx.gfx;
    int cw = 280, ch = 320;

    // Shadow
    gfx.fillRoundRect(x+4, y+4, cw, ch, 12, fromHex(0x00000066));
    // Card bg
    Color bg = focused ? fromHex(0x1E1E30FF) : fromHex(COL_PANEL);
    gfx.fillRoundRect(x, y, cw, ch, 12, bg);

    drawPreview(ctx, *e, x + 8, y + 8, cw - 16, 200);

    // Name
    std::string nm = gfx.truncate(e->name, cw-16, 18, false);
    gfx.text(nm, x+8, y+216, fromHex(COL_TEXT), 18);

    // Category chip
    gfx.fillRoundRect(x+8, y+244, 90, 24, 4, fromHex(0x2A1A45FF));
    gfx.text(e->category, x+14, y+248, fromHex(COL_ACCENT2), 14);

    // Animated badge
    if (e->animated) {
        gfx.fillRoundRect(x+104, y+244, 70, 24, 4, fromHex(0x1A3A20FF));
        gfx.text("DYNAMIC", x+110, y+248, fromHex(COL_SUCCESS), 14);
    }

    // Installed indicator
    if (e->installState == InstallState::Done) {
        gfx.fillRoundRect(x+cw-40, y+8, 32, 32, 6, fromHex(COL_SUCCESS));
        gfx.textCentered("✓", x+cw-40, y+8, 32, 32, fromHex(0x000000FF), 18, true);
    }

    // Favorite star
    if (e->favorite) {
        gfx.text("★", x+cw-28, y+244, fromHex(0xFFDD00FF), 20);
    }

    // Focus ring
    if (focused) {
        gfx.drawRect(x-3, y-3, cw+6, ch+6, fromHex(COL_ACCENT), 2);
    }

    // Progress bar if downloading
    if (e->installState == InstallState::Downloading) {
        auto jobs = ctx.net.queueSnapshot();
        for (auto& j : jobs) {
            if (j.id == e->id) {
                gfx.progressBar(x+8, y+ch-20, cw-16, 8, j.progress(),
                                fromHex(0x222233FF), fromHex(COL_PROGRESS), 4);
                break;
            }
        }
    }
}

void LibraryScreen::update(ScreenCtx& ctx) {
    if (!loaded_) {
        refreshEntries(ctx);
        categories_ = type_ == ContentType::Avatar ? ctx.db.avatarCategories() : ctx.db.themeCategories();
        loaded_ = true;
    }

    if (ctx.ctrl.pressed(ORBIS_PAD_BUTTON_SQUARE)) ctx.navigate(ScreenID::Search, nullptr);
    if (entries_.empty()) return;

    int cols = 5;
    int rows = ((int)entries_.size() + cols - 1) / cols;

    if (ctx.ctrl.pressed(ORBIS_PAD_BUTTON_RIGHT)) {
        int next = colIdx_ + 1;
        if (next < cols && rowIdx_ * cols + next < (int)entries_.size()) colIdx_ = next;
    }
    if (ctx.ctrl.pressed(ORBIS_PAD_BUTTON_LEFT) && colIdx_ > 0) colIdx_--;
    if (ctx.ctrl.pressed(ORBIS_PAD_BUTTON_DOWN) && rowIdx_ < rows - 1) rowIdx_++;
    if (ctx.ctrl.pressed(ORBIS_PAD_BUTTON_UP)   && rowIdx_ > 0) rowIdx_--;

    // Clamp colIdx for last (possibly shorter) row
    int maxCol = std::min(cols - 1, (int)entries_.size() - rowIdx_ * cols - 1);
    if (colIdx_ > maxCol) colIdx_ = maxCol;

    // Smooth scroll
    float targetX = -(float)(colIdx_ * 296);
    float targetY = -(float)(rowIdx_ * 340);
    scrollX_ += (targetX - scrollX_) * 0.18f;
    scrollY_ += (targetY - scrollY_) * 0.18f;

    auto* entry = entries_[rowIdx_ * cols + colIdx_];
    if (ctx.ctrl.pressed(ORBIS_PAD_BUTTON_TRIANGLE)) ctx.db.toggleFavorite(entry->id);
    if (ctx.ctrl.pressed(ORBIS_PAD_BUTTON_CROSS)) ctx.navigate(ScreenID::ItemPreview, entry);
}

void LibraryScreen::render(ScreenCtx& ctx) {
    auto& gfx = ctx.gfx;

    // Header
    gfx.fillRect(0, 0, SCREEN_W, 100, fromHex(0x0D0D1AFF));
    std::string title = (type_ == ContentType::Avatar) ? "AVATAR LIBRARY" : "THEME LIBRARY";
    gfx.text(title, 60, 28, fromHex(COL_HIGHLIGHT), 36, true);
    char cnt[64];
    snprintf(cnt, sizeof(cnt), "%zu items", entries_.size());
    gfx.text(cnt, 60, 68, fromHex(COL_SUBTEXT), 18);

    // Category filters (top-right)
    // (simplified — categories shown as tab chips)
    const auto& cats = categories_;
    int cx = SCREEN_W - 60;
    for (int i = (int)cats.size() - 1; i >= 0; --i) {
        int cw2 = gfx.measureText(cats[i], 16).x + 20;
        cx -= cw2 + 8;
        bool sel = cats[i] == selCategory_;
        Color bg = sel ? fromHex(COL_ACCENT) : fromHex(COL_PANEL_ALT);
        gfx.fillRoundRect(cx, 32, cw2, 32, 6, bg);
        gfx.text(cats[i], cx + 10, 40, fromHex(sel ? 0xFFFFFFFF : COL_SUBTEXT), 16);
    }

    // Grid
    int cardW = 280, cardH = 320, padX = 16, padY = 16;
    int startX = 60;
    int startY = 110;
    int cols = 5;

    const int rowHeight = cardH + padY;
    const int firstRow = std::max(0, (-int(scrollY_) - cardH - 50) / rowHeight);
    const int lastRow = std::max(firstRow, (SCREEN_H + 50 - startY - int(scrollY_)) / rowHeight + 1);
    const int end = std::min(int(entries_.size()), lastRow * cols);
    for (int i = firstRow * cols; i < end; ++i) {
        int row = i / cols;
        int col = i % cols;
        int x = startX + (int)scrollX_ + col * (cardW + padX);
        int y = startY + (int)scrollY_ + row * (cardH + padY);

        // Cull off-screen cards
        if (x + cardW < 0 || x > SCREEN_W) continue;
        if (y + cardH < startY - 50 || y > SCREEN_H + 50) continue;

        bool focused = (col == colIdx_ && row == rowIdx_);
        drawCard(ctx, entries_[i], x, y, focused);
    }

    // Scroll indicators
    if ((int)entries_.size() > cols) {
        float viewFrac = (float)1 / (float)(((int)entries_.size() + cols - 1) / cols);
        float offset   = (float)rowIdx_ / (float)(((int)entries_.size() + cols - 1) / cols);
        gfx.scrollBar(SCREEN_W - 20, startY, SCREEN_H - startY - 60, offset, viewFrac);
    }

    // Bottom hint
    int hy = SCREEN_H - 56;
    gfx.fillRect(0, hy, SCREEN_W, 56, fromHex(0x0A0A14E0), 0.9f);
    gfx.text("✕  Preview & Install    △  Favorite    ○  Back    □  Search",
             60, hy + 16, fromHex(COL_SUBTEXT), 20);
}

// ─────────────────────────────────────────────
//  SearchScreen
// ─────────────────────────────────────────────

SearchScreen::SearchScreen() {}

void SearchScreen::doSearch(ScreenCtx& ctx) {
    if (query_.empty()) {
        results_.clear();
        return;
    }
    bool animOnly = (filterMode_ == 3);
    // mode 0=all, 1=avatars, 2=themes
    ContentType filter = (ContentType)255;  // all
    if (filterMode_ == 1) filter = ContentType::Avatar;
    if (filterMode_ == 2) filter = ContentType::Theme;
    results_ = ctx.db.search(query_, filter, animOnly);
    selIdx_ = 0;
    scroll_ = 0.f;
}

void SearchScreen::update(ScreenCtx& ctx) {
    static const std::string alphabet = "abcdefghijklmnopqrstuvwxyz0123456789 ";
    if (ctx.ctrl.pressed(ORBIS_PAD_BUTTON_LEFT))
        searchChar_ = (searchChar_ + alphabet.size() - 1) % alphabet.size();
    if (ctx.ctrl.pressed(ORBIS_PAD_BUTTON_RIGHT))
        searchChar_ = (searchChar_ + 1) % alphabet.size();
    if (ctx.ctrl.pressed(ORBIS_PAD_BUTTON_SQUARE) && query_.size() < 100) {
        query_ += alphabet[searchChar_];
        doSearch(ctx);
    }
    if (ctx.ctrl.pressed(ORBIS_PAD_BUTTON_TRIANGLE) && !query_.empty()) {
        query_.pop_back();
        doSearch(ctx);
    }
    if (!results_.empty() && ctx.ctrl.pressed(ORBIS_PAD_BUTTON_CROSS))
        ctx.navigate(ScreenID::ItemPreview, results_[selIdx_]);
    if (ctx.ctrl.pressed(ORBIS_PAD_BUTTON_DOWN) && selIdx_ < (int)results_.size()-1) selIdx_++;
    if (ctx.ctrl.pressed(ORBIS_PAD_BUTTON_UP)   && selIdx_ > 0) selIdx_--;
    if (ctx.ctrl.pressed(ORBIS_PAD_BUTTON_L1)) {
        filterMode_ = (filterMode_ + 3) % 4;
        doSearch(ctx);
    }
    if (ctx.ctrl.pressed(ORBIS_PAD_BUTTON_R1)) {
        filterMode_ = (filterMode_ + 1) % 4;
        doSearch(ctx);
    }
}

void SearchScreen::drawResultCard(ScreenCtx& ctx, ContentEntry* e, int y, bool focused) {
    auto& gfx = ctx.gfx;
    Color bg = focused ? fromHex(0x1E1E34FF) : fromHex(COL_PANEL);
    gfx.fillRoundRect(60, y, SCREEN_W - 120, 90, 10, bg);
    if (focused)
        gfx.drawRect(58, y-2, SCREEN_W-116, 94, fromHex(COL_ACCENT), 2);
    gfx.text(e->name, 90, y+10, fromHex(COL_HIGHLIGHT), 22, true);
    std::string meta = e->category + " / " + e->subcategory
        + (e->animated ? "  [DYNAMIC]" : "")
        + (e->type == ContentType::Avatar ? "  AVATAR" : "  THEME");
    gfx.text(meta, 90, y+42, fromHex(COL_SUBTEXT), 17);
    if (e->installState == InstallState::Done)
        gfx.text("✓ installed", SCREEN_W - 200, y+32, fromHex(COL_SUCCESS), 18);
    if (e->favorite)
        gfx.text("★", SCREEN_W - 100, y+32, fromHex(0xFFDD00FF), 24);
}

void SearchScreen::render(ScreenCtx& ctx) {
    auto& gfx = ctx.gfx;

    // Header
    gfx.fillRect(0, 0, SCREEN_W, 100, fromHex(0x0D0D1AFF));
    gfx.text("SEARCH", 60, 28, fromHex(COL_HIGHLIGHT), 36, true);

    // Search box
    gfx.fillRoundRect(60, 110, SCREEN_W - 120, 60, 10, fromHex(COL_PANEL_ALT));
    gfx.drawRect(60, 110, SCREEN_W - 120, 60, fromHex(COL_ACCENT), 2);
    std::string display = query_.empty() ? "Search avatars & themes..." : query_ + "|";
    Color tc = query_.empty() ? fromHex(COL_SUBTEXT) : fromHex(COL_TEXT);
    gfx.text(display, 90, 128, tc, 22);
    static const std::string alphabet = "abcdefghijklmnopqrstuvwxyz0123456789 ";
    const char letter = alphabet[searchChar_];
    const auto selected = letter == ' ' ? std::string("space") : std::string(1, letter);
    gfx.text("← → Choose [" + selected + "]    □ Add    △ Delete    ✕ Open result",
             900, 128, fromHex(COL_SUBTEXT), 20);

    // Filter tabs
    static const char* ftabs[] = {"ALL", "AVATARS", "THEMES", "ANIMATED"};
    int fx = 60, fy = 185;
    for (int i = 0; i < 4; ++i) {
        int fw = gfx.measureText(ftabs[i], 16).x + 24;
        Color bg = (i == filterMode_) ? fromHex(COL_ACCENT) : fromHex(COL_PANEL);
        gfx.fillRoundRect(fx, fy, fw, 30, 6, bg);
        gfx.text(ftabs[i], fx+12, fy+6, fromHex(COL_TEXT), 16);
        fx += fw + 10;
    }

    // Results
    int ry = 230;
    gfx.text("Results:", 60, ry, fromHex(COL_SUBTEXT), 18);
    if (query_.empty()) {
        gfx.textCentered("Choose a character and press □ to search",
                         0, 300, SCREEN_W, 400, fromHex(COL_SUBTEXT), 24);
        return;
    }
    if (results_.empty()) {
        gfx.textCentered("No results for \"" + query_ + "\"",
                         0, 300, SCREEN_W, 400, fromHex(COL_SUBTEXT), 24);
        return;
    }

    char rcnt[64];
    snprintf(rcnt, sizeof(rcnt), "%zu results", results_.size());
    gfx.text(rcnt, 60 + gfx.measureText("Results: ", 18).x, ry,
             fromHex(COL_ACCENT2), 18);

    int cardY = 260;
    int cardH = 100;
    int visible = (SCREEN_H - cardY - 60) / cardH;
    int start = std::max(0, selIdx_ - visible/2);
    for (int i = start; i < std::min((int)results_.size(), start + visible + 1); ++i) {
        drawResultCard(ctx, results_[i], cardY + (i - start) * cardH, i == selIdx_);
    }

    // Bottom hint
    int hy = SCREEN_H - 56;
    gfx.fillRect(0, hy, SCREEN_W, 56, fromHex(0x0A0A14E0), 0.9f);
    gfx.text("L1/R1  Filter    ✕  Select    ○  Back",
             60, hy + 16, fromHex(COL_SUBTEXT), 20);
}

// ─────────────────────────────────────────────
//  PreviewScreen
// ─────────────────────────────────────────────

namespace {
std::string formatBytes(uint64_t bytes) {
    constexpr double mib = 1024.0 * 1024;
    constexpr double gib = 1024.0 * mib;
    char formatted[40];
    if (bytes >= gib) snprintf(formatted, sizeof(formatted), "%.2f GiB", bytes / gib);
    else if (bytes >= mib) snprintf(formatted, sizeof(formatted), "%.1f MiB", bytes / mib);
    else if (bytes >= 1024) snprintf(formatted, sizeof(formatted), "%.1f KiB", bytes / 1024.0);
    else snprintf(formatted, sizeof(formatted), "%llu B", static_cast<unsigned long long>(bytes));
    return formatted;
}

bool splitTheme(const ContentEntry& entry) {
    return entry.type == ContentType::Theme && entry.fileFormat == "pkg_split";
}

bool canDownload(const ContentEntry& entry) {
    if (splitTheme(entry)) return validHttpsUrl(entry.pkgStubUrl) && validHttpsUrl(entry.pkgPayloadUrl);
    return validHttpsUrl(entry.downloadUrl) &&
           (entry.type == ContentType::Theme ? entry.fileFormat == "pkg_theme" :
            (entry.fileFormat == "xavatar_fpkg" || entry.fileFormat == "xavatar"));
}

std::string transferSize(const DownloadJob& job) {
    if (!job.totalBytes) return formatBytes(job.bytesDown) + " baixados · Tamanho desconhecido";
    return formatBytes(job.bytesDown) + " / " + formatBytes(job.totalBytes) + "  " +
           std::to_string(int(job.progress() * 100)) + "%";
}
}

PreviewScreen::PreviewScreen(ContentEntry* entry) : entry_(entry) {}

void PreviewScreen::update(ScreenCtx& ctx) {
    if (ctx.ctrl.pressed(ORBIS_PAD_BUTTON_RIGHT)) btnIdx_ = (btnIdx_ + 1) % 3;
    if (ctx.ctrl.pressed(ORBIS_PAD_BUTTON_LEFT)) btnIdx_ = (btnIdx_ + 2) % 3;

    const auto installResult = ctx.installer.takeResult(entry_->id);
    if (!installResult.empty()) statusMsg_ = installResult;
    const auto jobs = ctx.net.queueSnapshot();
    bool pending = false;
    std::string error;
    for (const auto& job : jobs) {
        if (job.id != entry_->id && job.id != entry_->id + "_stub" &&
            job.id != entry_->id + "_payload") continue;
        pending |= job.state == DownloadState::Queued || activeDownload(job.state);
        if (job.state == DownloadState::Failed) error = job.errorMsg;
    }
    if (downloading_ && !pending) {
        bool completed = std::any_of(jobs.begin(), jobs.end(), [&](const auto& job) {
            return (job.id == entry_->id || job.id == entry_->id + "_payload") &&
                   job.state == DownloadState::Completed;
        });
        statusMsg_ = !error.empty() ? error : completed ? "Transfer finished. Select Install to verify the files."
                                                      : "Transfer cancelled.";
    }
    downloading_ = pending;

    if (!ctx.ctrl.pressed(ORBIS_PAD_BUTTON_CROSS)) return;
    if (btnIdx_ == 2) {
        ctx.db.toggleFavorite(entry_->id);
        statusMsg_ = entry_->favorite ? "★ Favorited" : "Removed from favorites";
        return;
    }
    if (downloading_) {
        statusMsg_ = "The download is still pending.";
        return;
    }

    const bool avatar = entry_->type == ContentType::Avatar;
    const std::string base = std::string(DOWNLOAD_DIR) + entry_->id;
    const auto singlePath = base + (avatar && entry_->fileFormat == "xavatar" ? ".xavatar" : ".pkg");
    const auto stubPath = base + "_1.pkg";
    const auto payloadPath = base + "_2.pkg";
    if (ctx.installer.busy()) {
        statusMsg_ = "An installation request is still running.";
        return;
    }
    if (btnIdx_ == 1) {
        ContentEntry requested = *entry_;
        requested.localPath = singlePath;
        requested.localStubPath = stubPath;
        requested.localPayloadPath = payloadPath;
        statusMsg_ = ctx.installer.submit(std::move(requested))
            ? "Validating local files for installation…" : "Install unavailable: worker busy or failed to start.";
        return;
    }
    if (!canDownload(*entry_)) {
        statusMsg_ = "Download indisponível neste catálogo.";
        return;
    }

    const auto enqueue = [&](const std::string& id, const std::string& url,
                             const std::string& path, uint64_t bytes, PackageRole role) {
        DownloadJob job;
        job.id = id;
        job.url = url;
        job.destPath = path;
        job.totalBytes = bytes;
        job.role = role;
        ctx.net.enqueue(std::move(job));
    };
    if (!splitTheme(*entry_)) {
        enqueue(entry_->id, entry_->downloadUrl, singlePath, entry_->fileSize,
                !avatar ? PackageRole::ThemePayload : entry_->fileFormat == "xavatar_fpkg" ?
                PackageRole::AvatarPack : PackageRole::None);
    } else {
        enqueue(entry_->id + "_stub", entry_->pkgStubUrl, stubPath, entry_->stubSize, PackageRole::ThemeFirst);
        enqueue(entry_->id + "_payload", entry_->pkgPayloadUrl, payloadPath, entry_->payloadSize, PackageRole::ThemePayload);
    }
    statusMsg_ = "Queued for download";
    downloading_ = true;
}

void PreviewScreen::render(ScreenCtx& ctx) {
    auto& gfx = ctx.gfx;

    // Dark panel bg
    gfx.fillRect(0, 0, SCREEN_W, SCREEN_H, fromHex(0x07070FFF));

    // Left — artwork panel
    int artW = 640, artH = 640;
    int artX = 100, artY = (SCREEN_H - artH) / 2;
    gfx.fillRoundRect(artX - 8, artY - 8, artW + 16, artH + 16, 16,
                      fromHex(0x1A1A2EFF));

    drawPreview(ctx, *entry_, artX, artY, artW, artH);

    // Right — info panel
    int infoX = artX + artW + 80;
    int infoY = 120;
    int infoW = SCREEN_W - infoX - 60;

    // Category chip
    gfx.fillRoundRect(infoX, infoY, 160, 32, 6, fromHex(COL_ACCENT));
    gfx.textCentered(
        (entry_->type == ContentType::Avatar ? "AVATAR" : "THEME"),
        infoX, infoY, 160, 32, fromHex(COL_HIGHLIGHT), 16, true);

    if (entry_->animated) {
        gfx.fillRoundRect(infoX + 170, infoY, 100, 32, 6, fromHex(0x1A3A20FF));
        gfx.textCentered("DYNAMIC", infoX + 170, infoY, 100, 32,
                         fromHex(COL_SUCCESS), 16, true);
    }

    // Title
    gfx.text(entry_->name, infoX, infoY + 50, fromHex(COL_HIGHLIGHT), 36, true, true);

    // Metadata rows
    struct Row { std::string label; std::string value; };
    std::vector<Row> rows {
        {"Category",   entry_->category + " / " + entry_->subcategory},
        {"Source",     entry_->source},
        {"Format",     entry_->fileFormat},
    };
    auto size = entry_->fileSize;
    if (splitTheme(*entry_)) size = entry_->stubSize && entry_->payloadSize
        ? entry_->stubSize + entry_->payloadSize : 0;
    rows.push_back({"Tamanho", !canDownload(*entry_) ? "Indisponível" :
                              size ? formatBytes(size) : "Tamanho desconhecido"});

    int ry = infoY + 110;
    for (auto& row : rows) {
        gfx.text(row.label + ":", infoX, ry, fromHex(COL_SUBTEXT), 18);
        gfx.text(row.value, infoX + 160, ry, fromHex(COL_TEXT), 18);
        ry += 34;
    }

    // Tags
    ry += 10;
    gfx.text("Tags:", infoX, ry, fromHex(COL_SUBTEXT), 18);
    int tx = infoX + 80;
    for (auto& tag : entry_->tags) {
        int tw = gfx.measureText(tag, 14).x + 16;
        if (tx + tw > infoX + infoW) break;
        gfx.fillRoundRect(tx, ry - 2, tw, 24, 4, fromHex(0x1E1E30FF));
        gfx.text(tag, tx + 8, ry + 1, fromHex(COL_SUBTEXT), 14);
        tx += tw + 6;
    }
    ry += 40;

    // Status message
    if (!statusMsg_.empty()) {
        gfx.fillRoundRect(infoX, ry, infoW, 44, 8, fromHex(0x1A2A1AFF));
        gfx.text(gfx.truncate(statusMsg_, infoW - 32, 18), infoX + 16, ry + 12, fromHex(COL_TEXT), 18);
        ry += 56;
    }

    // Action buttons
    static const char* btnLabels[] = { "DOWNLOAD", "INSTALL", "★ FAVORITE" };
    Color btnColors[] = {
        fromHex(COL_ACCENT),
        fromHex(0xFFDD00AA),
        fromHex(COL_PANEL_ALT)
    };
    int bx = infoX;
    int by = ry + 20;
    int bh = 56;
    for (int i = 0; i < 3; ++i) {
        int bw = gfx.measureText(btnLabels[i], 20, true).x + 48;
        bool sel = (i == btnIdx_);
        Color bc = i == 0 && !canDownload(*entry_) ? fromHex(COL_PANEL_ALT) : btnColors[i];
        if (!sel) bc.a = 100;
        gfx.fillRoundRect(bx, by, bw, bh, 10, bc, sel ? 1.0f : 0.4f);
        if (sel) {
            gfx.drawRect(bx - 2, by - 2, bw + 4, bh + 4, bc, 2);
        }
        gfx.textCentered(btnLabels[i], bx, by, bw, bh, fromHex(COL_HIGHLIGHT), 20, true);
        bx += bw + 20;
    }

    // Download progress if active
    auto jobs = ctx.net.queueSnapshot();
    for (auto& j : jobs) {
        if (j.id == entry_->id || j.id == entry_->id + "_stub" ||
            j.id == entry_->id + "_payload") {
            int py = by + bh + 30;
            gfx.text(transferSize(j) + (j.state == DownloadState::Downloading ? "  " + j.speedStr() + "  ETA: " + j.etaStr() : ""),
                     infoX, py, fromHex(COL_SUBTEXT), 18);
            if (j.totalBytes) gfx.progressBar(infoX, py + 30, infoW, 12, j.progress(),
                            fromHex(0x222233FF), fromHex(COL_PROGRESS), 6);
            break;
        }
    }
}

// ─────────────────────────────────────────────
//  DownloadsScreen
// ─────────────────────────────────────────────

void DownloadsScreen::update(ScreenCtx& ctx) {
    auto jobs = ctx.net.queueSnapshot();
    if (ctx.ctrl.pressed(ORBIS_PAD_BUTTON_DOWN) && selIdx_ < (int)jobs.size()-1) selIdx_++;
    if (ctx.ctrl.pressed(ORBIS_PAD_BUTTON_UP)   && selIdx_ > 0) selIdx_--;
    selIdx_ = std::max(0, std::min(selIdx_, int(jobs.size()) - 1));
    if (!jobs.empty()) {
        auto& j = jobs[selIdx_];
        if (ctx.ctrl.pressed(ORBIS_PAD_BUTTON_SQUARE)) {
            if (j.paused) ctx.net.resume(j.id);
            else          ctx.net.pause(j.id);
        }
        if (ctx.ctrl.pressed(ORBIS_PAD_BUTTON_TRIANGLE)) ctx.net.cancel(j.id);
        if (ctx.ctrl.pressed(ORBIS_PAD_BUTTON_CROSS) &&
            (j.state == DownloadState::Failed || j.state == DownloadState::Cancelled))
            ctx.net.retry(j.id);
    }
}

void DownloadsScreen::render(ScreenCtx& ctx) {
    auto& gfx = ctx.gfx;
    gfx.fillRect(0, 0, SCREEN_W, 100, fromHex(0x0D0D1AFF));
    gfx.text("DOWNLOADS", 60, 28, fromHex(COL_HIGHLIGHT), 36, true);

    auto jobs = ctx.net.queueSnapshot();
    if (jobs.empty()) {
        gfx.textCentered("No active downloads", 0, 100, SCREEN_W, SCREEN_H - 160,
                         fromHex(COL_SUBTEXT), 28);
        return;
    }

    int ry = 120;
    for (int i = std::max(0, selIdx_ - 5); i < (int)jobs.size(); ++i) {
        auto& j = jobs[i];
        bool sel = (i == selIdx_);
        Color bg = sel ? fromHex(0x1A1A30FF) : fromHex(COL_PANEL);
        gfx.fillRoundRect(60, ry, SCREEN_W - 120, 110, 10, bg);
        if (sel) gfx.drawRect(58, ry-2, SCREEN_W-116, 114, fromHex(COL_ACCENT), 2);

        // Job ID (shortened)
        gfx.text(j.id, 90, ry + 10, fromHex(COL_TEXT), 20, true);

        // Status + speed
        static const char* stateStr[] = {"Queued","Connecting","Downloading","Validating","Completed","Failed","Cancelled"};
        Color stc = fromHex(COL_SUBTEXT);
        if (activeDownload(j.state)) stc = fromHex(COL_PROGRESS);
        if (j.state == DownloadState::Completed)        stc = fromHex(COL_SUCCESS);
        if (j.state == DownloadState::Failed)       stc = fromHex(COL_ERROR);

        std::string info = j.paused ? "Pausado" : stateStr[static_cast<int>(j.state)];
        if (j.state == DownloadState::Downloading)
            info += "  " + j.speedStr() + "  ETA: " + j.etaStr();
        if (j.state == DownloadState::Failed) info += "  " + j.errorMsg;
        gfx.text(gfx.truncate(info, SCREEN_W - 180, 18), 90, ry + 37, stc, 18);
        gfx.text(j.state == DownloadState::Queued ? "Aguardando a vez na fila" : transferSize(j), 90, ry + 60, fromHex(COL_TEXT), 18);
        if (j.totalBytes && j.state != DownloadState::Queued) gfx.progressBar(90, ry + 86, SCREEN_W - 200, 10, j.progress(),
                                         fromHex(0x222233FF), fromHex(COL_PROGRESS), 5);

        ry += 126;
        if (ry > SCREEN_H - 80) break;
    }

    int hy = SCREEN_H - 56;
    gfx.fillRect(0, hy, SCREEN_W, 56, fromHex(0x0A0A14E0), 0.9f);
    gfx.text("□  Pause/Resume    △  Cancel / Remove history    ✕  Retry    ○  Back",
             60, hy + 16, fromHex(COL_SUBTEXT), 20);
}

// ─────────────────────────────────────────────
//  FavoritesScreen
// ─────────────────────────────────────────────

void FavoritesScreen::update(ScreenCtx& ctx) {
    if (revision_ != ctx.db.revision()) {
        favs_ = ctx.db.favorites();
        revision_ = ctx.db.revision();
    }
    if (ctx.ctrl.pressed(ORBIS_PAD_BUTTON_DOWN) && selIdx_ < (int)favs_.size()-1) selIdx_++;
    if (ctx.ctrl.pressed(ORBIS_PAD_BUTTON_UP)   && selIdx_ > 0) selIdx_--;
    if (selIdx_ >= (int)favs_.size()) selIdx_ = std::max(0, (int)favs_.size() - 1);
    if (!favs_.empty() && ctx.ctrl.pressed(ORBIS_PAD_BUTTON_CROSS))
        ctx.navigate(ScreenID::ItemPreview, favs_[selIdx_]);
}

void FavoritesScreen::render(ScreenCtx& ctx) {
    auto& gfx = ctx.gfx;
    gfx.fillRect(0, 0, SCREEN_W, 100, fromHex(0x0D0D1AFF));
    gfx.text("★ FAVORITES", 60, 28, fromHex(COL_HIGHLIGHT), 36, true);

    if (favs_.empty()) {
        gfx.textCentered("No favorites yet — press △ on any item to add",
                         0, 100, SCREEN_W, SCREEN_H - 160, fromHex(COL_SUBTEXT), 24);
        return;
    }

    int ry = 120;
    for (int i = std::max(0, selIdx_ - 7); i < (int)favs_.size(); ++i) {
        bool sel = (i == selIdx_);
        Color bg = sel ? fromHex(0x1E1E34FF) : fromHex(COL_PANEL);
        gfx.fillRoundRect(60, ry, SCREEN_W - 120, 80, 10, bg);
        if (sel) gfx.drawRect(58, ry-2, SCREEN_W-116, 84, fromHex(COL_ACCENT2), 2);
        gfx.text(favs_[i]->name, 90, ry + 12, fromHex(COL_HIGHLIGHT), 22, true);
        gfx.text(favs_[i]->category + " / " + favs_[i]->subcategory,
                 90, ry + 42, fromHex(COL_SUBTEXT), 17);
        ry += 92;
        if (ry > SCREEN_H - 80) break;
    }
}

// ─────────────────────────────────────────────
//  SettingsScreen
// ─────────────────────────────────────────────

void SettingsScreen::update(ScreenCtx& ctx) {
    if (settings_.empty()) {
        settings_ = {
            {"Catálogo", ctx.remoteCatalog ? "HTTPS configurado" : "Catálogo local"},
            {"Atualização", ""},
            {"Download Dir",    DOWNLOAD_DIR},
            {"Cache Dir",       CACHE_DIR},
            {"App Version",     APP_VERSION},
            {"Keep failed download for diagnostics", ""},
            {"Delete rejected sample", ""},
        };
    }
    settings_[1].value = ctx.catalogConfigError.empty() ? ctx.net.manifestStatus() : ctx.catalogConfigError;
    if (settings_[1].value.empty()) settings_[1].value = "Nenhuma atualização remota configurada";
    if (ctx.ctrl.pressed(ORBIS_PAD_BUTTON_DOWN) && selIdx_ < (int)settings_.size()-1) selIdx_++;
    if (ctx.ctrl.pressed(ORBIS_PAD_BUTTON_UP)   && selIdx_ > 0) selIdx_--;
    constexpr int keep = 5, clear = 6;
    if (ctx.ctrl.pressed(ORBIS_PAD_BUTTON_CROSS)) {
        if (selIdx_ == keep) ctx.net.toggleKeepRejected();
        if (selIdx_ == clear) ctx.net.requestClearRejected();
    }
    settings_[keep].value = ctx.net.keepingRejected() ? "Ativado nesta sessão · ✕ para desligar" :
                                                        "Desativado · ✕ para manter uma amostra nesta sessão";
    settings_[clear].value = formatBytes(ctx.net.rejectedBytes()) +
                            " / 192 MiB · " REJECTED_DOWNLOAD " · ✕ para excluir";
}

void SettingsScreen::render(ScreenCtx& ctx) {
    auto& gfx = ctx.gfx;
    gfx.fillRect(0, 0, SCREEN_W, 100, fromHex(0x0D0D1AFF));
    gfx.text("SETTINGS", 60, 28, fromHex(COL_HIGHLIGHT), 36, true);

    int ry = 120;
    for (int i = 0; i < (int)settings_.size(); ++i) {
        bool sel = (i == selIdx_);
        Color bg = sel ? fromHex(0x1A1A30FF) : fromHex(COL_PANEL);
        gfx.fillRoundRect(60, ry, SCREEN_W - 120, 70, 8, bg);
        if (sel) gfx.drawRect(58, ry-2, SCREEN_W-116, 74, fromHex(COL_ACCENT), 2);
        gfx.text(settings_[i].label, 90, ry + 8, fromHex(COL_SUBTEXT), 17);
        std::string val = gfx.truncate(settings_[i].value, SCREEN_W - 400, 19);
        gfx.text(val, 90, ry + 34, fromHex(COL_TEXT), 19);
        ry += 82;
    }
}
