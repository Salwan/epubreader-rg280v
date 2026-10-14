#include "platform/input.hpp"
#include "reader/worker.hpp"
#include "storage/position.hpp"
#include "storage/state.hpp"
#include "ui/reader_slice.hpp"
#include <algorithm>
#include <chrono>
#include <csignal>
#include <cstdio>
#include <cstdlib>
#include <iostream>
#include <stdexcept>
#include <time.h>
#include <unistd.h>

namespace {
volatile sig_atomic_t interrupted = 0;
void signalHandler(int) { interrupted = 1; }
uint64_t suspendClock() {
    struct timespec time{};
    if (clock_gettime(CLOCK_BOOTTIME, &time) != 0) return SDL_GetTicks();
    return static_cast<uint64_t>(time.tv_sec) * 1000 + static_cast<uint64_t>(time.tv_nsec) / 1000000;
}
std::string executableDirectory() {
    char path[4096];
    const ssize_t n = readlink("/proc/self/exe", path, sizeof(path) - 1);
    if (n <= 0) return ".";
    path[n] = 0; std::string s(path); return s.substr(0, s.find_last_of('/'));
}
struct SDLGuard { ~SDLGuard() { SDL_Quit(); } };
void present(epub::Canvas& canvas, SDL_Surface* display, bool native) {
    SDL_Surface* surface;
    epub::Canvas preview(640, 480);
    if (native) surface = canvas.surface();
    else {
        for (int y = 0; y < 480; ++y) for (int x = 0; x < 640; ++x)
            preview.pixel(x, y, canvas.pixels[static_cast<size_t>(y * 320 + x / 2)]);
        surface = preview.surface();
    }
    if (!surface) throw std::runtime_error(SDL_GetError());
    SDL_BlitSurface(surface, nullptr, display, nullptr); SDL_FreeSurface(surface);
    if (SDL_Flip(display) < 0) throw std::runtime_error(SDL_GetError());
}
}
int main(int argc, char** argv) {
    try {
#ifdef EPUBREADER_DEVICE
        bool native = true;
        setenv("SDL_VIDEO_KMSDRM_SCALING_MODE", "0", 1);
#else
        bool native = false;
#endif
        const char* home = std::getenv("HOME");
        const std::string homePath = home ? home : ".";
        std::string assets = executableDirectory() + "/assets", stateDirectory = homePath + "/.epubreader/slice", book, smoke;
        for (int i = 1; i < argc; ++i) {
            const std::string arg = argv[i];
            auto value = [&]() -> std::string { if (++i >= argc) throw std::runtime_error("Missing value for " + arg); return argv[i]; };
            if (arg == "--assets") assets = value();
            else if (arg == "--state-dir") stateDirectory = value();
            else if (arg == "--book") book = value();
            else if (arg == "--smoke-test") smoke = value();
            else if (arg == "--native-window") native = true;
            else if (arg == "--help") {
                std::cout << "EPUB Reader " EPUBREADER_VERSION " / Milestone 2\n"
                    << "--assets DIR --state-dir DIR --book FILE --native-window --smoke-test DIR\n"
                    << "Default: $HOME/books/milestone2.epub if present, otherwise bundled EPUB 2.\n"
                    << "Left/right or q/w: pages; e/r: theme; x: menu; b: reopen; up/down: sample; Esc+Enter: exit.\n"
                    << "Menu: up/down selects; left/right adjusts Font size; a activates; b/x closes.\n";
                return 0;
            } else if (!arg.empty() && arg[0] != '-' && book.empty()) book = arg;
            else throw std::runtime_error("Unknown argument: " + arg);
        }
        if (book.empty()) {
            book = homePath + "/books/milestone2.epub";
            if (::access(book.c_str(), F_OK) != 0) book = assets + "/books/prose-epub2.epub";
        }
        epub::ensureDirectory(stateDirectory);
        std::cout.setf(std::ios::unitbuf);
        if (!std::freopen((stateDirectory + "/reader.log").c_str(), "w", stdout) ||
            !std::freopen((stateDirectory + "/stderr.log").c_str(), "w", stderr)) throw std::runtime_error("Cannot open reader logs");
        std::cout << "EPUBREADER " EPUBREADER_VERSION " Milestone 2\nassets=" << assets << "\nbook=" << book << '\n';
        // Fork before SDL starts any internal threads. No engine APIs run in UI.
        epub::ReaderWorker worker(assets, stateDirectory);
        std::signal(SIGINT, signalHandler); std::signal(SIGTERM, signalHandler);
        SDLGuard guard;
        if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_TIMER) != 0) throw std::runtime_error(SDL_GetError());
        SDL_WM_SetCaption("EPUB Reader - RG280V Milestone 2", nullptr);
        SDL_ShowCursor(SDL_DISABLE); SDL_EnableKeyRepeat(0, 0);
        SDL_Surface* display = SDL_SetVideoMode(native ? 320 : 640, 480, 16,
            native && smoke.empty() ? SDL_HWSURFACE | SDL_DOUBLEBUF : SDL_SWSURFACE);
        if (!display || display->w != (native ? 320 : 640) || display->h != 480) throw std::runtime_error("SDL display initialization failed");
        auto settings = epub::loadState(stateDirectory);
        if (!smoke.empty()) settings = epub::State{};
        epub::ReaderResult page;
        page.font = settings.font; page.theme = settings.theme;
        epub::Canvas canvas;
        epub::ReaderControls controls;
        epub::Input input;
        std::string status = "Loading EPUB";
        bool running = true, dirty = true, positionDirty = false;
        uint32_t saveSince = 0;
        auto save = [&] {
            try {
                epub::saveState(stateDirectory, settings);
                if (!page.anchor.empty()) epub::savePosition(stateDirectory, {page.identity, page.path, page.anchor});
                positionDirty = false;
            } catch (const std::exception& error) {
                status = "SAVE ERROR: " + std::string(error.what());
                std::cerr << status << '\n'; dirty = true;
            }
        };
        auto submit = [&](epub::ReaderRequest request) {
            if (worker.submit(request)) { status = "Loading / rendering"; dirty = true; return true; }
            return false;
        };
        auto open = [&](const std::string& path) {
            save();
            submit({epub::ReaderCommand::Open, 0, settings.font, settings.theme, path});
        };
        auto receive = [&] {
            for (int count = 0; count < 16; ++count) {
                auto result = worker.poll();
                if (!result) break;
                dirty = true;
                if (!result->complete) status = result->message + " " + std::to_string(result->progress) + "%";
                else if (!result->error.empty()) {
                    status = result->error; controls.showMenu(); input.consumeA(); input.consumeRepeat();
                    std::cerr << "READER ERROR: " << status << '\n';
                    // Keep the last successfully displayed/savable page on error.
                } else {
                    page = std::move(*result); status = page.message;
                    settings.font = page.font; settings.theme = page.theme;
                    positionDirty = true; saveSince = SDL_GetTicks();
                    std::cout << "PAGE " << page.page + 1 << '/' << page.pageCount << " font=" << page.font << " theme=" << page.theme
                              << " ms=" << page.elapsedMs << " page-cache=" << page.pageCacheHit << " document-cache=" << page.documentCacheHit
                              << " anchor=" << page.anchor << '\n';
                }
            }
        };
        auto dispatch = [&](epub::Action action) {
            const bool wasOpen = controls.menuOpen();
            const auto control = controls.handle(action, settings.font, settings.theme, worker.busy());
            if (controls.menuOpen() != wasOpen) { input.consumeA(); input.consumeRepeat(); }
            using E = epub::ReaderEffect; using C = epub::ReaderCommand;
            switch (control.effect) {
            case E::Quit: running = false; break;
            case E::Refresh: dirty = true; break;
            case E::Turn: submit({C::Turn, control.value}); break;
            case E::Font: submit({C::Font, control.value}); break;
            case E::Theme: submit({C::Theme, control.value}); break;
            case E::Reopen: open(book); break;
            case E::Specimen:
                book = assets + "/books/prose-epub" + std::to_string(control.value) + ".epub";
                open(book); input.consumeA(); break;
            case E::None: break;
            }
        };
        open(book);
        if (!smoke.empty()) {
            epub::ensureDirectory(smoke);
            auto wait = [&] {
                const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(30);
                while (worker.busy() && std::chrono::steady_clock::now() < deadline) { receive(); SDL_Delay(1); }
                if (worker.busy() || page.pixels.empty() || !status.empty()) throw std::runtime_error("Reader smoke failed: " + status);
            };
            wait();
            dispatch(epub::Action::X); dispatch(epub::Action::Down);
            const epub::ReaderControls hidden;
            for (int size = 0; size < 3; ++size) {
                while (settings.font != size) { dispatch(settings.font < size ? epub::Action::Right : epub::Action::Left); wait(); }
                if (!controls.menuOpen() || controls.selected() != epub::ReaderMenuItem::FontSize)
                    throw std::runtime_error("Font adjustment dismissed or moved the menu");
                epub::renderReaderSlice(canvas, page, settings.theme, hidden, false, "");
                canvas.saveBmp(smoke + "/font-" + std::to_string(size) + ".bmp");
                epub::renderReaderSlice(canvas, page, settings.theme, controls, false, "");
                canvas.saveBmp(smoke + "/font-menu-" + std::to_string(size) + ".bmp");
            }
            dispatch(epub::Action::B);
            for (int theme = 0; theme < static_cast<int>(epub::palettes.size()); ++theme) {
                submit({epub::ReaderCommand::Theme, theme}); wait();
                epub::renderReaderSlice(canvas, page, settings.theme, hidden, false, "");
                canvas.saveBmp(smoke + '/' + epub::palettes[static_cast<size_t>(theme)].name + ".bmp");
            }
            dispatch(epub::Action::X); dispatch(epub::Action::Down);
            epub::renderReaderSlice(canvas, page, settings.theme, controls, false, "");
            canvas.saveBmp(smoke + "/info.bmp"); present(canvas, display, native);
            save(); worker.stop(); std::cout << "SMOKE PASS: EPUB, popup font controls, themes, native composition, state\n"; return 0;
        }
        uint64_t lastLoop = suspendClock();
        while (running && !interrupted) {
            const uint32_t now = SDL_GetTicks();
            const uint64_t clock = suspendClock();
            if (clock - lastLoop > 1500) input.reset();
            lastLoop = clock;
            SDL_Event event;
            while (SDL_PollEvent(&event)) {
                if (event.type == SDL_QUIT) running = false;
                else if (event.type == SDL_ACTIVEEVENT && !event.active.gain) input.reset();
                else if (event.type == SDL_KEYDOWN || event.type == SDL_KEYUP)
                    dispatch(input.key(event.key.keysym.sym, event.type == SDL_KEYDOWN, now));
            }
            if (!running) break;
            dispatch(input.tick(now)); receive();
            if (positionDirty && SDL_GetTicks() - saveSince >= 250) save();
            if (dirty) {
                epub::renderReaderSlice(canvas, page, settings.theme, controls, worker.busy(), status);
                present(canvas, display, native); dirty = false;
            }
            SDL_Delay(8);
        }
        save(); worker.stop(); std::cout << "Clean exit: last completed text anchor saved\n"; return 0;
    } catch (const std::exception& error) { std::cerr << "FATAL: " << error.what() << '\n'; return 1; }
}
