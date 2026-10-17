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
#include <atomic>

namespace {
void check(bool ok, const std::string& message) { if (!ok) throw std::runtime_error(message); }
uint32_t clockMs() {
    return static_cast<uint32_t>(std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now().time_since_epoch()).count());
}
struct Harness {
    struct FakeMusic : epub::MusicPort {
        unsigned begins = 0, ends = 0, toggles = 0, skips = 0, chirps = 0, ticks = 0;
        epub::MusicSnapshot state{epub::MusicScan::Ready, epub::Playback::Stopped, true, 5, 0, {}};
        void beginBook() override { ++begins; state.playback = epub::Playback::Stopped; }
        void endBook() override { ++ends; state.playback = epub::Playback::Stopped; }
        void toggle() override { ++toggles; state.playback = state.playback == epub::Playback::Playing ? epub::Playback::Paused : epub::Playback::Playing; state.track = 2; }
        void skip() override { ++skips; }
        void chirp() override { ++chirps; }
        bool tick() override { ++ticks; return false; }
        epub::MusicSnapshot snapshot() const override { return state; }
        void setRepeat(epub::RepeatMode mode) override { state.repeat = mode; }
    } music;
    epub::Store store;
    epub::ReaderWorker worker;
    epub::Controller app;
    epub::Input input;
    uint32_t inputNow = 1000;
    Harness(const std::string& root, const std::string& assets, const std::string& executable, const std::string& book = {}, epub::MusicPort* port = nullptr, epub::PoweroffPort* poweroff = nullptr)
        : store(root, root + "/home"), worker(assets, root, executable), app(store, worker, book, port ? port : &music, poweroff) {}
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
epub::PowerSnapshot battery(int percent, uint32_t sequence, uint64_t time = 1000) {
    epub::PowerSnapshot s; s.percent = percent; s.source = epub::PercentSource::Native;
    s.external = epub::ExternalPower::Battery; s.sequence = sequence; s.sampled = time; return s;
}
void settlePower(Harness& h, const epub::PowerSnapshot& s, uint64_t now = 1000) {
    for (int n = 0; n < 4; ++n) { h.store.flush(); h.app.power(s, now); h.app.consumeTransition(h.input); }
}
struct RecordingPoweroff : epub::PoweroffPort {
    unsigned requests = 0;
    std::function<void()> verify;
    epub::PoweroffResult result{epub::PoweroffState::Unsupported, "RECORDING BACKEND / NO SYSTEM COMMAND"};
    void request(uint64_t) override { ++requests; if (verify) verify(); }
    epub::PoweroffResult poll(uint64_t) override { return result; }
};
void productionShutdownIntegration(const std::string& root, const std::string& assets, const std::string& executable) {
    using namespace epub;
    const auto book = assets + "/books/prose-epub3.epub";
    for (const std::string context : {"library", "library-menu", "chooser", "opening", "reader", "menu", "jump", "font-busy", "jump-busy"}) {
        RecordingPoweroff backend;
        const bool library = context == "library" || context == "library-menu" || context == "chooser";
        Harness h(root + '/' + context, assets, executable, library ? "" : book, nullptr, &backend);
        if (context != "opening") h.wait();
        if (context == "library-menu" || context == "chooser" || context == "menu" || context == "font-busy") h.tap(SDLK_x);
        if (context == "chooser") { h.tap(SDLK_DOWN); h.tap(SDLK_a); }
        if (context == "font-busy") { h.tap(SDLK_DOWN); h.tap(SDLK_RIGHT); }
        if (context == "jump" || context == "jump-busy") target(h, 50);
        if (context == "jump-busy") h.tap(SDLK_a);
        h.tap(SDLK_LSHIFT);
        check(!h.app.lowPowerModal && !backend.requests && h.app.exitReason() == ExitReason::None, "Retired Y shortcut remained active in " + context);
    }
    std::string anchor, identity;
    const auto state = root + "/saved"; configure(state, assets + "/books");
    {
        RecordingPoweroff backend;
        Harness h(state, assets, executable, book, nullptr, &backend); h.wait(); h.tap(SDLK_RIGHT); h.wait();
        anchor = h.app.page.anchor; identity = h.app.page.identity;
        std::atomic<bool> started{false}, release{false};
        h.store.saveCatalog([&] { started = true; while (!release) std::this_thread::sleep_for(std::chrono::milliseconds(1)); return "{\"schema\":1}"; });
        while (!started) std::this_thread::sleep_for(std::chrono::milliseconds(1));
        h.tap(SDLK_x); h.tap(SDLK_a); // Back queues a book write; do not wait for it.
        h.store.config.preferences.rawLeft = 18; h.store.config.preferences.rawRight = 17;
        h.store.config.musicRepeat = RepeatMode::One;
        h.music.state.playback = Playback::Playing;
        backend.verify = [&] {
            check(readBook(state, identity).anchor == anchor, "Critical shutdown passed a pending Back bookmark");
            check(readDocument(state + "/session.json").find("\"mode\": \"list\"") != std::string::npos, "Critical shutdown preceded list persistence");
            check(readDocument(state + "/config.json").find("\"rawLeft\": 18") != std::string::npos, "Critical shutdown lost latest mixer/config");
            check(h.music.state.playback == Playback::Stopped && !h.worker.busy(), "Critical shutdown left work/music active");
        };
        h.key(SDLK_a, true); h.app.power(battery(1, 1), 1000); h.app.power(battery(1, 2), 1000);
        h.app.consumeTransition(h.input); h.key(SDLK_a, false);
        const bool prepared = h.app.exitReason() == ExitReason::LowBatteryPoweroff && !backend.requests && h.app.lowPowerModal;
        h.app.action(Action::Quit); const bool quitWaited = !h.app.quit() && h.app.exitReason() == ExitReason::LowBatteryPoweroff;
        release = true; settlePower(h, battery(1, 2));
        check(prepared && quitWaited && backend.requests == 1 && h.app.quit(), "Critical preparation, held input or quit priority failed");
        h.tap(SDLK_b); h.tap(SDLK_LSHIFT); settlePower(h, battery(1, 3));
        check(backend.requests == 1, "Critical shutdown repeated within its episode");
    }
    {
        Store s(state, root + "/new-home");
        check(!s.session.reader && s.session.selectedIdentity == identity && s.book(identity).anchor == anchor
              && s.config.preferences.rawLeft == 18 && s.config.musicRepeat == RepeatMode::One, "Critical shutdown reboot state lost book/selection/config");
    }
    for (const auto outcome : {PoweroffState::Submitted, PoweroffState::Failed, PoweroffState::TimedOut}) {
        RecordingPoweroff backend; backend.result = {outcome, poweroffStateName(outcome)};
        Harness h(root + '/' + poweroffStateName(outcome), assets, executable, {}, nullptr, &backend);
        h.app.power(battery(2, 1), 1000); h.app.power(battery(2, 2), 1000); settlePower(h, battery(2, 2));
        check(backend.requests == 1 && h.app.lowPowerDetail == poweroffStateName(outcome), "Critical shutdown command result not observable");
        h.tap(SDLK_b); h.tap(SDLK_y); h.app.power(battery(1, 3), 1000); check(backend.requests == 1, "Critical shutdown outcome repeated command");
    }
    std::cout << "PASS: production Y is inert, critical save-before-command, pending Back anchor, one-shot effects and reboot state\n";
}
void powerIntegration(const std::string& root, const std::string& assets, const std::string& executable, const std::string& shots) {
    using namespace epub;
    const auto book = assets + "/books/prose-epub3.epub";
    const auto state = root + "/low"; std::string anchor, identity;
    configure(state, assets + "/books");
    {
        Harness h(state, assets, executable, book); h.wait(); h.tap(SDLK_RIGHT); h.wait();
        anchor = h.app.page.anchor; identity = h.app.page.identity;
        h.music.state.playback = Playback::Playing;
        // Repeat changes while the engine is busy do not recreate music or pagination.
        h.tap(SDLK_x); h.tap(SDLK_DOWN); h.tap(SDLK_RIGHT); check(h.worker.busy(), "Power/repeat busy setup");
        h.tap(SDLK_DOWN); const auto begins = h.music.begins; h.tap(SDLK_RIGHT);
        check(h.store.config.musicRepeat == RepeatMode::One && h.music.state.repeat == RepeatMode::One && h.music.begins == begins, "Busy repeat adjustment/persistence dispatch"); h.wait(); h.tap(SDLK_b);
        // Exact native frame bounds through every overlay and theme.
        h.app.power(battery(14, 1), 1000); Canvas c;
        for (size_t theme = 0; theme < palettes.size(); ++theme) for (int overlay = 0; overlay < 3; ++overlay) {
            h.store.config.preferences.theme = static_cast<int>(theme); h.app.readerControls.closeOverlay();
            if (overlay == 1) { h.app.readerControls.showMenu(); h.dispatch(Action::Down); h.dispatch(Action::Down); }
            if (overlay == 2) h.dispatch(Action::Select);
            renderApplication(c, h.app);
            for (int y = 0; y < 478; ++y) for (int x = 0; x < 320; ++x) {
                if (x < 2 || x >= 318 || y < 2 || y >= 476) check(c.pixels[static_cast<size_t>(y * 320 + x)] == 0xe03030, "Low warning frame native bounds");
            }
            for (int y : {478, 479}) for (int x = 0; x < 320; ++x) check(c.pixels[static_cast<size_t>(y * 320 + x)] == (x < 320 * h.app.page.progress / 10000 ? palettes[theme].muted : palettes[theme].background), "Warning covered native progress rows");
            c.saveBmp(shots + "/power-" + palettes[theme].name + '-' + std::to_string(overlay) + ".bmp");
        }
        h.store.config.preferences.theme = 0; h.app.readerControls.closeOverlay(); target(h, 100);
        h.key(SDLK_a, true); h.app.power(battery(5, 2), 1000); h.app.power(battery(4, 3), 1000); h.app.consumeTransition(h.input);
        check(h.app.screen() == Screen::Library && h.app.lowPowerModal && h.music.state.playback == Playback::Stopped, "Low interruption did not stop/return to list");
        h.key(SDLK_a, false); check(h.app.lowPowerModal, "Held A dismissed low notification");
        settlePower(h, battery(4, 3));
        check(h.app.lowPowerSave == "POSITION SAVED" && readBook(state, identity).anchor == anchor, "Low transition saved preview/font result instead of last completed anchor");
        for (size_t theme = 0; theme < palettes.size(); ++theme) {
            h.store.config.preferences.theme = static_cast<int>(theme); renderApplication(c, h.app); c.saveBmp(shots + "/power-modal-" + palettes[theme].name + ".bmp");
        }
        h.tap(SDLK_a); check(!h.app.lowPowerModal && h.app.screen() == Screen::Library, "Dismissal also opened book");
        h.wait(); h.open(book); h.app.power(battery(3, 4), 1000); h.app.power(battery(3, 5), 1000);
        check(h.app.screen() == Screen::Reader, "Explicit low episode override reclosed book");
        h.app.power(battery(2, 6), 1000); h.app.power(battery(1, 7), 1000); settlePower(h, battery(1, 7));
        check(h.app.screen() == Screen::Library && h.app.exitReason() == ExitReason::LowBatteryPoweroff && !h.app.lowPowerDetail.empty(), "Two-percent list fallback absent");
    }
    {
        Store s(state, root); check(!s.session.reader && s.book(identity).anchor == anchor && s.config.musicRepeat == RepeatMode::One, "Reboot state/repeat did not retain low transition");
    }
    for (const std::string context : {"library", "library-menu", "chooser", "opening", "reader", "menu", "jump", "font-busy", "jump-busy"}) {
        RecordingPoweroff backend;
        const bool library = context == "library" || context == "library-menu" || context == "chooser";
        const auto path = root + '/' + context;
        Harness h(path, assets, executable, library ? "" : book, nullptr, &backend);
        if (context != "opening") h.wait();
        if (context == "library-menu" || context == "chooser" || context == "menu" || context == "font-busy") h.tap(SDLK_x);
        if (context == "chooser") { h.tap(SDLK_DOWN); h.tap(SDLK_a); }
        if (context == "font-busy") { h.tap(SDLK_DOWN); h.tap(SDLK_RIGHT); }
        if (context == "jump" || context == "jump-busy") target(h, 50);
        if (context == "jump-busy") h.tap(SDLK_a);
        const auto savedAnchor = h.app.page.anchor, savedId = h.app.page.identity;
        backend.verify = [&] {
            check(readDocument(path + "/session.json").find("\"mode\": \"list\"") != std::string::npos, "Shutdown requested before list acknowledgement in " + context);
            if (!savedAnchor.empty()) check(readBook(path, savedId).anchor == savedAnchor, "Shutdown lost completed anchor in " + context);
            check(h.music.state.playback == Playback::Stopped, "Shutdown before music stopped");
        };
        h.app.power(battery(1, 1), 1000); h.app.power(battery(1, 2), 1000); settlePower(h, battery(1, 2));
        for (uint32_t n = 3; n < 10; ++n) h.app.power(battery(0, n), 1000);
        check(backend.requests == 1 && h.app.lowPowerSave == "POSITION SAVED", "Poweroff loop/save failure in " + context);
        h.key(SDLK_RETURN, true); h.key(SDLK_ESCAPE, true); check(h.app.quit() && h.app.exitReason() == ExitReason::LowBatteryPoweroff, "Global quit downgraded poweroff reason/fallback exit broken");
    }
    for (const std::string condition : {"deadline", "charger", "unknown", "cleared", "stale"}) {
        const auto path = root + '/' + condition; RecordingPoweroff backend;
        Harness h(path, assets, executable, book, nullptr, &backend); h.wait();
        const auto savedAnchor = h.app.page.anchor, savedId = h.app.page.identity;
        std::atomic<bool> started{false}, release{false};
        h.store.saveCatalog([&] { started = true; while (!release) std::this_thread::sleep_for(std::chrono::milliseconds(1)); return "{\"schema\":1}"; });
        while (!started) std::this_thread::sleep_for(std::chrono::milliseconds(1));
        h.app.power(battery(5, 1), 1000); h.app.power(battery(5, 2), 1000);
        h.app.power(battery(2, 3), 1000); h.app.power(battery(1, 4), 1000);
        h.app.action(Action::Quit); const bool keptReason = !h.app.quit() && h.app.exitReason() == ExitReason::LowBatteryPoweroff;
        auto s = battery(1, 5, 6000);
        if (condition == "charger") s.external = ExternalPower::Connected;
        if (condition == "unknown") s.percent.reset();
        if (condition == "cleared") s.percent = 3;
        if (condition == "stale") s.sampled = 0;
        const uint64_t time = condition == "stale" ? 7000 : 6000;
        const auto since = std::chrono::steady_clock::now(); h.app.power(s, time);
        const bool bounded = std::chrono::steady_clock::now() - since < std::chrono::milliseconds(100);
        const bool outcome = condition != "deadline" ? !backend.requests : backend.requests == 1 && h.app.lowPowerSave == "SAVE INCOMPLETE";
        release = true; settlePower(h, s, time);
        check(keptReason && bounded && outcome, "Promotion/deadline/charger cancellation contract");
        check(readBook(path, savedId).anchor == savedAnchor && !h.store.session.reader, "Promotion lost captured checkpoint");
    }
    {
        const auto path = root + "/failed"; Harness h(path, assets, executable, book); h.wait();
        const auto savedId = h.app.page.identity, savedAnchor = h.app.page.anchor;
        ensureDirectory(path + "/books/" + savedId + ".json.tmp");
        h.app.power(battery(5, 1), 1000); h.app.power(battery(5, 2), 1000); settlePower(h, battery(5, 2));
        check(h.app.lowPowerSave == "SAVE FAILED", "Failed-but-drained transition claimed save success");
        std::filesystem::remove_all(path + "/books/" + savedId + ".json.tmp"); h.tap(SDLK_x); settlePower(h, battery(5, 2));
        check(h.app.lowPowerSave == "POSITION SAVED" && readBook(path, savedId).anchor == savedAnchor, "Explicit failed-save retry lost captured data");
    }
    {
        const auto path = root + "/failed-critical"; RecordingPoweroff backend;
        Harness h(path, assets, executable, book, nullptr, &backend); h.wait();
        ensureDirectory(path + "/config.json.tmp");
        h.app.power(battery(2, 1), 1000); h.app.power(battery(2, 2), 1000); settlePower(h, battery(2, 2));
        check(backend.requests == 1 && h.app.lowPowerSave != "POSITION SAVED", "Critical failure did not make a bounded best-effort request");
        check(h.app.lowPowerDetail.find("RECORDING BACKEND") != std::string::npos && !h.app.lowPowerError.empty(), "Save failure hid shutdown capability explanation");
        std::filesystem::remove_all(path + "/config.json.tmp"); h.tap(SDLK_x); settlePower(h, battery(2, 2));
        check(h.app.lowPowerSave == "POSITION SAVED" && backend.requests == 1, "Failed critical explicit retry resubmitted shutdown");
    }
    {
        const auto path = root + "/late-failure"; RecordingPoweroff backend;
        Harness h(path, assets, executable, book, nullptr, &backend); h.wait();
        std::atomic<bool> started{false}, release{false};
        h.store.saveCatalog([&] { started = true; while (!release) std::this_thread::sleep_for(std::chrono::milliseconds(1)); return "{\"schema\":1}"; });
        while (!started) std::this_thread::sleep_for(std::chrono::milliseconds(1));
        ensureDirectory(path + "/config.json.tmp");
        h.app.power(battery(2, 1), 1000); h.app.power(battery(2, 2), 1000);
        const auto s = battery(2, 3, 6000); h.app.power(s, 6000); release = true;
        h.store.flush(); h.app.power(s, 6000);
        const bool noLateRetry = h.store.idle() && h.app.lowPowerSave == "SAVE FAILED" && backend.requests == 1;
        std::filesystem::remove_all(path + "/config.json.tmp");
        check(noLateRetry, "Automatic retry escaped the five-second budget");
    }
    std::cout << "PASS: production power transitions across screens/work, durable acknowledgements, promotion/deadline/charger, retry, input and native frames\n";
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
        h.music.state.playback = reverse ? epub::Playback::Paused : epub::Playback::Playing;
        h.key(reverse ? SDLK_RETURN : SDLK_ESCAPE, true); h.inputNow += 10;
        h.key(reverse ? SDLK_ESCAPE : SDLK_RETURN, true);
        check(h.app.quit(), "Quit chord unavailable in " + context);
        check(h.music.state.playback == epub::Playback::Stopped, "Quit chord retained music in " + context);
    }
    std::cout << "PASS: production slider previews, hold/repeat/focus/chords, commits, cancellation, restart, completion and failure recovery\n";
}
void musicIntegration(const std::string& root, const std::string& folder, const std::string& assets, const std::string& executable, const std::string& shots) {
    using namespace epub;
    configure(root, folder);
    { Store s(root, root + "/home"); s.config.music = folder; s.saveConfig(); s.flush(); }
    {
        Harness h(root, assets, executable); h.wait();
        check(!h.music.chirps && !h.music.begins, "Indexing chirped or started a music session");
        while (h.app.catalog.selected()) h.tap(SDLK_LEFT);
        const auto baseline = h.music.chirps;
        h.tap(SDLK_UP); check(h.music.chirps == baseline, "Clamped navigation chirped");
        h.tap(SDLK_DOWN); check(h.music.chirps == baseline + 1, "Selection did not chirp once");
        h.tap(SDLK_RIGHT); check(h.music.chirps == baseline + 2, "Page navigation did not chirp");
        h.tap(SDLK_x); const auto chirps = h.music.chirps;
        h.tap(SDLK_DOWN); h.tap(SDLK_b); check(h.music.chirps == chirps, "Menu navigation chirped");
        h.select(folder + "/Book 3.epub");
        h.tap(SDLK_a); check(h.app.screen() == Screen::Opening && !h.music.toggles, "Opening A started music");
        h.key(SDLK_a, true); h.wait(); h.key(SDLK_a, false);
        check(h.music.begins == 1 && !h.music.toggles, "Held opening A leaked to reader");
        h.key(SDLK_a, true); check(!h.music.toggles, "Short A acted on keydown");
        h.inputNow += 40; h.key(SDLK_a, false); check(h.music.toggles == 1, "Short A did not toggle music");
        h.key(SDLK_a, true); h.inputNow += 599; h.dispatch(h.input.tick(h.inputNow)); check(!h.music.skips, "Early skip");
        ++h.inputNow; h.dispatch(h.input.tick(h.inputNow)); h.inputNow += 700; h.dispatch(h.input.tick(h.inputNow)); h.key(SDLK_a, false);
        check(h.music.skips == 1 && h.music.toggles == 1, "Long A repeated or toggled on release");
        h.key(SDLK_a, true); h.tap(SDLK_x); h.key(SDLK_a, false);
        h.tap(SDLK_DOWN); h.tap(SDLK_a); h.dispatch(Action::ALong);
        check(h.music.toggles == 1 && h.music.skips == 1, "Popup A leaked to music");
        h.tap(SDLK_RIGHT); check(h.worker.busy(), "Font job not busy"); h.tap(SDLK_b);
        h.tap(SDLK_a); const auto ticks = h.music.ticks; h.app.tick(clockMs());
        check(h.music.toggles == 2 && h.music.ticks > ticks, "Reader work blocked music dispatch/tick"); h.wait();
        check(h.music.begins == 1, "Font results reset music session");
        target(h, 50); h.dispatch(Action::ALong); h.tap(SDLK_a); h.wait();
        check(h.music.toggles == 2 && h.music.skips == 1 && h.music.begins == 1, "Jump A leaked or reset music");
        h.tap(SDLK_ESCAPE); h.key(SDLK_a, true); h.tap(SDLK_b); h.key(SDLK_a, false);
        check(h.music.toggles == 2, "Held overlay close A leaked");
        h.key(SDLK_a, true); h.input.reset(); h.key(SDLK_a, false); check(h.music.toggles == 2, "Focus reset toggled music");
        Canvas c; renderApplication(c, h.app);
        check(std::equal(c.pixels.begin(), c.pixels.begin() + 478 * 320, h.app.page.pixels.begin()), "Music added normal reading overlay");
        for (size_t theme = 0; theme < palettes.size(); ++theme) {
            h.store.config.preferences.theme = static_cast<int>(theme);
            h.music.state.count = 9999; h.music.state.track = 8888; h.music.state.playback = Playback::Playing;
            h.app.readerControls.showMenu(); renderApplication(c, h.app); checkBar(c, h.app.page, static_cast<int>(theme));
            c.saveBmp(shots + "/music-" + palettes[theme].name + "-playing.bmp");
        }
        h.store.config.preferences.theme = 0;
        for (const std::string state : {"paused", "stopped", "scanning", "empty", "failed", "unavailable"}) {
            h.music.state = {MusicScan::Ready, Playback::Paused, true, 24, 7, {}};
            if (state == "stopped") h.music.state.playback = Playback::Stopped;
            if (state == "scanning") h.music.state.scan = MusicScan::Scanning;
            if (state == "empty") h.music.state.scan = MusicScan::Empty;
            if (state == "failed") { h.music.state.scan = MusicScan::Failed; h.music.state.detail = "Music folder: unavailable"; }
            if (state == "unavailable") { h.music.state.available = false; h.music.state.detail = "Audio device unavailable"; }
            renderApplication(c, h.app); checkBar(c, h.app.page, 0); c.saveBmp(shots + "/music-" + state + ".bmp");
        }
        h.music.state.detail = "No playable tracks; restart to retry";
        h.app.notice = "SAVE ERROR: retain priority"; renderApplication(c, h.app); c.saveBmp(shots + "/music-save-error.bmp");
        h.app.notice.clear(); h.app.readerControls.closeOverlay();
        h.back(); check(h.music.state.playback == Playback::Stopped, "Back to list did not stop music");
        h.tap(SDLK_x); h.tap(SDLK_DOWN); h.tap(SDLK_DOWN); h.tap(SDLK_a); h.wait();
        h.tap(SDLK_a); h.wait();
        check(h.app.notice == "Restart to apply music folder" && h.store.config.music == folder && h.music.begins == 1,
            "Music folder commit lost restart notice or restarted music");
        h.open(folder + "/Book 3.epub"); check(h.music.begins == 2 && h.music.state.playback == Playback::Stopped, "Reopen auto-started music");
        h.tap(SDLK_a);
    }
    {
        Harness h(root, assets, executable); h.wait();
        check(h.music.begins == 1 && !h.music.toggles && h.music.state.playback == Playback::Stopped, "Resume auto-started music");
        h.tap(SDLK_a); h.key(SDLK_RETURN, true); h.key(SDLK_ESCAPE, true);
        check(h.app.quit() && h.music.state.playback == Playback::Stopped, "Quit did not immediately stop music");
    }
    std::cout << "PASS: production music input/lifecycle/chirps, busy-reader controls, overlays and native popup snapshots\n";
}
void liveMusic(const std::string& root, const std::string& fixtures, const std::string& assets, const std::string& executable) {
    using namespace epub;
    check(SDL_Init(SDL_INIT_TIMER) == 0, "Live music SDL init");
    struct Guard { ~Guard() { SDL_Quit(); } } guard;
    Audio audio; Music music(audio, fixtures, 15);
    Harness h(root, assets, executable, assets + "/books/prose-epub3.epub", &music); h.wait();
    const auto scanSince = SDL_GetTicks();
    while (music.snapshot().scan == MusicScan::Scanning && SDL_GetTicks() - scanSince < 4000) { h.app.tick(clockMs()); SDL_Delay(1); }
    check(music.snapshot().scan == MusicScan::Ready, "Live startup catalog");
    h.tap(SDLK_a); h.app.tick(clockMs());
    const auto first = audio.status().generation;
    for (int n = 0; n < 12; ++n) { h.tap(SDLK_RIGHT); h.wait(); }
    h.tap(SDLK_x); h.tap(SDLK_DOWN); h.tap(SDLK_RIGHT); h.wait();
    check(music.snapshot().playback == Playback::Playing, "Font popup stopped live music"); h.tap(SDLK_b);
    target(h, 60); h.tap(SDLK_a); h.wait();
    const auto since = SDL_GetTicks();
    while ((SDL_GetTicks() - since < 1500 || !audio.status().playedFrames) && SDL_GetTicks() - since < 4000) { h.app.tick(clockMs()); SDL_Delay(2); }
    check(audio.status().generation > first + 2 && audio.status().playedFrames > 0, "Reader work stalled automatic MP3 continuation");
    h.tap(SDLK_RIGHT); h.tap(SDLK_a); h.wait();
    check(music.snapshot().playback == Playback::Paused, "Busy reader could not pause live music");
    const auto requests = h.app.page.requestId;
    h.back(); check(audio.status().playback == Playback::Stopped && audio.status().queuedFrames == 0, "List retained live PCM");
    h.app.tick(clockMs()); check(music.snapshot().playback == Playback::Stopped, "Late EOF restarted list playback");
    const auto s = audio.status(); std::cout << "LIVE MUSIC reader request=" << requests << " output=" << s.frequency << '/' << s.channels << " underruns=" << s.underruns << '\n';
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
        musicIntegration(root + "/music-state", folder, assets, executable, shots);
        powerIntegration(root + "/power-state", assets, executable, shots);
        productionShutdownIntegration(root + "/production-shutdown", assets, executable);
        liveMusic(root + "/live-music-state", fixtures, assets, executable);
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
