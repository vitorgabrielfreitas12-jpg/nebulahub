#include "image.h"

#define STB_IMAGE_IMPLEMENTATION
#define STBI_NO_STDIO
#define STBI_ONLY_PNG
#define STBI_ONLY_JPEG
#include <stb/stb_image.h>

#include <cstring>

SDL_Surface* decodePreview(const std::string& bytes) {
    if (bytes.empty() || bytes.size() > 2 * 1024 * 1024) return nullptr;
    int width = 0, height = 0, channels = 0;
    const auto* data = reinterpret_cast<const unsigned char*>(bytes.data());
    if (!stbi_info_from_memory(data, static_cast<int>(bytes.size()), &width, &height, &channels) ||
        width <= 0 || height <= 0 || width > 2048 || height > 2048) return nullptr;
    auto* pixels = stbi_load_from_memory(data, static_cast<int>(bytes.size()),
                                         &width, &height, &channels, STBI_rgb_alpha);
    if (!pixels) return nullptr;
    SDL_Surface* surface = SDL_CreateRGBSurfaceWithFormat(0, width, height, 32, SDL_PIXELFORMAT_RGBA32);
    if (surface) {
        for (int y = 0; y < height; ++y) {
            memcpy(static_cast<unsigned char*>(surface->pixels) + y * surface->pitch,
                   pixels + y * width * 4, width * 4);
        }
    }
    stbi_image_free(pixels);
    return surface;
}
