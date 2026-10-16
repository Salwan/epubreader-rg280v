#include "app/controller.hpp"
#include "library/identity.hpp"
#include "ui/books_list.hpp"
#include "ui/reader_slice.hpp"
#include <algorithm>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <thread>

namespace {
void check(bool ok, const std::string& message) { if (!ok) throw std::runtime_error(message); }
uint32_t clockMs() {
    return static_cast<uint32_t>(std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now().time_since_epoch()).count());
}
struct Harness {
    epub::Store store;
    epub::ReaderWorker worker;
    epub::Controller app;
    epub::Input input;
    uint32_t inputNow = 1000;
    Harness(const std::string& root, const std::string& assets, const std::string& executable, const std::string& book = {})
        : store(root, root + "/home"), worker(assets, root, executable), app(store, worker, book) {}
    ~Harness() { app.shutdown(); }
    void dispatch(epub::Action action) { app.action(action); app.consumeTransition(input); }
    void key(SDLKey key, bool down) { dispatch(input.key(key, down, inputNow)); }
    void tap(SDLKey symbol) { inputNow += 10; key(symbol, true); inputNow += 40; key(symbol, false); }
    void wait() {
        const auto until = std::chrono::steady_clock::now() + std::chrono::seconds(40);
        do { app.tick(clockMs()); app.consumeTransition(input); std::this_thread::sleep_for(std::chrono::milliseconds(1)); }
        while (app.busy() && std::chrono::steady_clock::now() < until);
        check(!app.busy(), "Controller timeout: " + app.status + ' ' + app.notice);
    }
    void select(const std::string& path) {
        check(app.screen() == epub::Screen::Library, "Selection outside library");
        const auto i = std::find_if(app.catalog.entries.begin(), app.catalog.entries.end(), [&](const auto& e) { return e.path == path; });
        check(i != app.catalog.entries.end(), "Book not listed: " + path);
        const int target = static_cast<int>(i - app.catalog.entries.begin());
        while (app.catalog.selected() != target) app.action(app.catalog.selected() < target ? epub::Action::Down : epub::Action::Up);
    }
    void open(const std::string& path) { select(path); app.action(epub::Action::AShort); wait(); check(app.screen() == epub::Screen::Reader, "Open failed: " + app.notice); }
    void back() { app.action(epub::Action::X); app.action(epub::Action::AShort); wait(); check(app.screen() == epub::Screen::Library, "Back to list failed"); }
};
void configure(const std::string& root, const std::string& folder) {
    epub::Store store(root, root + "/home"); store.config.books = folder; store.saveConfig(); store.flush();
}
void copy(const std::string& source, const std::string& target) { std::filesystem::copy_file(source, target, std::filesystem::copy_options::overwrite_existing); }
void target(Harness& h, int percent) {
    if (!h.app.readerControls.jumpOpen()) h.tap(SDLK_ESCAPE);
    check(h.app.readerControls.jumpOpen(), "Slider unavailable");
    for (int n = 0; n < 20; ++n) h.dispatch(epub::Action::Down);
    for (int n = 0; n < percent / 10; ++n) h.dispatch(epub::Action::Up);
    for (int n = 0; n < percent % 10; ++n) h.dispatch(epub::Action::Right);
    check(h.app.readerControls.targetPercent() == percent, "Target adjustment failed");
}
void checkBar(const epub::Canvas& canvas, const epub::ReaderResult& page, int theme) {
    const auto& palette = epub::palettes[static_cast<size_t>(theme)];
    const int fill = 320 * std::clamp(page.progress, 0, 10000) / 10000;
    for (int x = 0; x < 320; ++x) {
        check(canvas.pixels[static_cast<size_t>(477 * 320 + x)] == page.pixels[static_cast<size_t>(477 * 320 + x)], "Overlay/bar entered row 477");
        for (int y : {478, 479}) check(canvas.pixels[static_cast<size_t>(y * 320 + x)] == (x < fill ? palette.muted : palette.background), "Native progress bar dimensions/fill");
    }
}
void jumpIntegration(const std::string& root, const std::string& assets, const std::string& executable, const std::string& shots) {
    const auto book = assets + "/books/prose-epub3.epub";
    std::string origin, jumped, identity; int jumpPage = 0;
    epub::Canvas canvas;
    auto shot = [&](Harness& h, const std::string& name) {
        epub::renderApplication(canvas, h.app); checkBar(canvas, h.app.page, h.app.config().preferences.theme);
        canvas.saveBmp(shots + '/' + name + ".bmp");
    };
    {
        Harness h(root, assets, executable, book);
        h.tap(SDLK_ESCAPE); check(!h.app.readerControls.jumpOpen(), "Slider opened before initial page"); h.wait();
        for (int n = 0; n < 4; ++n) { h.tap(SDLK_RIGHT); h.wait(); }
        origin = h.app.page.anchor; identity = h.app.page.identity;
        epub::renderApplication(canvas, h.app);
        check(std::equal(canvas.pixels.begin(), canvas.pixels.begin() + 478 * 320, h.app.page.pixels.begin()), "Normal reading has an overlay");
        checkBar(canvas, h.app.page, h.app.config().preferences.theme);
        auto longPage = h.app.page;
        longPage.title = std::string(80, 'T') + " Café — title\nsecond line";
        longPage.author = ""; longPage.path = '/' + std::string(200, 'F') + ".epub";
        epub::ReaderControls menu; menu.showMenu();
        epub::renderReaderSlice(canvas, longPage, 0, menu, true, std::string(100, 'S') + "\nstatus");
        checkBar(canvas, longPage, 0); canvas.saveBmp(shots + "/reader-long-metadata.bmp");
        // No metadata/status glyph may escape its panel into native edge columns.
        for (int y = 0; y < 478; ++y) for (int x : {0, 1, 2, 3, 316, 317, 318, 319})
            check(canvas.pixels[static_cast<size_t>(y * 320 + x)] == longPage.pixels[static_cast<size_t>(y * 320 + x)], "Metadata clipped outside reader panels");
        const auto request = h.app.page.requestId;
        h.tap(SDLK_ESCAPE); shot(h, "jump-origin");
        h.tap(SDLK_a); check(!h.worker.busy() && h.app.page.requestId == request && h.app.page.anchor == origin, "Untouched confirmation changed reader state");
        // A spanning a transition is consumed using the production helper.
        h.inputNow += 10; h.key(SDLK_a, true); h.tap(SDLK_ESCAPE);
        h.inputNow += 40; h.key(SDLK_a, false);
        check(h.app.readerControls.jumpOpen() && !h.worker.busy(), "Held A leaked into newly opened slider");
        h.inputNow += 10; h.key(SDLK_UP, true);
        const int afterPress = h.app.readerControls.targetPercent();
        h.key(SDLK_UP, true); check(h.app.readerControls.targetPercent() == afterPress, "Firmware repeat duplicated slider movement");
        h.inputNow += 349; h.dispatch(h.input.tick(h.inputNow));
        check(h.app.readerControls.targetPercent() == afterPress, "Slider repeat fired early");
        ++h.inputNow; h.dispatch(h.input.tick(h.inputNow));
        check(h.app.readerControls.targetPercent() == std::min(100, afterPress + 10), "Slider repeat threshold");
        h.tap(SDLK_x); check(h.app.readerControls.menuOpen() && !h.app.readerControls.jumpOpen(), "Jump-to-menu transition");
        h.inputNow += 1000; h.dispatch(h.input.tick(h.inputNow));
        check(h.app.readerControls.selected() == epub::ReaderMenuItem::Back, "Held jump direction leaked into menu");
        h.tap(SDLK_b); h.dispatch(h.input.tick(h.inputNow + 1000));
        check(h.app.page.anchor == origin && !h.worker.busy(), "Held menu input leaked into reader");
        h.input.reset(); h.tap(SDLK_ESCAPE); target(h, 50);
        h.inputNow += 10; h.key(SDLK_a, true); h.inputNow += 600; h.dispatch(h.input.tick(h.inputNow));
        h.inputNow += 600; h.dispatch(h.input.tick(h.inputNow)); h.key(SDLK_a, false);
        check(h.app.readerControls.jumpOpen() && !h.worker.busy(), "Long A committed slider");
        // A focus/suspend reset preserves the preview but consumes old releases.
        h.key(SDLK_a, true); h.input.reset(); h.key(SDLK_a, false);
        check(h.app.readerControls.targetPercent() == 50 && !h.worker.busy(), "Focus reset changed/committed preview");
        h.inputNow = 0xffffff00U; h.key(SDLK_UP, true); const int beforeWrap = h.app.readerControls.targetPercent();
        h.dispatch(h.input.tick(94)); check(h.app.readerControls.targetPercent() == beforeWrap + 10, "Slider repeat tick wraparound");
        h.input.reset(); h.inputNow = 10000;
        target(h, 0); shot(h, "jump-zero");
        target(h, 100); shot(h, "jump-hundred");
        h.tap(SDLK_b); check(h.app.page.anchor == origin && !h.worker.busy(), "Preview cancellation changed anchor");
        target(h, 50); shot(h, "jump-middle");
        h.app.notice = "Persistent notice must not cover jump controls";
        shot(h, "jump-notice");
        check(canvas.pixels[320 * 320 + 4] == epub::palettes[0].accent, "Notice overpainted slider border"); h.app.notice.clear();
        const auto start = std::chrono::steady_clock::now();
        for (int n = 0; n < 1000; ++n) h.dispatch(n % 2 ? epub::Action::Left : epub::Action::Right);
        std::cout << "PREVIEW 1000 local adjustments us=" << std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now() - start).count() << '\n';
        check(h.app.page.requestId == request && !h.worker.busy() && h.app.page.anchor == origin, "Preview submitted reader work");
        jumpPage = h.app.readerControls.targetPage();
        h.tap(SDLK_a); check(h.worker.busy() && !h.app.readerControls.jumpOpen(), "Confirmed jump not submitted/closed");
        h.tap(SDLK_a); h.tap(SDLK_ESCAPE); check(!h.app.readerControls.jumpOpen(), "Busy navigation queued another overlay");
        h.wait();
        check(h.app.page.page == jumpPage && h.app.page.requestId == request + 1, "Jump preview/commit mismatch or duplicate submission");
        jumped = h.app.page.anchor;
        h.tap(SDLK_x); h.tap(SDLK_DOWN); h.tap(SDLK_RIGHT); h.wait(); h.tap(SDLK_LEFT); h.wait();
        check(h.app.page.anchor == jumped && h.app.page.page == jumpPage, "Font cycle after jump drifted"); h.tap(SDLK_b);
    }
    {
        Harness h(root, assets, executable); h.wait();
        check(h.app.page.page == jumpPage && h.app.page.anchor == jumped, "Restart after confirmed jump lost bookmark");
        target(h, 100); h.tap(SDLK_ESCAPE);
        check(h.app.page.anchor == jumped && !h.store.book(identity).finished, "Canceling final preview completed book");
        target(h, 100); h.key(SDLK_RETURN, true); h.key(SDLK_ESCAPE, true);
        check(h.app.quit() && h.app.page.anchor == jumped, "Quit chord committed preview");
    }
    {
        Harness h(root, assets, executable); h.wait();
        check(h.app.page.anchor == jumped, "Quit with preview changed saved position");
        target(h, 100); h.tap(SDLK_a); h.tap(SDLK_x); h.tap(SDLK_a); h.wait();
        check(h.app.screen() == epub::Screen::Library, "Back during jump failed");
    }
    {
        Harness h(root, assets, executable, book); h.wait();
        check(h.app.page.anchor == jumped && !h.store.book(identity).finished, "Canceled jump saved an unfinished result");
        target(h, 100); h.tap(SDLK_a); h.wait();
        check(h.app.page.page == h.app.page.pageCount - 1 && h.app.page.progress == 10000, "Final jump failed");
        h.tap(SDLK_LEFT); h.wait();
        h.app.tick(clockMs() + 250); h.store.flush();
        check(h.store.book(identity).finished, "Jump completion was not sticky");
        const auto beforeFailure = h.app.page.anchor;
        h.worker.stop(); target(h, 0); h.tap(SDLK_a);
        check(h.app.readerControls.menuOpen() && !h.app.notice.empty() && h.app.page.anchor == beforeFailure, "Failed jump lost displayed anchor/recovery menu");
        h.tap(SDLK_a); h.wait(); check(h.app.screen() == epub::Screen::Library, "Worker-failure recovery to list");
    }
    for (bool reverse : {false, true}) for (const std::string context : {"library", "library-menu", "chooser", "opening", "reader", "menu", "jump", "font-busy", "jump-busy"}) {
        const bool library = context == "library" || context == "library-menu" || context == "chooser";
        Harness h(root + "/quit-" + context + (reverse ? "-reverse" : ""), assets, executable, library ? "" : book);
        if (context != "opening") h.wait();
        if (context == "library-menu" || context == "chooser" || context == "menu" || context == "font-busy") h.tap(SDLK_x);
        if (context == "chooser") { h.tap(SDLK_DOWN); h.tap(SDLK_a); h.wait(); check(h.app.screen() == epub::Screen::FolderChooser, "Quit chooser setup"); }
        if (context == "font-busy") { h.tap(SDLK_DOWN); h.tap(SDLK_RIGHT); check(h.worker.busy(), "Quit font-busy setup"); }
        if (context == "jump" || context == "jump-busy") target(h, 50);
        if (context == "jump-busy") { h.tap(SDLK_a); check(h.worker.busy(), "Quit jump-busy setup"); }
        h.key(reverse ? SDLK_RETURN : SDLK_ESCAPE, true); h.inputNow += 10;
        h.key(reverse ? SDLK_ESCAPE : SDLK_RETURN, true);
        check(h.app.quit(), "Quit chord unavailable in " + context);
    }
    std::cout << "PASS: production slider previews, hold/repeat/focus/chords, commits, cancellation, restart, completion and failure recovery\n";
}
}
int main(int argc, char** argv) {
    try {
        check(argc == 6, "Expected assets, fixtures, isolated state, executable, screenshots");
        const std::string assets = argv[1], fixtures = argv[2], root = argv[3], executable = argv[4], shots = argv[5];
        std::filesystem::remove_all(root); epub::ensureDirectory(root + "/library"); epub::ensureDirectory(shots);
        const auto folder = root + "/library", state = root + "/state";
        const auto book2 = folder + "/Café book 2.EPUB", book3 = folder + "/Book 3.epub", one = folder + "/one.epub";
        copy(assets + "/books/prose-epub2.epub", book2); copy(assets + "/books/prose-epub3.epub", book3); copy(fixtures + "/one-page.epub", one);
        copy(fixtures + "/no-spine.epub", folder + "/invalid-spine.epub");
        std::ofstream(folder + "/broken.epub") << "not ZIP";
        for (int n = 0; n < 22; ++n) copy(fixtures + "/one-page.epub", folder + "/copy " + std::to_string(n) + ".epub");
        configure(state, folder);
        std::string anchor, id2, id3; int pageNumber = 0;
        epub::Canvas canvas;
        auto shot = [&](Harness& h, const std::string& name) { epub::renderApplication(canvas, h.app); canvas.saveBmp(shots + '/' + name + ".bmp"); };
        {
            Harness h(state, assets, executable); h.wait();
            check(h.app.screen() == epub::Screen::Library && h.app.catalog.entries.size() == 27, "Fresh library population");
            check(h.app.inspections() == 27 && h.app.cacheHits() == 0, "Cold indexing counters");
            shot(h, "library-dark");
            int columns = 0;
            for (int x = 316; x < 320; ++x) if (canvas.pixels[static_cast<size_t>(64 * 320 + x)] != epub::palettes[0].background) ++columns;
            check(columns == 2, "List scrollbar not exactly two native columns");
            h.app.action(epub::Action::Right); shot(h, "library-scrolled");
            h.open(book2); id2 = h.app.page.identity;
            for (int n = 0; n < 4; ++n) { h.app.action(epub::Action::Right); h.wait(); }
            anchor = h.app.page.anchor; pageNumber = h.app.page.page;
            // Font changes use the production popup, retaining its selected row.
            h.app.action(epub::Action::X); h.app.action(epub::Action::Down);
            for (const auto action : {epub::Action::Right, epub::Action::Left, epub::Action::Left, epub::Action::Right}) {
                h.app.action(action); h.wait(); check(h.app.page.anchor == anchor, "Controller font-anchor drift");
                check(h.app.readerControls.menuOpen() && h.app.readerControls.selected() == epub::ReaderMenuItem::FontSize, "Font row dismissed/moved");
            }
            shot(h, "reader-popup"); h.app.action(epub::Action::B);
            // Back and immediate reopen must flush the most recent displayed page.
            h.back(); check(h.app.inspections() == 0 && h.app.cacheHits() == 27, "Warm library re-inspected unchanged books");
            h.open(book2); check(h.app.page.anchor == anchor && h.app.page.page == pageNumber, "Immediate reopen lost un-debounced checkpoint");
        }
        {
            Harness h(state, assets, executable); h.wait();
            check(h.app.screen() == epub::Screen::Reader && h.app.page.identity == id2 && h.app.page.anchor == anchor, "Startup reader resume");
            h.back(); h.open(book3); id3 = h.app.page.identity;
            check(id2 != id3 && h.app.page.page == 0, "Independent books shared position");
            h.app.action(epub::Action::Right); h.wait();
            h.back(); h.open(book2); check(h.app.page.anchor == anchor, "Second book overwrote first position");
        }
        const auto renamed = folder + "/renamed book.epub";
        std::filesystem::rename(book2, renamed);
        {
            Harness h(state, assets, executable); h.wait();
            check(h.app.screen() == epub::Screen::Reader && h.app.page.path == renamed && h.app.page.anchor == anchor, "Renamed startup resume");
            h.back(); h.open(one);
            const auto singleId = h.app.page.identity;
            check(h.app.page.pageCount == 1 && h.app.page.progress == 10000 && !h.store.book(singleId).finished, "One-page progress/completion on load");
            target(h, 0); h.tap(SDLK_a);
            check(!h.worker.busy() && !h.store.book(singleId).finished, "One-page slider changed completion");
            h.app.action(epub::Action::Right); h.wait(); h.back();
            check(h.store.book(singleId).finished, "One-page forward attempt did not complete");
            h.select(one); check(h.app.catalog.current()->finished, "Completed row not updated"); shot(h, "library-finished");
            h.open(renamed);
            check(h.worker.submit({epub::ReaderCommand::Page, h.app.page.pageCount - 2}), "Near-final navigation submit"); h.wait();
            h.app.action(epub::Action::Right); h.wait();
            check(h.app.page.progress == 10000, "Final page progress must reach 10000");
            h.app.action(epub::Action::Left); h.wait(); h.back();
            check(h.store.book(id2).finished && h.store.book(id2).page < h.store.book(id2).pageCount - 1, "Sticky completion lost before debounce");
            // Invalid spine metadata is useful but does not prove readability.
            h.select(folder + "/invalid-spine.epub"); h.app.action(epub::Action::AShort); h.wait();
            check(h.app.screen() == epub::Screen::Library && !h.app.notice.empty(), "Malformed full-open recovery");
            h.open(book3); check(h.app.page.page == 1, "Failed open corrupted a different book");
            // Cancel a submitted re-pagination through Back to list.
            h.app.action(epub::Action::X); h.app.action(epub::Action::Down); h.app.action(epub::Action::Right);
            h.app.action(epub::Action::Up); h.app.action(epub::Action::AShort); h.wait();
            check(h.app.screen() == epub::Screen::Library, "Back during font pagination failed");
            h.app.action(epub::Action::R2); h.app.action(epub::Action::R2); h.app.action(epub::Action::R2); h.app.action(epub::Action::R2); h.app.action(epub::Action::R2);
            shot(h, "library-warm2");
            for (const auto& palette : epub::palettes) {
                while (std::string(epub::palettes[static_cast<size_t>(h.app.config().preferences.theme)].name) != palette.name) h.app.action(epub::Action::R2);
                const std::string prefix = std::string("interface-") + palette.name;
                shot(h, prefix + "-list");
                h.app.action(epub::Action::X); shot(h, prefix + "-library-menu");
                h.app.action(epub::Action::Down); h.app.action(epub::Action::AShort); h.wait(); shot(h, prefix + "-chooser");
                h.app.action(epub::Action::B); h.wait();
            }
            h.app.action(epub::Action::X); h.app.action(epub::Action::Down); h.app.action(epub::Action::AShort); h.wait();
            check(h.app.screen() == epub::Screen::FolderChooser, "Books folder menu"); shot(h, "folder-chooser");
            h.app.action(epub::Action::AShort); h.wait(); check(h.app.screen() == epub::Screen::Library && h.store.config.books == folder, "Folder setting commit");
        }
        {
            Harness h(state, assets, executable); h.wait();
            check(h.app.screen() == epub::Screen::Library && h.store.config.preferences.theme == 5, "List startup/theme persistence");
            h.open(renamed);
        }
        // A different book at the saved path must not be silently resumed.
        copy(fixtures + "/one-page.epub", renamed);
        {
            Harness h(state, assets, executable); h.wait();
            check(h.app.screen() == epub::Screen::Library && !h.app.notice.empty(), "Replaced resume target opened wrong content");
            h.open(book3);
        }
        std::filesystem::remove(book3);
        {
            Harness h(state, assets, executable); h.wait();
            check(h.app.screen() == epub::Screen::Library && !h.app.notice.empty(), "Missing resume target recovery");
            check(h.store.book(id2).finished && h.store.book(id3).page == 1, "Missing/replaced files discarded durable state");
        }
        const auto empty = root + "/empty"; epub::ensureDirectory(empty); configure(root + "/empty-state", empty);
        {
            Harness h(root + "/empty-state", assets, executable); h.wait(); shot(h, "library-empty");
            h.app.action(epub::Action::AShort); check(h.app.screen() == epub::Screen::Library, "Empty library opened book");
            h.app.action(epub::Action::X); h.app.action(epub::Action::Down); h.app.action(epub::Action::AShort); h.wait();
            // Parent row navigates to root; select a real child directory and cancel.
            h.app.action(epub::Action::Down); h.app.action(epub::Action::AShort); h.wait();
            check(h.app.folderPath == root, "Folder parent navigation");
            h.app.action(epub::Action::B); h.wait();
            check(h.store.config.books == empty, "Cancelled chooser changed settings");
        }
        configure(root + "/missing-state", root + "/missing");
        {
            Harness h(root + "/missing-state", assets, executable); h.wait(); shot(h, "library-unavailable");
            check(!h.app.notice.empty(), "Missing directory lacks status");
            h.app.action(epub::Action::Quit); check(h.app.quit(), "Global quit unavailable in error state");
        }
        check(epub::bitmapText("Café — ‘test’ 文", 40) == "CafE - 'test' ?", "UTF-8 bitmap conversion");
        check(epub::bitmapText("abc文def", 4) == "abc?", "UTF-8 clipping split a codepoint");
        check(epub::bitmapLine("Café\nsecond\rline", 40) == "CafE second line", "Single-line UI metadata contains control breaks");
        check(epub::clipUtf8("abcédef", 4) == "abc", "Persisted metadata clipping split UTF-8");
        jumpIntegration(root + "/jump-state", assets, executable, shots);
        std::cout << "PASS: production library/reader sessions, rename/missing/replaced targets, completion, cancellation, folders and native UI\n";
        return 0;
    } catch (const std::exception& e) { std::cerr << "FAIL: " << e.what() << '\n'; return 1; }
}
