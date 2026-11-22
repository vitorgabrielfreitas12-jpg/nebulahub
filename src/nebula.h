#pragma once

// ─────────────────────────────────────────────
//  NEBULA HUB — Global Constants
// ─────────────────────────────────────────────

#define APP_TITLE        "NEBULA HUB"
#define APP_VERSION      "1.0.2"
#define TITLE_ID         "NEBU00001"

#define SCREEN_W         1920
#define SCREEN_H         1080

// Optional build default. A local manifest-url.txt can override it.
#define MANIFEST_URL     ""

// Local paths. Actual write access is checked before using them.
#define DATA_DIR         "/data/NEBULAHUB/"
#define DOWNLOAD_DIR     "/data/NEBULAHUB/downloads/"
#define REJECTED_DOWNLOAD DATA_DIR "diagnostic-rejected.bin"
#define CACHE_DIR        "/data/NEBULAHUB/cache/"
#define AVATAR_DEST      "/data/AVATARS/"
#define PKG_INSTALL_DIR  "/data/pkg/"

// UI palette  (0xRRGGBBAA)
#define COL_BG           0x0A0A14FF   // deep navy
#define COL_PANEL        0x12121EFF   // panel
#define COL_PANEL_ALT    0x1A1A2EFF   // lighter panel
#define COL_ACCENT       0x7B2FFFFF  // nebula purple
#define COL_ACCENT2      0xFF5DB1FF   // pink
#define COL_TEXT         0xEEEEEEFF
#define COL_SUBTEXT      0x888899FF
#define COL_HIGHLIGHT    0xFFFFFFFF
#define COL_PROGRESS     0x7B2FFFFF
#define COL_ERROR        0xFF3B3BFF
#define COL_SUCCESS      0x2BFFB1FF

// Fonts (embedded in PKG sce_sys)
#define FONT_REGULAR     "/app0/assets/fonts/DejaVuSans.ttf"
#define FONT_BOLD        "/app0/assets/fonts/DejaVuSans-Bold.ttf"

// Content types
enum class ContentType { Avatar, Theme };
enum class ThemeKind   { Static, Dynamic };
enum class InstallState {
    None, Queued, Downloading, Installing, Done, Error
};

// Screen IDs
enum class ScreenID {
    Home,
    AvatarLibrary,
    ThemeLibrary,
    AnimatedThemes,
    Search,
    ItemPreview,
    Downloads,
    Installed,
    Favorites,
    Settings
};
