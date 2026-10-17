#include "app/controller.hpp"
#include "reader/engine.hpp"
#include "ui/books_list.hpp"
#include <algorithm>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <thread>

namespace {
void check(bool ok, const std::string& message) { if (!ok) throw std::runtime_error(message); }
uint32_t clockMs() {
    return static_cast<uint32_t>(std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now().time_since_epoch()).count());
}
struct Harness {
    epub::Store store;
    epub::ReaderWorker worker;
    epub::Controller app;
    epub::Input input;
    uint32_t inputNow = 1000;
    Harness(const std::string& state, const std::string& assets, const std::string& book, const std::string& executable)
        : store(state, state + "/home"), worker(assets, state, executable), app(store, worker, book) {}
    ~Harness() { app.shutdown(); }
    void key(SDLKey symbol, bool down) {
        app.action(input.key(symbol, down, inputNow));
        app.consumeTransition(input);
    }
    void tap(SDLKey symbol) {
        inputNow += 10; key(symbol, true);
        inputNow += 40; key(symbol, false);
    }
    void wait() {
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(40);
        do {
            app.tick(clockMs()); app.consumeTransition(input);
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        } while (app.busy() && std::chrono::steady_clock::now() < deadline);
        check(!app.busy(), "Controller timeout: " + app.status + ' ' + app.notice);
        check(app.screen() == epub::Screen::Reader && app.notice.empty(), "Reader failed: " + app.notice);
        check(app.page.validNavigation() && app.page.pixels.size() == 320 * 480, "Missing page result");
    }
};
void diagnosePrefetch(const std::string& assets, const std::string& book, const std::string& output, std::ostream& trace) {
    // Isolate engine navigation from input/IPC, then introduce the worker's
    // post-response prefetch step to identify where the position starts drifting.
    for (bool prefetch : {false, true}) {
        const auto state = output + (prefetch ? "/engine-prefetch" : "/engine-no-prefetch");
        std::filesystem::remove_all(state);
        epub::ReaderEngine engine(assets, state);
        auto page = engine.execute({epub::ReaderCommand::Open, 0, 1, 0, book});
        int invalidAnchorPage = -1;
        std::string invalidAnchor;
        for (int n = 0; n < 7; ++n) {
            const auto message = std::string("engine-prefetch=") + (prefetch ? "yes" : "no")
                + " step=" + std::to_string(n) + " page=" + std::to_string(page.page + 1)
                + " anchor-on-page=" + std::to_string(page.anchorOnPage);
            std::cout << message << '\n'; trace << message << '\n';
            if (!page.anchorOnPage && invalidAnchorPage < 0) {
                invalidAnchorPage = page.page; invalidAnchor = page.anchor;
            }
            if (prefetch) engine.prefetch();
            if (n < 6) page = engine.execute({epub::ReaderCommand::Turn, 1});
        }
        if (!prefetch && invalidAnchorPage >= 0) {
            const auto restored = engine.execute({epub::ReaderCommand::Restore, 0, 1, 0, {}, invalidAnchor});
            const auto message = "anchor-roundtrip: original-page=" + std::to_string(invalidAnchorPage + 1)
                + " restored-page=" + std::to_string(restored.page + 1);
            std::cout << message << '\n'; trace << message << '\n';
        }
    }
}
}

int main(int argc, char** argv) {
    try {
        check(argc == 5 || (argc == 6 && std::string(argv[5]) == "--expect-blank"),
            "Expected assets, book, output directory, worker executable, optional --expect-blank");
        const bool expectBlank = argc == 6;
        const std::string assets = argv[1], book = argv[2], output = argv[3];
        check(std::filesystem::is_regular_file(book), "EPUB does not exist: " + book);
        std::filesystem::create_directories(output);
        const auto state = output + "/state";
        std::filesystem::remove_all(state);
        std::ofstream trace(output + "/trace.txt");
        check(static_cast<bool>(trace), "Cannot create reproduction trace");
        Harness h(state, assets, book, argv[4]);
        h.wait();
        check(h.app.page.page == 0 && h.app.page.pageCount > 1, "Expected fresh multi-page book at page 1");
        unsigned failures = 0, sequence = 0;
        bool blankSeen = false, blankFallbackSeen = false, blankThemeTested = false;
        auto record = [&](const std::string& label, int expected = -1) {
            const auto& page = h.app.page;
            const auto background = epub::palettes[static_cast<size_t>(page.theme)].background;
            const auto ink = std::count_if(page.pixels.begin(), page.pixels.begin() + 320 * 478,
                [&](uint32_t pixel) { return pixel != background; });
            blankSeen = blankSeen || ink == 0;
            blankFallbackSeen = blankFallbackSeen || (ink == 0 && !page.anchorOnPage);
            const auto message = label + ": page=" + std::to_string(page.page + 1) + '/' + std::to_string(page.pageCount)
                + " expected=" + (expected < 0 ? "n/a" : std::to_string(expected + 1))
                + " ink=" + std::to_string(ink) + " text-bytes=" + std::to_string(page.text.size())
                + " anchor-on-page=" + std::to_string(page.anchorOnPage) + " page-cache=" + std::to_string(page.pageCacheHit)
                + " anchor=" + page.anchor + " page-anchor=" + page.pageAnchor;
            std::cout << message << '\n'; trace << message << '\n'; trace.flush();
            const auto prefix = output + '/' + std::to_string(sequence++) + '-' + label;
            epub::Canvas canvas; epub::renderApplication(canvas, h.app); canvas.saveBmp(prefix + ".bmp");
            h.tap(SDLK_x); epub::renderApplication(canvas, h.app); canvas.saveBmp(prefix + "-popup.bmp"); h.tap(SDLK_b);
            if (expected >= 0 && page.page != expected) {
                ++failures;
                std::cerr << "FAIL: " << label << " did not reach page " << expected + 1 << '\n';
            }
        };
        auto turn = [&](SDLKey symbol, const std::string& label, int direction) {
            const int expected = std::clamp(h.app.page.page + direction, 0, h.app.page.pageCount - 1);
            const auto request = h.app.page.requestId;
            h.tap(symbol);
            check(h.app.busy(), "Page-turn input did not submit work");
            h.wait();
            check(h.app.page.requestId > request && h.app.page.command == epub::ReaderCommand::Turn,
                "Page-turn result was not received");
            record(label, expected);
        };
        record("open");
        // Every input is released and its job completed before the next press.
        // Continue after a mismatch to distinguish a persistent stall from a blank page.
        for (int n = 0; n < 12; ++n) {
            turn(n % 2 ? SDLK_BACKSPACE : SDLK_RIGHT, n % 2 ? "R1" : "dpad-right", 1);
            if (blankSeen && !blankThemeTested) {
                const int origin = h.app.page.page;
                const int count = h.app.page.pageCount;
                const auto anchor = h.app.page.anchor;
                for (const auto key : {SDLK_PAGEDOWN, SDLK_PAGEUP}) {
                    h.tap(key); h.wait();
                    check(h.app.page.command == epub::ReaderCommand::Theme, "Theme input did not reach worker");
                    check(h.app.page.anchor == anchor, "Theme redraw changed the authoritative anchor");
                    check(h.app.page.pageCount == count, "Theme redraw changed the layout");
                    record("blank-theme", origin);
                }
                blankThemeTested = true;
            }
        }
        check(!expectBlank || (blankFallbackSeen && blankThemeTested), "Fixture no longer exercises an off-page blank bookmark");
        // Cross the same boundary backward and forward with warm neighbor caches.
        for (int n = 0; n < 12; ++n)
            turn(n % 2 ? SDLK_TAB : SDLK_LEFT, n % 2 ? "L1" : "dpad-left", -1);
        for (int n = 0; n < 6; ++n) turn(SDLK_BACKSPACE, "R1-after-backtracking", 1);

        // Exercise the reported workaround through the real slider, not a direct engine seek.
        h.tap(SDLK_ESCAPE);
        check(h.app.readerControls.jumpOpen(), "Jump slider did not open");
        for (int n = 0; n < 20; ++n) h.tap(SDLK_DOWN);
        h.tap(SDLK_UP);
        check(h.app.readerControls.targetPercent() == 10, "Failed to select 10% target");
        const int jumpPage = h.app.readerControls.targetPage();
        h.tap(SDLK_a); h.wait();
        check(!h.app.readerControls.jumpOpen(), "Jump slider stayed open");
        record("jump-10-percent", jumpPage);
        check(jumpPage > 0, "Expected a forward jump target");
        for (int n = 0; n < 6; ++n) turn(SDLK_LEFT, "dpad-left-after-jump", -1);
        if (failures) diagnosePrefetch(assets, book, output, trace);
        const auto summary = failures ? "FAIL: " + std::to_string(failures) + " page turns failed to advance to the adjacent page"
                                      : "PASS: sequential forward/backward turns, blank-page theme redraw and jump navigation";
        trace << summary << '\n'; std::cout << summary << '\n';
        return failures ? 1 : 0;
    } catch (const std::exception& error) { std::cerr << "FAIL: " << error.what() << '\n'; return 1; }
}
