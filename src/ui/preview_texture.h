#pragma once

#include <SDL2/SDL.h>
#include <chrono>
#include <deque>
#include <memory>
#include <string>
#include <unordered_map>

class PreviewTextures {
public:
    explicit PreviewTextures(SDL_Renderer* renderer, decltype(&SDL_RenderCopy) copy = SDL_RenderCopy)
        : renderer_(renderer), copy_(copy) {}
    bool draw(const std::string& id, const std::string& source, SDL_Surface* image, SDL_Rect bounds);

private:
    struct Texture {
        std::unique_ptr<SDL_Texture, decltype(&SDL_DestroyTexture)> handle{nullptr, SDL_DestroyTexture};
        std::chrono::steady_clock::time_point retry{};
        size_t bytes = 0;
        int width = 0;
        int height = 0;
        bool drawn = false;
        bool drawFailed = false;
    };

    SDL_Renderer* renderer_;
    decltype(&SDL_RenderCopy) copy_;
    std::unordered_map<std::string, Texture> textures_;
    std::deque<std::string> order_;
    size_t bytes_ = 0;
};
