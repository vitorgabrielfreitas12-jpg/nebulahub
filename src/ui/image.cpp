#include "image.h"
#include "../file_input.h"

#define STB_IMAGE_IMPLEMENTATION
#define STBI_NO_STDIO
#define STBI_ONLY_PNG
#define STBI_ONLY_JPEG
#include <stb/stb_image.h>

#include <SDL2/SDL_endian.h>
#include <cstring>

namespace {
constexpr size_t maxPreviewBytes = 2 * 1024 * 1024;

SDL_Surface* fail(PreviewError* error, const char* stage, int code = 0, const char* detail = "") {
    if (error) *error = {stage, code, detail ? detail : ""};
    return nullptr;
}
}

SDL_Surface* decodePreview(const std::string& bytes, PreviewError* error) {
    if (bytes.empty() || bytes.size() > maxPreviewBytes)
        return fail(error, "Invalid image length");
    const auto* data = reinterpret_cast<const unsigned char*>(bytes.data());
    const bool png = bytes.compare(0, 8, "\x89PNG\r\n\x1a\n", 8) == 0;
    const bool jpeg = bytes.compare(0, 3, "\xff\xd8\xff", 3) == 0;
    if (!png && !jpeg) return fail(error, "Unsupported image signature");
    int width = 0, height = 0, channels = 0;
    if (!stbi_info_from_memory(data, static_cast<int>(bytes.size()), &width, &height, &channels))
        return fail(error, "Read image header", 0, stbi_failure_reason());
    if (width <= 0 || height <= 0 || width > 2048 || height > 2048)
        return fail(error, "Unsupported image dimensions");
    auto* pixels = stbi_load_from_memory(data, static_cast<int>(bytes.size()),
                                         &width, &height, &channels, STBI_rgb_alpha);
    if (!pixels) return fail(error, "Decode image pixels", 0, stbi_failure_reason());

#if SDL_BYTEORDER == SDL_BIG_ENDIAN
    constexpr uint32_t masks[] = {0xff000000, 0x00ff0000, 0x0000ff00, 0x000000ff};
#else
    constexpr uint32_t masks[] = {0x000000ff, 0x0000ff00, 0x00ff0000, 0xff000000};
#endif
    SDL_Surface* surface = SDL_CreateRGBSurface(0, width, height, 32, masks[0], masks[1], masks[2], masks[3]);
    if (surface) {
        for (int y = 0; y < height; ++y) {
            memcpy(static_cast<unsigned char*>(surface->pixels) + y * surface->pitch,
                   pixels + y * width * 4, width * 4);
        }
    }
    stbi_image_free(pixels);
    return surface ? surface : fail(error, "Create RGBA image surface", 0, SDL_GetError());
}

SDL_Surface* loadPreviewFile(const std::string& path, PreviewError* error) {
    FileInput file(path.c_str());
    if (!file) return fail(error, "Open preview file", file.error);
    // Read to EOF in bounded chunks rather than requiring a stat structure
    // or assuming every mounted application asset supports SEEK_END.
    std::string bytes;
    char buffer[16 * 1024];
    for (;;) {
        const auto count = file.readBytes(buffer, sizeof(buffer));
        if (file.error) return fail(error, "Read preview file", file.error);
        if (bytes.size() + count > maxPreviewBytes) return fail(error, "Preview file too large");
        bytes.append(buffer, count);
        if (count < sizeof(buffer)) break;
    }
    return decodePreview(bytes, error);
}
