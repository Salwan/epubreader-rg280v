#pragma once
#include "ui/theme.hpp"
#include <SDL.h>
#include <SDL_ttf.h>
#include <array>
#include <cstdint>
#include <string>
#include <vector>

namespace epub {
std::string bitmapText(const std::string& value, size_t cells = 4096);
std::string bitmapLine(const std::string& value, size_t cells = 4096);
// App-owned RGB888 canvas. Device presentation performs SDL format conversion.
class Canvas {
public:
    int width, height;
    std::vector<uint32_t> pixels;
    explicit Canvas(int w = 320, int h = 480);
    void clear(uint32_t color);
    void pixel(int x, int y, uint32_t color);
    void rect(int x, int y, int w, int h, uint32_t color);
    void outline(int x, int y, int w, int h, uint32_t color);
    // 5x14 native bitmap text is the device-selected UI font.
    void text(int x, int y, const std::string& value, uint32_t color, int rowScale = 2);
    void circle(int cx, int cy, int radius, uint32_t color);
    void ttf(int x, int y, const std::string& value, TTF_Font* font, uint32_t color);
    void transformFrom(const Canvas& source, bool horizontalReduction);
    SDL_Surface* surface(); // Caller frees the surface, pixels remain owned here.
    void saveBmp(const std::string& path);
};
}
