#include "app/controller.hpp"
#include "library/identity.hpp"
#include "ui/books_list.hpp"
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
    Harness(const std::string& root, const std::string& assets, const std::string& executable, const std::string& book = {})
        : store(root, root + "/home"), worker(assets, root, executable), app(store, worker, book) {}
    ~Harness() { app.shutdown(); }
    void wait() {
        const auto until = std::chrono::steady_clock::now() + std::chrono::seconds(40);
        do { app.tick(clockMs()); std::this_thread::sleep_for(std::chrono::milliseconds(1)); }
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
        check(epub::clipUtf8("abcédef", 4) == "abc", "Persisted metadata clipping split UTF-8");
        std::cout << "PASS: production library/reader sessions, rename/missing/replaced targets, completion, cancellation, folders and native UI\n";
        return 0;
    } catch (const std::exception& e) { std::cerr << "FAIL: " << e.what() << '\n'; return 1; }
}
