#include "ui/canvas.hpp"
#include <algorithm>
#include <cctype>
#include <stdexcept>

namespace epub {
std::string bitmapText(const std::string& value, size_t cells) {
    std::string out;
    for (size_t i = 0; i < value.size() && out.size() < cells;) {
        const auto first = static_cast<unsigned char>(value[i++]);
        uint32_t cp = first;
        if (first >= 0x80) {
            int extra = first >= 0xc2 && first <= 0xdf ? 1 : first >= 0xe0 && first <= 0xef ? 2 : first >= 0xf0 && first <= 0xf4 ? 3 : 0;
            if (!extra || i + static_cast<size_t>(extra) > value.size()) cp = '?';
            else {
                cp = first & ((1u << (6 - extra)) - 1);
                bool valid = true;
                for (int n = 0; n < extra; ++n) {
                    const auto b = static_cast<unsigned char>(value[i]);
                    if ((b & 0xc0) != 0x80) { valid = false; break; }
                    ++i; cp = (cp << 6) | (b & 0x3f);
                }
                if (!valid || cp > 0x10ffff || (cp >= 0xd800 && cp <= 0xdfff) ||
                    (extra == 1 && cp < 0x80) || (extra == 2 && cp < 0x800) || (extra == 3 && cp < 0x10000)) cp = '?';
            }
        }
        char c = '?';
        if (cp < 128) c = cp == '\n' ? '\n' : cp >= 32 ? static_cast<char>(cp) : ' ';
        else if ((cp >= 0xc0 && cp <= 0xc6) || (cp >= 0xe0 && cp <= 0xe6)) c = 'A';
        else if (cp == 0xc7 || cp == 0xe7) c = 'C';
        else if ((cp >= 0xc8 && cp <= 0xcb) || (cp >= 0xe8 && cp <= 0xeb)) c = 'E';
        else if ((cp >= 0xcc && cp <= 0xcf) || (cp >= 0xec && cp <= 0xef)) c = 'I';
        else if (cp == 0xd1 || cp == 0xf1) c = 'N';
        else if ((cp >= 0xd2 && cp <= 0xd6) || (cp >= 0xf2 && cp <= 0xf6) || cp == 0xd8 || cp == 0xf8) c = 'O';
        else if ((cp >= 0xd9 && cp <= 0xdc) || (cp >= 0xf9 && cp <= 0xfc)) c = 'U';
        else if (cp == 0xdd || cp == 0xfd || cp == 0xff) c = 'Y';
        else if (cp == 0xdf) c = 'S';
        else if (cp == 0x2018 || cp == 0x2019) c = '\'';
        else if (cp == 0x201c || cp == 0x201d) c = '"';
        else if (cp >= 0x2010 && cp <= 0x2015) c = '-';
        else if (cp == 0x2026) c = '.';
        else if (cp == 0xa0) c = ' ';
        out += c;
    }
    return out;
}
const std::array<Palette, 6> palettes{{
    {"dark", 0x101419, 0xd8dce0, 0x92b6b0, 0x444f59, 0x202930},
    {"warm", 0x272019, 0xe5d2b3, 0xc8a16c, 0x655240, 0x3b3025},
    // Extra red bias compensates for the RG280V panel's cool appearance.
    {"amber", 0x1b1006, 0xed9038, 0xffa744, 0x714322, 0x352313},
    {"matrix", 0x07140b, 0x8dcc96, 0xa1eab0, 0x2d5939, 0x142b1b},
    {"light", 0xeee9dd, 0x292d30, 0x4b746e, 0xc0bbaa, 0xdbd5c7},
    // Warmer paper and matching panel, with the same dark-brown ink.
    {"warm2", 0xffd0a0, 0x35271d, 0x9b6333, 0xc39a6a, 0xf1c396}
}};

// Original 5x7 diagnostic glyphs, columns top-bit first. Lowercase folds to
// uppercase deliberately; the TrueType specimen demonstrates mixed case.
static const uint8_t glyphs[][5] = {
    {0,0,0,0,0}, {0,0,0x5f,0,0}, {0,7,0,7,0}, {0x14,0x7f,0x14,0x7f,0x14},
    {0x24,0x2a,0x7f,0x2a,0x12}, {0x23,0x13,8,0x64,0x62}, {0x36,0x49,0x55,0x22,0x50}, {0,5,3,0,0},
    {0,0x1c,0x22,0x41,0}, {0,0x41,0x22,0x1c,0}, {0x14,8,0x3e,8,0x14}, {8,8,0x3e,8,8},
    {0,0x50,0x30,0,0}, {8,8,8,8,8}, {0,0x60,0x60,0,0}, {0x20,0x10,8,4,2},
    {0x3e,0x51,0x49,0x45,0x3e}, {0,0x42,0x7f,0x40,0}, {0x42,0x61,0x51,0x49,0x46}, {0x21,0x41,0x45,0x4b,0x31},
    {0x18,0x14,0x12,0x7f,0x10}, {0x27,0x45,0x45,0x45,0x39}, {0x3c,0x4a,0x49,0x49,0x30}, {1,0x71,9,5,3},
    {0x36,0x49,0x49,0x49,0x36}, {6,0x49,0x49,0x29,0x1e}, {0,0x36,0x36,0,0}, {0,0x56,0x36,0,0},
    {8,0x14,0x22,0x41,0}, {0x14,0x14,0x14,0x14,0x14}, {0,0x41,0x22,0x14,8}, {2,1,0x51,9,6},
    {0x3e,0x41,0x5d,0x55,0x1e}, {0x7e,9,9,9,0x7e}, {0x7f,0x49,0x49,0x49,0x36}, {0x3e,0x41,0x41,0x41,0x22},
    {0x7f,0x41,0x41,0x22,0x1c}, {0x7f,0x49,0x49,0x49,0x41}, {0x7f,9,9,9,1}, {0x3e,0x41,0x49,0x49,0x7a},
    {0x7f,8,8,8,0x7f}, {0,0x41,0x7f,0x41,0}, {0x20,0x40,0x41,0x3f,1}, {0x7f,8,0x14,0x22,0x41},
    {0x7f,0x40,0x40,0x40,0x40}, {0x7f,2,0x0c,2,0x7f}, {0x7f,4,8,0x10,0x7f}, {0x3e,0x41,0x41,0x41,0x3e},
    {0x7f,9,9,9,6}, {0x3e,0x41,0x51,0x21,0x5e}, {0x7f,9,0x19,0x29,0x46}, {0x46,0x49,0x49,0x49,0x31},
    {1,1,0x7f,1,1}, {0x3f,0x40,0x40,0x40,0x3f}, {0x1f,0x20,0x40,0x20,0x1f}, {0x3f,0x40,0x38,0x40,0x3f},
    {0x63,0x14,8,0x14,0x63}, {7,8,0x70,8,7}, {0x61,0x51,0x49,0x45,0x43}, {0,0x7f,0x41,0x41,0},
    {2,4,8,0x10,0x20}, {0,0x41,0x41,0x7f,0}, {4,2,1,2,4}, {0x40,0x40,0x40,0x40,0x40}, {0,1,2,4,0}
};

Canvas::Canvas(int w, int h) : width(w), height(h), pixels(static_cast<size_t>(w * h)) {}
void Canvas::clear(uint32_t c) { std::fill(pixels.begin(), pixels.end(), c); }
void Canvas::pixel(int x, int y, uint32_t c) {
    if (x >= 0 && x < width && y >= 0 && y < height) pixels[static_cast<size_t>(y * width + x)] = c;
}
void Canvas::rect(int x, int y, int w, int h, uint32_t c) {
    for (int yy = std::max(0, y); yy < std::min(height, y + h); ++yy)
        for (int xx = std::max(0, x); xx < std::min(width, x + w); ++xx) pixel(xx, yy, c);
}
void Canvas::outline(int x, int y, int w, int h, uint32_t c) {
    rect(x,y,w,1,c); rect(x,y+h-1,w,1,c); rect(x,y,1,h,c); rect(x+w-1,y,1,h,c);
}
void Canvas::text(int x, int y, const std::string& s, uint32_t c, int scale) {
    const int start = x;
    for (unsigned char ch : bitmapText(s)) {
        if (ch == '\n') { x = start; y += 9 * scale; continue; }
        ch = static_cast<unsigned char>(std::toupper(ch));
        if (ch < 32 || ch > 96) ch = '?';
        for (int column = 0; column < 5; ++column)
            for (int row = 0; row < 7; ++row)
                if ((glyphs[ch - 32][column] >> row) & 1)
                    rect(x + column, y + row * scale, 1, scale, c);
        x += 6;
    }
}
void Canvas::circle(int cx, int cy, int r, uint32_t c) {
    int x = r, y = 0, error = 1 - r;
    while (x >= y) {
        pixel(cx+x,cy+y,c); pixel(cx+y,cy+x,c); pixel(cx-x,cy+y,c); pixel(cx-y,cy+x,c);
        pixel(cx+x,cy-y,c); pixel(cx+y,cy-x,c); pixel(cx-x,cy-y,c); pixel(cx-y,cy-x,c);
        ++y;
        if (error < 0) error += 2*y+1;
        else { --x; error += 2*(y-x)+1; }
    }
}
SDL_Surface* Canvas::surface() {
    return SDL_CreateRGBSurfaceFrom(pixels.data(), width, height, 32, width * 4, 0xff0000, 0xff00, 0xff, 0);
}
void Canvas::ttf(int x, int y, const std::string& s, TTF_Font* font, uint32_t color) {
    SDL_Color c{static_cast<Uint8>(color >> 16), static_cast<Uint8>(color >> 8), static_cast<Uint8>(color), 0};
    SDL_Surface* line = TTF_RenderUTF8_Blended(font, s.c_str(), c);
    if (!line) throw std::runtime_error(TTF_GetError());
    SDL_Surface* dst = surface();
    SDL_Rect pos{static_cast<Sint16>(x), static_cast<Sint16>(y), 0, 0};
    SDL_BlitSurface(line, nullptr, dst, &pos);
    SDL_FreeSurface(dst); SDL_FreeSurface(line);
}
void Canvas::transformFrom(const Canvas& source, bool horizontal) {
    for (int y = 0; y < height; ++y) for (int x = 0; x < width; ++x) {
        if (!horizontal) pixel(x, y, source.pixels[static_cast<size_t>((y/2)*source.width+x)]);
        else {
            const uint32_t a = source.pixels[static_cast<size_t>(y*source.width+x*2)];
            const uint32_t b = source.pixels[static_cast<size_t>(y*source.width+x*2+1)];
            pixel(x,y, (((a & 0xfefefe) + (b & 0xfefefe)) >> 1) + (a & b & 0x010101));
        }
    }
}
void Canvas::saveBmp(const std::string& path) {
    SDL_Surface* s = surface();
    const int result = SDL_SaveBMP(s, path.c_str()); SDL_FreeSurface(s);
    if (result != 0) throw std::runtime_error(SDL_GetError());
}
}
