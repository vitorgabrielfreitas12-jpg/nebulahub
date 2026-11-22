#pragma once
#include "../nebula.h"
#include <SDL2/SDL.h>
#include <SDL_ttf.h>
#include <SDL2/SDL_image.h>
#include <string>
#include <unordered_map>

struct Color { uint8_t r, g, b, a; };

inline Color fromHex(uint32_t hex) {
    return { (uint8_t)(hex >> 24), (uint8_t)(hex >> 16),
             (uint8_t)(hex >> 8),  (uint8_t)(hex) };
}

// ─────────────────────────────────────────────
//  Renderer — thin SDL2 wrapper
// ─────────────────────────────────────────────

class Renderer {
public:
    explicit Renderer(SDL_Renderer* r);
    ~Renderer();

    void clear(Color bg = fromHex(COL_BG));
    void present();

    // Primitives
    void fillRect(int x, int y, int w, int h, Color c, float alpha = 1.f);
    void drawRect(int x, int y, int w, int h, Color c, int thick = 1);
    void fillRoundRect(int x, int y, int w, int h, int radius, Color c, float alpha = 1.f);
    void drawLine(int x1, int y1, int x2, int y2, Color c);

    // Gradient bar (horizontal)
    void gradientH(int x, int y, int w, int h, Color left, Color right);

    // Text
    void text(const std::string& str, int x, int y, Color c,
              int size = 24, bool bold = false,
              bool shadow = false);

    // Measure text
    SDL_Point measureText(const std::string& str, int size, bool bold = false);

    // Centered text in a rect
    void textCentered(const std::string& str, int rx, int ry, int rw, int rh,
                      Color c, int size = 24, bool bold = false);

    // Truncate text with "…" to fit width
    std::string truncate(const std::string& str, int maxWidth, int size, bool bold = false);

    // Textures / images
    SDL_Texture* loadTexture(const std::string& path);
    void         freeTexture(const std::string& path);
    void         drawTexture(SDL_Texture* tex, int x, int y, int w, int h,
                             float alpha = 1.f);
    void         drawTextureRounded(SDL_Texture* tex, int x, int y, int w, int h,
                                    int radius, float alpha = 1.f);

    // Progress bar
    void progressBar(int x, int y, int w, int h, float progress,
                     Color bg, Color fg, int radius = 4);

    // Scrollbar
    void scrollBar(int x, int y, int h, float offset, float viewFraction);

    SDL_Renderer* raw() const { return sdl_; }

private:
    SDL_Renderer* sdl_;

    // Font cache keyed by size+bold
    std::unordered_map<std::string, TTF_Font*> fonts_;
    TTF_Font* getFont(int size, bool bold);

    // Texture cache keyed by path
    std::unordered_map<std::string, SDL_Texture*> texCache_;

    static std::string fontKey(int size, bool bold) {
        return std::to_string(size) + (bold?"B":"R");
    }
};
