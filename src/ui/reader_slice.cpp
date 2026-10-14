#include "ui/reader_slice.hpp"
#include <algorithm>

namespace epub {
void renderReaderSlice(Canvas& canvas, const ReaderResult& page, int theme,
                       const ReaderControls& controls, bool busy, const std::string& status) {
    const auto& palette = palettes[static_cast<size_t>(theme)];
    canvas.clear(palette.background);
    if (page.pixels.size() == canvas.pixels.size()) canvas.pixels = page.pixels;
    canvas.rect(0, 478, 320, 2, palette.background);
    canvas.rect(0, 478, 320 * std::clamp(page.progress, 0, 10000) / 10000, 2, palette.muted);
    auto panel = [&](int y, int height) {
        for (int row = y; row < y + height; ++row) for (int x = 4; x < 316; ++x) {
            const uint32_t p = canvas.pixels[static_cast<size_t>(row * 320 + x)];
            const uint32_t c = palette.panel;
            canvas.pixel(x, row, ((((p & 0xff0000) + 7 * (c & 0xff0000)) / 8) & 0xff0000)
                | ((((p & 0xff00) + 7 * (c & 0xff00)) / 8) & 0xff00)
                | (((p & 0xff) + 7 * (c & 0xff)) / 8));
        }
    };
    auto line = [&](int y, const std::string& text) { canvas.text(10, y, text.substr(0, 50), palette.text); };
    if (controls.menuOpen() || page.pixels.empty()) {
        panel(8, 154);
        line(18, "EPUB READER " EPUBREADER_VERSION " / MILESTONE 2");
        line(44, page.title.empty() ? "EPUB RENDERING VERTICAL SLICE" : page.title);
        line(64, page.author);
        line(86, page.path.substr(page.path.find_last_of('/') + 1));
        line(110, "PAGE " + std::to_string(page.page + 1) + " / " + std::to_string(page.pageCount) + " / " + palette.name);
        line(138, "JOB " + std::to_string(page.elapsedMs) + "MS / PAGE CACHE " + (page.pageCacheHit ? "HIT" : "MISS"));
    }
    if (controls.menuOpen()) {
        panel(180, 210);
        const std::array<std::string, 3> rows{{"REOPEN BOOK",
            std::string("FONT SIZE: ") + (page.font > 0 ? "< " : "  ")
            + std::array<const char*, 3>{{"SMALL", "MEDIUM", "LARGE"}}[static_cast<size_t>(page.font)]
            + (page.font < 2 ? " >" : "  "), "EXIT APPLICATION"}};
        for (size_t item = 0; item < rows.size(); ++item) {
            const int y = 194 + static_cast<int>(item) * 32;
            if (item == static_cast<size_t>(controls.selected())) {
                canvas.outline(8, y - 6, 304, 26, palette.accent);
                canvas.text(12, y, ">", palette.accent);
            }
            canvas.text(26, y, rows[item], palette.text);
        }
        line(302, "UP/DOWN: SELECT   A: ACTIVATE");
        line(324, "FONT SIZE: LEFT/RIGHT TO ADJUST");
        line(346, "B OR X: CLOSE MENU");
        line(368, "SELECT+START: SAVE AND EXIT");
    } else if (page.pixels.empty()) {
        panel(180, 154);
        line(194, "X: OPEN MENU   A: RETRY LOAD");
        line(222, "UP/DOWN: BUNDLED EPUB 2/3 SPECIMEN");
        line(250, "SELECT+START: SAVE AND EXIT");
    }
    if (busy || !status.empty()) {
        panel(404, 64);
        line(412, busy ? "READER WORKING / SELECT+START EXITS" : "READER STATUS");
        line(438, status);
    }
}
}
