#include "ui/reader_slice.hpp"
#include <algorithm>

namespace epub {
void renderReaderSlice(Canvas& canvas, const ReaderResult& page, int theme,
                       const ReaderControls& controls, bool busy, const std::string& status,
                        const MusicSnapshot& music, const std::string& battery, bool warning) {
    const auto& palette = palettes[static_cast<size_t>(theme)];
    canvas.clear(palette.background);
    if (page.pixels.size() == canvas.pixels.size()) canvas.pixels = page.pixels;
    auto panel = [&](int y, int height) {
        for (int row = y; row < y + height; ++row) for (int x = 4; x < 316; ++x) {
            const uint32_t p = canvas.pixels[static_cast<size_t>(row * 320 + x)];
            const uint32_t c = palette.panel;
            canvas.pixel(x, row, ((((p & 0xff0000) + 7 * (c & 0xff0000)) / 8) & 0xff0000)
                | ((((p & 0xff00) + 7 * (c & 0xff00)) / 8) & 0xff00)
                | (((p & 0xff) + 7 * (c & 0xff)) / 8));
        }
    };
    auto line = [&](int y, const std::string& text) { canvas.text(10, y, bitmapLine(text, 50), palette.text); };
    if (controls.menuOpen() || page.pixels.empty()) {
        panel(8, 170);
        line(18, "EPUB READER " EPUBREADER_VERSION " / MILESTONE 5.5");
        line(44, page.title.empty() ? "OPENING BOOK" : page.title);
        line(64, page.author);
        line(86, page.path.substr(page.path.find_last_of('/') + 1));
        line(110, "PAGE " + std::to_string(page.page + 1) + " / " + std::to_string(page.pageCount) + " / " + palette.name);
        line(138, music.label());
        line(158, battery);
    }
    if (controls.menuOpen()) {
        panel(180, 210);
        const std::array<std::string, 4> rows{{"BACK TO BOOK LIST",
            std::string("FONT SIZE: ") + (page.font > 0 ? "< " : "  ")
            + std::array<const char*, 3>{{"SMALL", "MEDIUM", "LARGE"}}[static_cast<size_t>(page.font)]
            + (page.font < 2 ? " >" : "  "), std::string("MUSIC REPEAT: ") + (music.repeat != RepeatMode::All ? "< " : "  ")
            + repeatLabel(music.repeat) + (music.repeat != RepeatMode::Stop ? " >" : "  "), "EXIT APPLICATION"}};
        for (size_t item = 0; item < rows.size(); ++item) {
            const int y = 194 + static_cast<int>(item) * 28;
            if (item == static_cast<size_t>(controls.selected())) {
                canvas.outline(8, y - 6, 304, 26, palette.accent);
                canvas.text(12, y, ">", palette.accent);
            }
            canvas.text(26, y, rows[item], palette.text);
        }
        line(310, "UP/DOWN: SELECT   A: ACTIVATE");
        line(332, "FONT/REPEAT: LEFT/RIGHT TO ADJUST");
        line(354, "B OR X: CLOSE MENU");
        line(376, "SELECT+START: SAVE AND EXIT");
    } else if (page.pixels.empty()) {
        panel(180, 154);
        line(194, "X: OPEN MENU");
        line(222, "MENU: BACK / FONT / REPEAT / EXIT");
        line(250, "SELECT+START: SAVE AND EXIT");
    }
    if (controls.jumpOpen()) {
        panel(320, 148);
        canvas.outline(4, 320, 312, 148, palette.accent);
        line(330, "JUMP TO: " + std::to_string(controls.targetPercent()) + '%');
        line(352, "PAGE " + std::to_string(controls.targetPage() + 1) + " / " + std::to_string(controls.targetPageCount()));
        canvas.rect(14, 380, 292, 2, palette.muted);
        const int marker = 14 + 291 * controls.targetPercent() / 100;
        canvas.rect(std::clamp(marker - 1, 14, 303), 374, 3, 14, palette.accent);
        line(396, "LEFT/RIGHT: -/+1%   UP/DOWN: +/-10%");
        line(418, "A: JUMP   B/SELECT: CANCEL");
        line(440, "X: MENU   SELECT+START: SAVE AND EXIT");
        if (!status.empty()) { panel(270, 34); line(280, status); }
    } else if (busy || !status.empty() || (controls.menuOpen() && !music.detail.empty())) {
        panel(404, 64);
        line(412, busy ? "READER WORKING / SELECT+START EXITS" : "READER STATUS");
        line(438, status.empty() ? music.detail : status);
    }
    if (warning && !page.pixels.empty()) {
        constexpr uint32_t red = 0xe03030;
        canvas.rect(0, 0, 320, 2, red); canvas.rect(0, 476, 320, 2, red);
        canvas.rect(0, 2, 2, 474, red); canvas.rect(318, 2, 2, 474, red);
    }
    canvas.rect(0, 478, 320, 2, palette.background);
    canvas.rect(0, 478, 320 * std::clamp(page.progress, 0, 10000) / 10000, 2, palette.muted);
}
}
