#include "ui/books_list.hpp"
#include "ui/reader_slice.hpp"
#include "library/identity.hpp"
#include <algorithm>
#include <array>

namespace epub {
namespace {
void scrollbar(Canvas& c, const Palette& p, int total, int top) {
    if (total <= libraryRows) return;
    constexpr int height = libraryRows * 20;
    c.rect(318, 64, 2, height, p.panel);
    const int thumb = std::max(4, height * libraryRows / total);
    c.rect(318, 64 + (height - thumb) * top / (total - libraryRows), 2, thumb, p.accent);
}
}
void renderApplication(Canvas& canvas, const Controller& app) {
    const auto& config = app.config();
    const auto& p = palettes[static_cast<size_t>(config.preferences.theme)];
    if (app.screen() == Screen::Reader || app.screen() == Screen::Opening) {
        renderReaderSlice(canvas, app.page, config.preferences.theme, app.readerControls, app.busy(), app.notice.empty() ? app.status : app.notice, app.music());
        return;
    }
    canvas.clear(p.background);
    auto line = [&](int x, int y, const std::string& text, size_t cells = 51) { canvas.text(x, y, bitmapLine(text, cells), p.text); };
    const bool chooser = app.screen() == Screen::FolderChooser;
    auto pathLabel = [](const std::string& path) {
        auto text = bitmapLine(path);
        return text.size() > 51 ? "..." + text.substr(text.size() - 48) : text;
    };
    line(6, 10, chooser ? "CHOOSE CONTENT FOLDER" : "EPUB READER " EPUBREADER_VERSION " / BOOKS");
    line(6, 34, chooser ? pathLabel(app.folderPath) : std::string("THEME: ") + p.name + "   FONT: " + std::array<const char*, 3>{{"SMALL", "MEDIUM", "LARGE"}}[static_cast<size_t>(config.preferences.font)]);
    canvas.rect(4, 56, 312, 1, p.muted);
    const int count = chooser ? static_cast<int>(app.folders.size()) + 2 : static_cast<int>(app.catalog.entries.size());
    const int top = chooser ? app.folderTop : app.catalog.top(), selected = chooser ? app.folderRow : app.catalog.selected();
    for (int row = 0; row < libraryRows && top + row < count; ++row) {
        const int index = top + row, y = 66 + row * 20;
        if (index == selected) {
            canvas.rect(3, y - 2, 312, 19, p.panel); canvas.outline(3, y - 2, 312, 19, p.accent); line(5, y, ">");
        }
        if (chooser) line(16, y, index == 0 ? "[ USE THIS FOLDER ]" : index == 1 ? "../" : app.folders[static_cast<size_t>(index - 2)].title + '/', 49);
        else {
            const auto& e = app.catalog.entries[static_cast<size_t>(index)];
            line(16, y, e.title.empty() ? baseName(e.path) : e.title, 40);
            line(264, y, std::to_string(std::max(0, e.progress) / 100) + '%', 4);
            if (e.finished) {
                for (int d = 0; d < 3; ++d) canvas.rect(300 + d, y + 6 + d * 2, 1, 2, p.accent);
                for (int d = 0; d < 5; ++d) canvas.rect(302 + d, y + 10 - d * 2, 1, 2, p.accent);
            } else if (!e.error.empty()) line(302, y, "!");
        }
    }
    scrollbar(canvas, p, count, std::min(top, std::max(0, count - libraryRows)));
    if (!chooser && count == 0) {
        line(10, 104, app.busy() ? "SCANNING FOR EPUB BOOKS..." : "NO BOOKS AVAILABLE");
        line(10, 136, "X: CHOOSE BOOKS FOLDER OR RESCAN");
        line(10, 168, pathLabel(app.config().books), 49);
    }
    if (!chooser) if (const auto* e = app.catalog.current()) line(6, 414, baseName(e->path), 51);
    std::string status = app.notice.empty() ? app.status : app.notice;
    if (!chooser && app.notice.empty()) if (const auto* e = app.catalog.current(); e && !e->error.empty()) status = e->error;
    line(6, 438, status, 51);
    line(6, 460, chooser ? "A: OPEN/USE  B: CANCEL  SELECT+START: EXIT" : "A: READ  X: SETTINGS  SELECT+START: EXIT");
    if (app.libraryMenu) {
        canvas.rect(12, 140, 296, 188, p.panel); canvas.outline(12, 140, 296, 188, p.accent);
        const std::array<const char*, 4> rows{{"RESCAN LIBRARY", "BOOKS FOLDER", "MUSIC FOLDER", "EXIT APPLICATION"}};
        for (size_t i = 0; i < rows.size(); ++i) {
            const int y = 154 + static_cast<int>(i) * 32;
            if (app.menuRow == static_cast<int>(i)) { canvas.outline(18, y - 4, 284, 24, p.accent); line(22, y, ">"); }
            line(36, y, rows[i], 44);
        }
        line(24, 298, "A: SELECT   B/X: CLOSE", 45);
    }
}
}
