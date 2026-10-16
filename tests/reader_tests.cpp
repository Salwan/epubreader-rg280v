#include "reader/worker.hpp"
#include "storage/position.hpp"
#include "storage/state.hpp"
#include "ui/reader_controls.hpp"
#include <algorithm>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <regex>
#include <stdexcept>
#include <thread>

namespace {
void check(bool value, const std::string& message) { if (!value) throw std::runtime_error(message); }
epub::ReaderResult request(epub::ReaderWorker& worker, const epub::ReaderRequest& r) {
    check(worker.submit(r), "Worker rejected request");
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(30);
    while (std::chrono::steady_clock::now() < deadline) {
        if (auto result = worker.poll(); result && result->complete) return std::move(*result);
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    throw std::runtime_error("Reader worker timeout");
}
epub::ReaderResult good(epub::ReaderWorker& worker, const epub::ReaderRequest& r) {
    auto result = request(worker, r);
    check(result.error.empty(), result.error);
    check(result.pixels.size() == 320 * 480 && result.pageCount > 0, "Missing rendered page");
    check(result.anchorOnPage, "Authoritative anchor not on displayed page");
    return result;
}
void checkMenuControls() {
    using A = epub::Action; using E = epub::ReaderEffect; using M = epub::ReaderMenuItem;
    epub::ReaderControls controls;
    auto action = [&](A a, int font = 1, bool busy = false) { return controls.handle(a, font, 0, busy); };
    check(action(A::Y).effect == E::None, "Font hotkey still active outside popup");
    check(action(A::Right).effect == E::Turn, "Normal page-turn mapping");
    action(A::X);
    check(controls.menuOpen() && controls.selected() == M::Back, "Popup default selection");
    check(action(A::Right).effect == E::None && action(A::R1).effect == E::None, "Popup leaked page turns");
    action(A::Down);
    check(controls.selected() == M::FontSize, "Font row selection");
    const auto larger = action(A::Right), smaller = action(A::Left);
    check(larger.effect == E::Font && larger.value == 2, "Right increases menu font size");
    check(smaller.effect == E::Font && smaller.value == 0, "Left decreases menu font size");
    check(action(A::Left, 0).effect == E::None && action(A::Right, 2).effect == E::None, "Font range does not clamp");
    check(action(A::AShort).effect == E::None && action(A::ALong).effect == E::None && action(A::Y).effect == E::None, "Font row has an unintended activation/hotkey");
    check(action(A::Right, 1, true).effect == E::None, "Busy reader accepted another font job");
    action(A::Down, 1, true);
    check(action(A::AShort, 1, true).effect == E::Quit, "Popup exit unavailable during reader work");
    check(action(A::Quit, 1, true).effect == E::Quit, "Global exit did not override popup/busy state");
    action(A::B, 1, true);
    check(!controls.menuOpen(), "B did not close popup during reader work");
    action(A::X);
    check(controls.selected() == M::Back, "Popup selection was not reset on reopen");
    check(action(A::AShort, 1, true).effect == E::Back && !controls.menuOpen(), "Back to library unavailable while busy");
    // Closing an overlay must not turn a held menu direction into a specimen
    // shortcut; an A press spanning the transition must also be consumed.
    epub::Input input;
    input.key(SDLK_DOWN, true, 0);
    input.consumeRepeat();
    check(input.tick(400) == A::None && input.held(epub::Button::Down), "Held menu navigation leaked after closing");
    input.key(SDLK_DOWN, false, 500);
    check(input.key(SDLK_DOWN, true, 600) == A::Down, "Fresh navigation press suppressed");
    input.key(SDLK_a, true, 700); input.consumeA();
    check(input.key(SDLK_a, false, 750) == A::None, "A release leaked across popup transition");
    std::cout << "Popup font controls, overlay arbitration, busy exit and held-input consumption PASS\n";
}
}
int main(int argc, char** argv) {
    try {
        check(argc == 5, "Expected assets, fixtures, isolated state directory, worker executable");
        checkMenuControls();
        const std::string assets = argv[1], fixtures = argv[2], state = argv[3];
        std::filesystem::remove_all(state);
        epub::ensureDirectory(state);
        epub::ReaderWorker worker(assets, state, argv[4]);
        auto metadata = request(worker, {epub::ReaderCommand::Inspect, 0, 1, 0, assets + "/books/prose-epub3.epub"});
        check(metadata.error.empty() && metadata.entries.size() == 1 && metadata.entries[0].error.empty(), "Metadata-only EPUB inspection: " + metadata.error + (metadata.entries.empty() ? " (no entry)" : metadata.entries[0].error));
        check(metadata.entries[0].title == "EPUB 3 Prose Specimen" && metadata.entries[0].author == "RG280V Reader Project", "Metadata-only properties");
        check(metadata.pixels.empty() && metadata.pageCount == 0 && !std::filesystem::exists(state + "/cache/documents"), "Metadata inspection initialized pagination");
        const auto generation = worker.generation(); worker.restart();
        check(worker.generation() > generation, "Worker generation did not advance");
        auto malformedMetadata = request(worker, {epub::ReaderCommand::Inspect, 0, 1, 0, fixtures + "/no-spine.epub"});
        check(malformedMetadata.error.empty() && malformedMetadata.entries[0].error.empty(), "Metadata-only invalid spine distinction");
        std::string restartAnchor;
        int restartPage = 0;
        for (const std::string version : {"2", "3"}) {
            epub::ReaderRequest open;
            open.path = assets + "/books/prose-epub" + version + ".epub";
            auto first = good(worker, open);
            check(first.title == "EPUB " + version + " Prose Specimen", "EPUB title metadata");
            check(first.author == "RG280V Reader Project", "EPUB author metadata");
            check(first.text.find("Café") != std::string::npos && first.text.find("naïve") != std::string::npos, "UTF-8 prose missing");
            check(first.pageCount > 30, "Fixture did not paginate");
            check(good(worker, {epub::ReaderCommand::Turn, -1}).page == 0, "First-page clamp");
            bool sawImage = false, sawGreen = false, sawBlue = false;
            std::string all;
            for (int page = 0; page < first.pageCount; ++page) {
                auto result = good(worker, {epub::ReaderCommand::Page, page});
                all += result.text + ' '; sawImage = sawImage || result.images > 0;
                sawGreen = sawGreen || std::find(result.pixels.begin(), result.pixels.end(), 0x328c6e) != result.pixels.end();
                sawBlue = sawBlue || std::any_of(result.pixels.begin(), result.pixels.end(), [](uint32_t p) {
                    return (p & 0xff) > 160 && ((p >> 8) & 0xff) < 100 && (p >> 16) < 60;
                });
                check(std::any_of(result.pixels.begin(), result.pixels.end(), [&](uint32_t p) { return p != result.pixels[0]; }), "Blank rendered page");
            }
            check(sawImage && sawGreen, "Nested relative PNG resource not rendered in original color");
            check(sawBlue, "Nested relative JPEG resource not decoded/rendered");
            // Every unique prose token appears exactly once across page ranges,
            // including a single paragraph that spans several pages.
            for (char prefix : {'S', 'W'}) {
                const std::regex token(std::string(1, prefix) + "[0-9]{4}");
                std::vector<std::string> found;
                for (auto i = std::sregex_iterator(all.begin(), all.end(), token); i != std::sregex_iterator(); ++i) found.push_back(i->str());
                const size_t expected = prefix == 'S' ? 288 : 300;
                check(found.size() == expected, std::string("Page continuity token count: ") + prefix + " got " + std::to_string(found.size()));
                check(std::is_sorted(found.begin(), found.end()), "Spine/page order incorrect");
                check(std::adjacent_find(found.begin(), found.end()) == found.end(), "Duplicated page-boundary text");
            }
            check(good(worker, {epub::ReaderCommand::Turn, 1}).page == first.pageCount - 1, "Last-page clamp");
            auto middle = good(worker, {epub::ReaderCommand::Page, first.pageCount / 2});
            const std::string anchor = middle.anchor, mediumKey = middle.layoutKey;
            epub::ReaderControls menu;
            menu.handle(epub::Action::X, 1, 0, false);
            menu.handle(epub::Action::Down, 1, 0, false);
            int font = 1;
            for (auto direction : {epub::Action::Left, epub::Action::Right, epub::Action::Right,
                                   epub::Action::Left, epub::Action::Left, epub::Action::Right}) {
                const auto control = menu.handle(direction, font, 0, false);
                check(control.effect == epub::ReaderEffect::Font, "Highlighted font row did not issue font change");
                font = control.value;
                auto result = good(worker, {epub::ReaderCommand::Font, font});
                check(result.anchor == anchor, "Anchor drift across repeated font changes");
                check(menu.menuOpen() && menu.selected() == epub::ReaderMenuItem::FontSize, "Font change moved/dismissed popup");
                if (font != 1) check(result.layoutKey != mediumKey && result.pageCount != middle.pageCount, "Preset did not repaginate/key cache");
                else check(result.page == middle.page && result.pageAnchor == middle.pageAnchor, "Medium preset restoration");
            }
            auto theme = good(worker, {epub::ReaderCommand::Theme, 5});
            check(theme.pageCount == middle.pageCount && theme.anchor == anchor && theme.layoutKey == mediumKey, "Theme changed pagination/anchor");
            check(theme.pixels != middle.pixels, "Theme failed to redraw");
            check(std::count(theme.pixels.begin(), theme.pixels.end(), 0x35271d) > 50, "Warm2 foreground is not authoritative");
            epub::savePosition(state, {middle.identity, middle.path, anchor});
            if (version == "3") { restartAnchor = anchor; restartPage = middle.page; }
            check(epub::loadPosition(state, middle.identity).anchor == anchor, "Serialized text position roundtrip");
            auto reopened = good(worker, open);
            check(reopened.anchor == anchor && reopened.page == middle.page, "Reopen at text location");
            check(reopened.documentCacheHit, "Document cache was not reused");
            // Rename preserves the content identity, bookmark and cache key.
            std::filesystem::copy_file(open.path, state + "/renamed.epub", std::filesystem::copy_options::overwrite_existing);
            open.path = state + "/renamed.epub";
            auto renamed = good(worker, open);
            check(renamed.identity == middle.identity && renamed.anchor == anchor && renamed.documentCacheHit, "Rename identity/restoration");
            check(good(worker, {epub::ReaderCommand::Turn, 1}).pageCacheHit, "Neighbor page cache was not reused");
            auto invalid = request(worker, {epub::ReaderCommand::Restore, 0, 1, 0, {}, "/does/not/exist.0"});
            check(!invalid.error.empty(), "Invalid bookmark accepted");
            std::cout << "EPUB " << version << ": " << first.pageCount << " pages; continuity, images, fonts, theme, bookmarks, document cache PASS\n";
        }
        auto shortBook = good(worker, {epub::ReaderCommand::Open, 0, 1, 0, fixtures + "/one-page.epub"});
        check(shortBook.pageCount == 1 && good(worker, {epub::ReaderCommand::Turn, 1}).page == 0, "One-page clamp");
        epub::savePosition(state, {shortBook.identity, shortBook.path, "/does/not/exist.0"});
        auto invalidSaved = good(worker, {epub::ReaderCommand::Open, 0, 1, 0, shortBook.path});
        check(!invalidSaved.message.empty() && invalidSaved.page == 0, "Invalid saved anchor fallback");
        std::filesystem::copy_file(shortBook.path, state + "/renamed.epub", std::filesystem::copy_options::overwrite_existing);
        auto changed = good(worker, {epub::ReaderCommand::Open, 0, 1, 0, state + "/renamed.epub"});
        check(changed.identity == shortBook.identity && changed.pageCount == 1, "Changed content reused stale document");
        std::ofstream(state + "/bad.epub") << "not an EPUB";
        check(!request(worker, {epub::ReaderCommand::Open, 0, 1, 0, state + "/bad.epub"}).error.empty(), "Malformed book accepted");
        check(!request(worker, {epub::ReaderCommand::Open, 0, 1, 0, state + "/missing.epub"}).error.empty(), "Missing book accepted");
        check(!request(worker, {epub::ReaderCommand::Open, 0, 1, 0, fixtures + "/no-spine.epub"}).error.empty(), "Malformed EPUB package accepted");
        check(!request(worker, {epub::ReaderCommand::Turn, 1}).error.empty(), "Failed-open engine error document remained navigable");
        good(worker, {epub::ReaderCommand::Open, 0, 1, 0, fixtures + "/one-page.epub"});
        // Stop an active load/pagination: the UI never joins a CRengine call.
        check(worker.submit({epub::ReaderCommand::Open, 0, 1, 0, assets + "/books/prose-epub3.epub"}), "Submit before cancellation");
        worker.poll();
        const auto start = std::chrono::steady_clock::now();
        worker.stop();
        const auto cancelTime = std::chrono::steady_clock::now() - start;
        check(cancelTime < std::chrono::milliseconds(500), "Reader cancellation not bounded");
        std::cout << "CANCEL terminate/reap ms=" << std::chrono::duration_cast<std::chrono::milliseconds>(cancelTime).count() << '\n';
        check(!worker.submit({}), "Stopped worker accepted job");
        {
            epub::ReaderWorker restarted(assets, state, argv[4]);
            const auto result = good(restarted, {epub::ReaderCommand::Open, 0, 1, 0, assets + "/books/prose-epub3.epub"});
            check(result.anchor == restartAnchor && result.page == restartPage, "Fresh worker/process did not restore serialized location");
        }
        for (const auto& entry : std::filesystem::directory_iterator(state + "/cache/documents")) {
            if (entry.path().extension() == ".cr3") std::ofstream(entry.path(), std::ios::trunc) << "interrupted cache";
        }
        {
            epub::ReaderWorker recovered(assets, state, argv[4]);
            const auto result = good(recovered, {epub::ReaderCommand::Open, 0, 1, 0, assets + "/books/prose-epub3.epub"});
            check(result.anchor == restartAnchor && !result.documentCacheHit, "Damaged cache did not rebuild at saved text location");
        }
        std::cout << "Worker framing, error recovery, one-page boundaries and bounded shutdown PASS\n";
        return 0;
    } catch (const std::exception& error) { std::cerr << "FAIL: " << error.what() << '\n'; return 1; }
}
